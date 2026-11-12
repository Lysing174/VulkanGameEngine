#pragma once

#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanComputeShader.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanFrameBuffer.h"
#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

namespace Engine {

// Implementation helper for GaussianRenderer. Header-only so existing generated
// IDE projects pick up the new path without editing generated project files.
// Depth sorting happens first in GaussianRenderer; the second stable radix sort
// groups references by tile ID without changing their near-to-far order.
class GaussianTilePipeline {
public:
    GaussianTilePipeline() {
        try { Initialize(); }
        catch (...) { Destroy(); throw; }
    }
    ~GaussianTilePipeline() { Destroy(); }

private:
    void Initialize() {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (uint32_t b = 0; b < 12; ++b) {
            VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            if (b == 0) type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            if (b == 10) type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            if (b == 11) type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bindings.push_back({ b, type, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
        }
        m_Layout = MakeLayout(bindings);
        m_PipelineLayout = MakePipelineLayout(m_Layout);
        m_Set = Allocate(m_Layout);
        const char* names[] = { "GaussianTileCount", "GaussianTileScan", "GaussianTileEmit",
                               "GaussianTileRanges", "GaussianTileRender" };
        for (size_t i = 0; i < m_Stages.size(); ++i) {
            m_Stages[i] = std::make_unique<VulkanComputeShader>(std::string("shaders/spv/") + names[i] + ".comp.spv");
            m_Stages[i]->CreatePipeline(m_PipelineLayout);
        }
        const std::vector<std::vector<uint32_t>> sortBindings = { {0,1,2,3}, {1,2,3}, {0,1,2,3,4,5} };
        const char* sortNames[] = { "GaussianSortUpsweep", "GaussianSortSpine", "GaussianSortDownsweep" };
        for (size_t i = 0; i < 3; ++i) {
            bindings.clear();
            for (uint32_t b : sortBindings[i])
                bindings.push_back({ b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
            m_SortLayouts[i] = MakeLayout(bindings);
            m_SortPipelineLayouts[i] = MakePipelineLayout(m_SortLayouts[i]);
            m_SortShaders[i] = std::make_unique<VulkanComputeShader>(std::string("shaders/spv/") + sortNames[i] + ".comp.spv");
            m_SortShaders[i]->CreatePipeline(m_SortPipelineLayouts[i]);
        }
        for (uint32_t i = 0; i < 2; ++i) {
            m_UpsweepSets[i] = Allocate(m_SortLayouts[0]);
            m_DownsweepSets[i] = Allocate(m_SortLayouts[2]);
        }
        m_SpineSet = Allocate(m_SortLayouts[1]);
    }

    void Destroy() {
        auto* context = VulkanContext::Get();
        auto device = context->GetDevice();
        // Same shared-per-frame resource lifetime as the parent renderer.
        vkDeviceWaitIdle(device);
        for (auto& shader : m_Stages) shader.reset();
        for (auto& shader : m_SortShaders) shader.reset();
        std::array<VkDescriptorSet, 6> sets = { m_Set, m_UpsweepSets[0], m_UpsweepSets[1],
            m_SpineSet, m_DownsweepSets[0], m_DownsweepSets[1] };
        for (auto set : sets)
            if (set != VK_NULL_HANDLE)
                vkFreeDescriptorSets(device, context->GetDescriptorPool(), 1, &set);
        vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, m_Layout, nullptr);
        for (size_t i = 0; i < 3; ++i) {
            vkDestroyPipelineLayout(device, m_SortPipelineLayouts[i], nullptr);
            vkDestroyDescriptorSetLayout(device, m_SortLayouts[i], nullptr);
        }
    }

public:
    void Configure(uint32_t inputCount, const VulkanBuffer& frame, const VulkanBuffer& projected,
                   const VulkanBuffer& sorted, const VulkanBuffer& work,
                   const VulkanBuffer& scratch, const VulkanBuffer& groups) {
        // Called after the parent's device wait, only when scene resources change.
        m_InputGroups = (inputCount + 511u) / 512u;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(VulkanContext::Get()->GetPhysicalDevice(), &properties);
        const uint64_t maxReferences = std::min<uint64_t>(8u * 1024u * 1024u,
            std::min<uint64_t>(properties.limits.maxStorageBufferRange / 4u,
                              uint64_t(properties.limits.maxComputeWorkGroupCount[0]) * 512u));
        // Bounded scratch space, about 144 MiB at the maximum (keys/IDs/histogram).
        m_Capacity = static_cast<uint32_t>(std::min<uint64_t>(maxReferences,
            std::max<uint64_t>(1024u * 1024u, uint64_t(inputCount) * 2u)));
        auto storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        for (uint32_t i = 0; i < 2; ++i) {
            m_Keys[i] = VulkanBuffer(VkDeviceSize(m_Capacity) * 4, storage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            m_IDs[i] = VulkanBuffer(VkDeviceSize(m_Capacity) * 4, storage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        }
        m_Histogram = VulkanBuffer(4096, storage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        m_Partitions = VulkanBuffer(VkDeviceSize((m_Capacity + 511u) / 512u) * 1024,
            storage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        m_Work = VulkanBuffer(32, storage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        WriteBuffer(m_Set, 0, frame, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        WriteBuffer(m_Set, 1, projected);
        WriteBuffer(m_Set, 2, sorted);
        WriteBuffer(m_Set, 3, work);
        WriteBuffer(m_Set, 4, scratch);
        WriteBuffer(m_Set, 5, groups);
        WriteBuffer(m_Set, 6, m_Work);
        WriteBuffer(m_Set, 7, m_Keys[0]);
        WriteBuffer(m_Set, 8, m_IDs[0]);
        for (uint32_t src = 0; src < 2; ++src) {
            WriteBuffer(m_UpsweepSets[src], 0, m_Keys[src]);
            WriteBuffer(m_UpsweepSets[src], 1, m_Histogram);
            WriteBuffer(m_UpsweepSets[src], 2, m_Partitions);
            WriteBuffer(m_UpsweepSets[src], 3, m_Work);
            WriteBuffer(m_DownsweepSets[src], 0, m_Keys[src]);
            WriteBuffer(m_DownsweepSets[src], 1, m_IDs[src]);
            WriteBuffer(m_DownsweepSets[src], 2, m_Partitions);
            WriteBuffer(m_DownsweepSets[src], 3, m_Keys[1-src]);
            WriteBuffer(m_DownsweepSets[src], 4, m_IDs[1-src]);
            WriteBuffer(m_DownsweepSets[src], 5, m_Work);
        }
        WriteBuffer(m_SpineSet, 1, m_Histogram);
        WriteBuffer(m_SpineSet, 2, m_Partitions);
        WriteBuffer(m_SpineSet, 3, m_Work);
    }

    void Prepare(VkCommandBuffer cmd, const VulkanFramebuffer& framebuffer, VkSampler sampler,
                 const glm::mat4& projection) {
        uint32_t width = framebuffer.GetWidth(), height = framebuffer.GetHeight();
        uint32_t gridX = (width + 15u) / 16u, gridY = (height + 15u) / 16u;
        VkDeviceSize rangeBytes = VkDeviceSize(gridX) * gridY * 8;
        if (m_Ranges.GetSize() != rangeBytes) {
            vkDeviceWaitIdle(VulkanContext::Get()->GetDevice());
            m_Ranges = VulkanBuffer(rangeBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            WriteBuffer(m_Set, 9, m_Ranges);
        }
        if (framebuffer.GetColorFormat() != VK_FORMAT_R32G32B32A32_SFLOAT)
            throw std::runtime_error("Gaussian tile rendering requires an RGBA32F framebuffer");
        WriteImage(10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            { sampler, framebuffer.GetGaussianDepthView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL });
        WriteImage(11, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            { VK_NULL_HANDLE, framebuffer.GetResolveView(), VK_IMAGE_LAYOUT_GENERAL });
        m_Parameters.grid = glm::uvec4(gridX, gridY, m_Capacity, m_InputGroups);
        m_Parameters.projection = glm::vec4(projection[2][2], projection[3][2], 0, 0);
        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_Parameters), &m_Parameters);
        Barrier(cmd); // reuse prefix scratch/groups only after cull/compact/sort
        m_Stages[0]->Dispatch(cmd, m_PipelineLayout, m_Set, m_InputGroups, 1, 1);
        Barrier(cmd);
        m_Stages[1]->Dispatch(cmd, m_PipelineLayout, m_Set, 1, 1, 1);
        Barrier(cmd);
        m_Stages[2]->Dispatch(cmd, m_PipelineLayout, m_Set, m_InputGroups, 1, 1);
        Barrier(cmd);
        vkCmdFillBuffer(cmd, m_Histogram.GetBuffer(), 0, m_Histogram.GetSize(), 0);
        vkCmdFillBuffer(cmd, m_Ranges.GetBuffer(), 0, m_Ranges.GetSize(), 0);
        Barrier(cmd);
        for (uint32_t pass = 0; pass < 4; ++pass) {
            uint32_t src = pass % 2;
            uint32_t pc[4] = { pass, 0, 0, 0 };
            for (size_t stage = 0; stage < 3; ++stage) {
                auto layout = m_SortPipelineLayouts[stage];
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
                VkDescriptorSet set = stage == 0 ? m_UpsweepSets[src] : stage == 1 ? m_SpineSet : m_DownsweepSets[src];
                if (stage == 1) m_SortShaders[stage]->Dispatch(cmd, layout, set, 1, 1, 1);
                else m_SortShaders[stage]->DispatchIndirect(cmd, layout, set, m_Work.GetBuffer(), 16);
                Barrier(cmd);
            }
        }
        // Sort pipeline layouts use a different push-constant range: restore ours.
        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_Parameters), &m_Parameters);
        m_Stages[3]->DispatchIndirect(cmd, m_PipelineLayout, m_Set, m_Work.GetBuffer(), 16);
        Barrier(cmd);
    }

    void Composite(VkCommandBuffer cmd, const VulkanFramebuffer& framebuffer) {
        VkImageMemoryBarrier images[2]{};
        for (auto& barrier : images) {
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        }
        images[0].image = framebuffer.GetResolveImage();
        images[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        images[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        images[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        images[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        images[1].image = framebuffer.GetGaussianDepthImage();
        images[1].oldLayout = images[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        images[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        VkFormat depthFormat = VulkanContext::Get()->GetDepthFormat();
        if (depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || depthFormat == VK_FORMAT_D24_UNORM_S8_UINT)
            images[1].subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        images[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        images[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, images);
        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_Parameters), &m_Parameters);
        m_Stages[4]->Dispatch(cmd, m_PipelineLayout, m_Set, m_Parameters.grid.x, m_Parameters.grid.y, 1);
        images[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        images[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        images[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        images[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, images);
    }

private:
    static VkDescriptorSetLayout MakeLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = static_cast<uint32_t>(bindings.size());
        info.pBindings = bindings.data();
        VkDescriptorSetLayout layout;
        if (vkCreateDescriptorSetLayout(VulkanContext::Get()->GetDevice(), &info, nullptr, &layout) != VK_SUCCESS)
            throw std::runtime_error("failed to create Gaussian tile descriptor layout");
        return layout;
    }
    static VkPipelineLayout MakePipelineLayout(VkDescriptorSetLayout layout) {
        VkPushConstantRange pc{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 32 };
        VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        info.setLayoutCount = 1; info.pSetLayouts = &layout;
        info.pushConstantRangeCount = 1; info.pPushConstantRanges = &pc;
        VkPipelineLayout result;
        if (vkCreatePipelineLayout(VulkanContext::Get()->GetDevice(), &info, nullptr, &result) != VK_SUCCESS)
            throw std::runtime_error("failed to create Gaussian tile pipeline layout");
        return result;
    }
    static VkDescriptorSet Allocate(VkDescriptorSetLayout layout) {
        VkDescriptorSetAllocateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        info.descriptorPool = VulkanContext::Get()->GetDescriptorPool();
        info.descriptorSetCount = 1; info.pSetLayouts = &layout;
        VkDescriptorSet result;
        if (vkAllocateDescriptorSets(VulkanContext::Get()->GetDevice(), &info, &result) != VK_SUCCESS)
            throw std::runtime_error("failed to allocate Gaussian tile descriptor set");
        return result;
    }
    static void WriteBuffer(VkDescriptorSet set, uint32_t binding, const VulkanBuffer& buffer,
                            VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
        VkDescriptorBufferInfo info{ buffer.GetBuffer(), 0, buffer.GetSize() };
        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = set; write.dstBinding = binding; write.descriptorCount = 1;
        write.descriptorType = type; write.pBufferInfo = &info;
        vkUpdateDescriptorSets(VulkanContext::Get()->GetDevice(), 1, &write, 0, nullptr);
    }
    void WriteImage(uint32_t binding, VkDescriptorType type, const VkDescriptorImageInfo& info) {
        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = m_Set; write.dstBinding = binding; write.descriptorCount = 1;
        write.descriptorType = type; write.pImageInfo = &info;
        vkUpdateDescriptorSets(VulkanContext::Get()->GetDevice(), 1, &write, 0, nullptr);
    }
    static void Barrier(VkCommandBuffer cmd) {
        VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
            VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
    struct Parameters { glm::uvec4 grid; glm::vec4 projection; } m_Parameters{};
    static_assert(sizeof(Parameters) == 32, "Gaussian tile push constant ABI");
    uint32_t m_Capacity = 0, m_InputGroups = 0;
    VulkanBuffer m_Keys[2], m_IDs[2], m_Histogram, m_Partitions, m_Work, m_Ranges;
    VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_Set = VK_NULL_HANDLE;
    std::array<std::unique_ptr<VulkanComputeShader>, 5> m_Stages;
    std::array<std::unique_ptr<VulkanComputeShader>, 3> m_SortShaders;
    std::array<VkDescriptorSetLayout, 3> m_SortLayouts{};
    std::array<VkPipelineLayout, 3> m_SortPipelineLayouts{};
    VkDescriptorSet m_UpsweepSets[2]{}, m_DownsweepSets[2]{}, m_SpineSet{};
};

} // namespace Engine
