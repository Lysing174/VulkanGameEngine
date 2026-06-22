#include "pch.h"
#include "Engine/Renderer/Renderer.h"
#include "Platform/Vulkan/VulkanRenderer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanBuffer.h"

#include <algorithm>
#include <fstream>

namespace Engine {
    Renderer::API Renderer::s_API = Renderer::API::Vulkan;
	std::vector<MeshRenderCommandRequest> Renderer::s_MeshRenderQueue;
	std::vector<GaussianRenderCommandRequest> Renderer::s_GaussianRenderQueue;
    std::shared_ptr<ShaderLibrary> Renderer::s_ShaderLibrary = std::make_shared<ShaderLibrary>();
    Renderer::SceneData Renderer::s_SceneData;

    // Global Gaussian SSBO
    std::shared_ptr<ShaderStorageBuffer> Renderer::s_GlobalGaussianSSBO;
    uint32_t Renderer::s_TotalGaussianCount = 0;
    bool Renderer::s_GaussianSSBODirty = false;
    std::vector<std::shared_ptr<GaussianModel>> Renderer::s_RegisteredGaussianModels;
    VkDescriptorSet Renderer::s_GaussianDescriptorSet = VK_NULL_HANDLE;

    // 3DGS Splat: FrameInfo UBO + descriptor
    VkBuffer Renderer::s_FrameInfoUBO = VK_NULL_HANDLE;
    VkDeviceMemory Renderer::s_FrameInfoUBOMemory = VK_NULL_HANDLE;
    GaussianFrameInfo Renderer::s_FrameInfoData = {};
    VkDescriptorSet Renderer::s_GaussianSplatDescriptorSet = VK_NULL_HANDLE;

    // 3DGS GPU Sort: ping-pong buffers
    VkBuffer Renderer::s_DistancesBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory Renderer::s_DistancesBufferMemory[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkBuffer Renderer::s_IndicesBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory Renderer::s_IndicesBufferMemory[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

    // 3DGS GPU Sort: histogram buffers
    VkBuffer Renderer::s_GlobalHistogramBuffer = VK_NULL_HANDLE;
    VkDeviceMemory Renderer::s_GlobalHistogramBufferMemory = VK_NULL_HANDLE;
    VkBuffer Renderer::s_PartitionHistogramBuffer = VK_NULL_HANDLE;
    VkDeviceMemory Renderer::s_PartitionHistogramBufferMemory = VK_NULL_HANDLE;

    // 3DGS GPU Sort: compute pipelines
    VkPipeline Renderer::s_DistComputePipeline = VK_NULL_HANDLE;
    VkPipelineLayout Renderer::s_DistComputePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout Renderer::s_DistDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet Renderer::s_DistDescriptorSets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

    VkPipeline Renderer::s_UpsweepPipeline = VK_NULL_HANDLE;
    VkPipelineLayout Renderer::s_UpsweepPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout Renderer::s_UpsweepDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet Renderer::s_UpsweepDescriptorSets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

    VkPipeline Renderer::s_SpinePipeline = VK_NULL_HANDLE;
    VkPipelineLayout Renderer::s_SpinePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout Renderer::s_SpineDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet Renderer::s_SpineDescriptorSet = VK_NULL_HANDLE;

    VkPipeline Renderer::s_DownsweepPipeline = VK_NULL_HANDLE;
    VkPipelineLayout Renderer::s_DownsweepPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout Renderer::s_DownsweepDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet Renderer::s_DownsweepDescriptorSets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };

    uint32_t Renderer::s_CurrentSortBuffer = 0;

    // Model transform SSBO
    VkBuffer Renderer::s_ModelTransformSSBO = VK_NULL_HANDLE;
    VkDeviceMemory Renderer::s_ModelTransformSSBOMemory = VK_NULL_HANDLE;

    // Sort cache
    bool Renderer::s_SortCacheValid = false;
    glm::mat4 Renderer::s_PrevViewMatrix = glm::mat4(0.0f);
    glm::mat4 Renderer::s_PrevProjectionMatrix = glm::mat4(0.0f);
    std::vector<glm::mat4> Renderer::s_PrevModelTransforms;

    // ============================================================
    // Helper: Create a compute pipeline from SPIR-V
    // ============================================================
    static VkPipeline CreateComputePipeline(
        VkDevice device,
        const std::vector<char>& spirvCode,
        VkPipelineLayout pipelineLayout,
        VkPipeline& outPipeline)
    {
        VkShaderModuleCreateInfo moduleInfo{};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = spirvCode.size();
        moduleInfo.pCode = reinterpret_cast<const uint32_t*>(spirvCode.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create compute shader module!");
        }

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = shaderModule;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = pipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create compute pipeline!");
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
        return outPipeline;
    }

    static std::vector<char> ReadShaderFile(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open shader file: " + filename);
        }
        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    }

    // ============================================================
    // Init
    // ============================================================
    void Renderer::Init()
    {
        // Mesh Shader: PBR
        s_ShaderLibrary->Load("shaders/Mesh.vert.spv", "shaders/Mesh.frag.spv", ShaderConfig::DefaultPBR());

        // Gaussian Splat Shader: 3DGS instanced quad rendering
        s_ShaderLibrary->Load("shaders/GaussianSplat.vert.spv", "shaders/GaussianSplat.frag.spv", ShaderConfig::GaussianSplat());
    }

    // ============================================================
    // GaussianModel Registration
    // ============================================================
    void Renderer::RegisterGaussianModel(const std::shared_ptr<GaussianModel>& model)
    {
        if (!model || model->IsRegistered()) return;

        for (const auto& registered : s_RegisteredGaussianModels)
        {
            if (registered.get() == model.get()) return;
        }

        model->SetGlobalOffset(s_TotalGaussianCount);
        model->SetModelIndex((uint32_t)s_RegisteredGaussianModels.size());
        s_TotalGaussianCount += model->GetGaussianCount();
        s_RegisteredGaussianModels.push_back(model);
        s_GaussianSSBODirty = true;

        EG_CORE_INFO("Registered GaussianModel: offset={0}, count={1}, total={2}",
            model->GetGlobalOffset(), model->GetGaussianCount(), s_TotalGaussianCount);
    }

    // ============================================================
    // Rebuild Global Gaussian SSBO
    // ============================================================
    void Renderer::RebuildGlobalGaussianSSBO()
    {
        if (s_RegisteredGaussianModels.empty()) return;

        auto context = VulkanContext::Get();

        // Collect all GPU data from all registered models
        std::vector<GaussianDataGPU> allData;
        allData.reserve(s_TotalGaussianCount);
        for (const auto& model : s_RegisteredGaussianModels)
        {
            auto gpuData = model->GetGPUData();
            allData.insert(allData.end(), gpuData.begin(), gpuData.end());
        }

        // Create new SSBO with all data
        s_GlobalGaussianSSBO = ShaderStorageBuffer::Create(
            allData.data(), (uint32_t)(allData.size() * sizeof(GaussianDataGPU)));

        // Create GPU sort buffers and pipelines FIRST (so GaussianSplat DS can reference valid buffers)
        CreateGaussianSortPipelines();

        // Create 3DGS buffers and descriptor sets (references sort buffers)
        CreateGaussianSplatBuffers();

        s_GaussianSSBODirty = false;

        EG_CORE_INFO("Rebuilt global Gaussian SSBO: {0} gaussians, {1} bytes",
            allData.size(), allData.size() * sizeof(GaussianDataGPU));
    }

    // ============================================================
    // Create GaussianSplat descriptor set (FrameInfo UBO + GaussianData SSBO + SortedIndices SSBO)
    // ============================================================
    void Renderer::CreateGaussianSplatBuffers()
    {
        auto context = VulkanContext::Get();
        auto device = context->GetDevice();

        // --- FrameInfo UBO (host-visible, updated every frame) ---
        if (s_FrameInfoUBO != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device, s_FrameInfoUBO, nullptr);
            vkFreeMemory(device, s_FrameInfoUBOMemory, nullptr);
        }
        context->CreateBuffer(sizeof(GaussianFrameInfo),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            s_FrameInfoUBO, s_FrameInfoUBOMemory);

        // --- GaussianSplat DescriptorSet ---
        auto splatShader = s_ShaderLibrary->Get("GaussianSplat.vert.spv");
        auto vkShader = std::static_pointer_cast<VulkanShader>(splatShader);

        if (s_GaussianSplatDescriptorSet != VK_NULL_HANDLE)
        {
            vkFreeDescriptorSets(device, context->GetDescriptorPool(), 1, &s_GaussianSplatDescriptorSet);
            s_GaussianSplatDescriptorSet = VK_NULL_HANDLE;
        }

        VkDescriptorSetLayout layout = vkShader->GetMaterialDescriptorSetLayout();
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = context->GetDescriptorPool();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        if (vkAllocateDescriptorSets(device, &allocInfo, &s_GaussianSplatDescriptorSet) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate GaussianSplat descriptor set!");
        }

        // Write descriptor sets (initially with empty buffers, will be updated after sort)
        std::vector<VkWriteDescriptorSet> writes;

        // Binding 0: FrameInfo UBO
        VkDescriptorBufferInfo frameInfoBufInfo{};
        frameInfoBufInfo.buffer = s_FrameInfoUBO;
        frameInfoBufInfo.offset = 0;
        frameInfoBufInfo.range = sizeof(GaussianFrameInfo);

        VkWriteDescriptorSet frameInfoWrite{};
        frameInfoWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        frameInfoWrite.dstSet = s_GaussianSplatDescriptorSet;
        frameInfoWrite.dstBinding = 0;
        frameInfoWrite.dstArrayElement = 0;
        frameInfoWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        frameInfoWrite.descriptorCount = 1;
        frameInfoWrite.pBufferInfo = &frameInfoBufInfo;
        writes.push_back(frameInfoWrite);

        // Binding 1: GaussianData SSBO
        VkDescriptorBufferInfo ssboBufInfo{};
        if (s_GlobalGaussianSSBO)
        {
            auto vkSSBO = std::static_pointer_cast<VulkanShaderStorageBuffer>(s_GlobalGaussianSSBO);
            ssboBufInfo.buffer = vkSSBO->GetVulkanBuffer();
            ssboBufInfo.offset = 0;
            ssboBufInfo.range = s_TotalGaussianCount * sizeof(GaussianDataGPU);

            VkWriteDescriptorSet ssboWrite{};
            ssboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ssboWrite.dstSet = s_GaussianSplatDescriptorSet;
            ssboWrite.dstBinding = 1;
            ssboWrite.dstArrayElement = 0;
            ssboWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            ssboWrite.descriptorCount = 1;
            ssboWrite.pBufferInfo = &ssboBufInfo;
            writes.push_back(ssboWrite);
        }

        // Binding 2: SortedIndices SSBO (will point to the sorted indices buffer)
        // Initially point to indices buffer 0 (will be updated after sort)
        VkDescriptorBufferInfo sortedBufInfo{};
        sortedBufInfo.buffer = s_IndicesBuffer[0] != VK_NULL_HANDLE ? s_IndicesBuffer[0] : VK_NULL_HANDLE;
        sortedBufInfo.offset = 0;
        sortedBufInfo.range = s_TotalGaussianCount * sizeof(uint32_t);

        VkWriteDescriptorSet sortedWrite{};
        sortedWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        sortedWrite.dstSet = s_GaussianSplatDescriptorSet;
        sortedWrite.dstBinding = 2;
        sortedWrite.dstArrayElement = 0;
        sortedWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sortedWrite.descriptorCount = 1;
        sortedWrite.pBufferInfo = &sortedBufInfo;
        writes.push_back(sortedWrite);

        // Binding 3: Depth texture (combined image sampler from mesh render pass)
        VkDescriptorImageInfo depthImageInfo{};
        depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depthImageInfo.imageView = context->GetDepthImageView();
        depthImageInfo.sampler = context->GetDepthSampler();

        VkWriteDescriptorSet depthWrite{};
        depthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        depthWrite.dstSet = s_GaussianSplatDescriptorSet;
        depthWrite.dstBinding = 3;
        depthWrite.dstArrayElement = 0;
        depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        depthWrite.descriptorCount = 1;
        depthWrite.pImageInfo = &depthImageInfo;
        writes.push_back(depthWrite);

        // Binding 4: Model Transform SSBO (one mat4 per model)
        if (s_ModelTransformSSBO != VK_NULL_HANDLE)
        {
            uint32_t modelCount = (uint32_t)s_RegisteredGaussianModels.size();
            VkDescriptorBufferInfo transformBufInfo{};
            transformBufInfo.buffer = s_ModelTransformSSBO;
            transformBufInfo.offset = 0;
            transformBufInfo.range = modelCount * sizeof(glm::mat4);

            VkWriteDescriptorSet transformWrite{};
            transformWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            transformWrite.dstSet = s_GaussianSplatDescriptorSet;
            transformWrite.dstBinding = 4;
            transformWrite.dstArrayElement = 0;
            transformWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            transformWrite.descriptorCount = 1;
            transformWrite.pBufferInfo = &transformBufInfo;
            writes.push_back(transformWrite);
        }

        vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    }

    void Renderer::CreateGaussianDescriptorSet()
    {
        // Legacy - kept for compatibility but not used by GaussianSplat pipeline
    }

    void Renderer::OnSwapchainRecreated()
    {
        if (s_TotalGaussianCount > 0 && s_GlobalGaussianSSBO)
        {
            CreateGaussianSortPipelines();
            CreateGaussianSplatBuffers();
            s_SortCacheValid = false;
        }
    }

    // ============================================================
    // Create GPU Sort Pipelines and Buffers
    // ============================================================
    void Renderer::CreateGaussianSortPipelines()
    {
        auto context = VulkanContext::Get();
        auto device = context->GetDevice();
        VkDeviceSize bufferSize = s_TotalGaussianCount * sizeof(uint32_t);
        uint32_t numPartitions = (s_TotalGaussianCount + 511) / 512;

        // --- Destroy old resources ---
        DestroyGaussianSortResources();

        // --- Create Model Transform SSBO (one mat4 per model, host-visible for per-frame updates) ---
        {
            uint32_t modelCount = (uint32_t)s_RegisteredGaussianModels.size();
            VkDeviceSize transformBufferSize = modelCount * sizeof(glm::mat4);

            if (modelCount > 0)
            {
                context->CreateBuffer(transformBufferSize,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    s_ModelTransformSSBO, s_ModelTransformSSBOMemory);
            }
        }

        // --- Create ping-pong distance/keys + indices/values buffers ---
        for (int i = 0; i < 2; i++)
        {
            context->CreateBuffer(bufferSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                s_DistancesBuffer[i], s_DistancesBufferMemory[i]);

            context->CreateBuffer(bufferSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                s_IndicesBuffer[i], s_IndicesBufferMemory[i]);
        }

        // Initialize indices buffers with sequential indices [0, 1, 2, ...]
        {
            std::vector<uint32_t> initIndices(s_TotalGaussianCount);
            for (uint32_t i = 0; i < s_TotalGaussianCount; i++)
                initIndices[i] = i;

            VkBuffer stagingBuffer;
            VkDeviceMemory stagingMemory;
            context->CreateBuffer(bufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer, stagingMemory);

            void* data;
            vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);
            memcpy(data, initIndices.data(), bufferSize);
            vkUnmapMemory(device, stagingMemory);

            context->CopyBuffer(stagingBuffer, s_IndicesBuffer[0], bufferSize);
            context->CopyBuffer(stagingBuffer, s_IndicesBuffer[1], bufferSize);

            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingMemory, nullptr);
        }

        // --- Histogram buffers ---
        context->CreateBuffer(1024 * sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            s_GlobalHistogramBuffer, s_GlobalHistogramBufferMemory);

        context->CreateBuffer(VkDeviceSize(numPartitions) * 256 * sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            s_PartitionHistogramBuffer, s_PartitionHistogramBufferMemory);

        // ============================================================
        // Dist Compute Pipeline
        // ============================================================
        {
            VkDescriptorSetLayoutBinding distBindings[4] = {};
            distBindings[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
            distBindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
            distBindings[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
            distBindings[3] = { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }; // ModelTransform

            VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            layoutInfo.bindingCount = 4;
            layoutInfo.pBindings = distBindings;
            vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &s_DistDescriptorSetLayout);

            VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 2 * sizeof(glm::mat4) };
            VkPipelineLayoutCreateInfo plLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            plLayoutInfo.setLayoutCount = 1;
            plLayoutInfo.pSetLayouts = &s_DistDescriptorSetLayout;
            plLayoutInfo.pushConstantRangeCount = 1;
            plLayoutInfo.pPushConstantRanges = &pcRange;
            vkCreatePipelineLayout(device, &plLayoutInfo, nullptr, &s_DistComputePipelineLayout);

            auto distCode = ReadShaderFile("shaders/GaussianDist.comp.spv");
            CreateComputePipeline(device, distCode, s_DistComputePipelineLayout, s_DistComputePipeline);

            // Dist always writes to buffer 0, so only need 1 descriptor set
            VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            allocInfo.descriptorPool = context->GetDescriptorPool();
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &s_DistDescriptorSetLayout;
            vkAllocateDescriptorSets(device, &allocInfo, &s_DistDescriptorSets[0]);

            auto vkSSBO = std::static_pointer_cast<VulkanShaderStorageBuffer>(s_GlobalGaussianSSBO);
            VkDescriptorBufferInfo ssboInfo{ vkSSBO->GetVulkanBuffer(), 0, s_TotalGaussianCount * sizeof(GaussianDataGPU) };
            VkDescriptorBufferInfo distInfo{ s_DistancesBuffer[0], 0, bufferSize };
            VkDescriptorBufferInfo idxInfo{ s_IndicesBuffer[0], 0, bufferSize };
            uint32_t modelCount = (uint32_t)s_RegisteredGaussianModels.size();
            VkDescriptorBufferInfo transformInfo{ s_ModelTransformSSBO, 0, modelCount * sizeof(glm::mat4) };

            VkWriteDescriptorSet writes[4] = {};
            for (int w = 0; w < 4; w++)
            {
                writes[w].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[w].dstSet = s_DistDescriptorSets[0];
                writes[w].dstBinding = w;
                writes[w].descriptorCount = 1;
                writes[w].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            }
            writes[0].pBufferInfo = &ssboInfo;
            writes[1].pBufferInfo = &distInfo;
            writes[2].pBufferInfo = &idxInfo;
            writes[3].pBufferInfo = &transformInfo;
            vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
        }

        // ============================================================
        // Upsweep Pipeline — 2 descriptor sets for ping-pong
        // [0]: keysIn = distances[0], [1]: keysIn = distances[1]
        // ============================================================
        {
            VkDescriptorSetLayoutBinding bindings[3] = {};
            bindings[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
            bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
            bindings[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };

            VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            layoutInfo.bindingCount = 3;
            layoutInfo.pBindings = bindings;
            vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &s_UpsweepDescriptorSetLayout);

            VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 4 * sizeof(uint32_t) };
            VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            plInfo.setLayoutCount = 1;
            plInfo.pSetLayouts = &s_UpsweepDescriptorSetLayout;
            plInfo.pushConstantRangeCount = 1;
            plInfo.pPushConstantRanges = &pcRange;
            vkCreatePipelineLayout(device, &plInfo, nullptr, &s_UpsweepPipelineLayout);

            auto code = ReadShaderFile("shaders/GaussianSortUpsweep.comp.spv");
            CreateComputePipeline(device, code, s_UpsweepPipelineLayout, s_UpsweepPipeline);

            VkDescriptorBufferInfo globalHistInfo{ s_GlobalHistogramBuffer, 0, 1024 * sizeof(uint32_t) };
            VkDescriptorBufferInfo partHistInfo{ s_PartitionHistogramBuffer, 0, VkDeviceSize(numPartitions) * 256 * sizeof(uint32_t) };

            for (int i = 0; i < 2; i++)
            {
                VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
                allocInfo.descriptorPool = context->GetDescriptorPool();
                allocInfo.descriptorSetCount = 1;
                allocInfo.pSetLayouts = &s_UpsweepDescriptorSetLayout;
                vkAllocateDescriptorSets(device, &allocInfo, &s_UpsweepDescriptorSets[i]);

                VkDescriptorBufferInfo keysInfo{ s_DistancesBuffer[i], 0, bufferSize };

                VkWriteDescriptorSet writes[3] = {};
                writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_UpsweepDescriptorSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &keysInfo, nullptr };
                writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_UpsweepDescriptorSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &globalHistInfo, nullptr };
                writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_UpsweepDescriptorSets[i], 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &partHistInfo, nullptr };
                vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
            }
        }

        // ============================================================
        // Spine Pipeline — 1 descriptor set (histograms don't change)
        // ============================================================
        {
            VkDescriptorSetLayoutBinding bindings[2] = {};
            bindings[0] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
            bindings[1] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };

            VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            layoutInfo.bindingCount = 2;
            layoutInfo.pBindings = bindings;
            vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &s_SpineDescriptorSetLayout);

            VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 4 * sizeof(uint32_t) };
            VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            plInfo.setLayoutCount = 1;
            plInfo.pSetLayouts = &s_SpineDescriptorSetLayout;
            plInfo.pushConstantRangeCount = 1;
            plInfo.pPushConstantRanges = &pcRange;
            vkCreatePipelineLayout(device, &plInfo, nullptr, &s_SpinePipelineLayout);

            auto code = ReadShaderFile("shaders/GaussianSortSpine.comp.spv");
            CreateComputePipeline(device, code, s_SpinePipelineLayout, s_SpinePipeline);

            VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            allocInfo.descriptorPool = context->GetDescriptorPool();
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &s_SpineDescriptorSetLayout;
            vkAllocateDescriptorSets(device, &allocInfo, &s_SpineDescriptorSet);

            VkDescriptorBufferInfo globalHistInfo{ s_GlobalHistogramBuffer, 0, 1024 * sizeof(uint32_t) };
            VkDescriptorBufferInfo partHistInfo{ s_PartitionHistogramBuffer, 0, VkDeviceSize(numPartitions) * 256 * sizeof(uint32_t) };

            VkWriteDescriptorSet writes[2] = {};
            writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_SpineDescriptorSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &globalHistInfo, nullptr };
            writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_SpineDescriptorSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &partHistInfo, nullptr };
            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        }

        // ============================================================
        // Downsweep Pipeline — 2 descriptor sets for ping-pong
        // [0]: in=buf0 out=buf1, [1]: in=buf1 out=buf0
        // ============================================================
        {
            VkDescriptorSetLayoutBinding bindings[5] = {};
            bindings[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
            bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
            bindings[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
            bindings[3] = { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
            bindings[4] = { 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };

            VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            layoutInfo.bindingCount = 5;
            layoutInfo.pBindings = bindings;
            vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &s_DownsweepDescriptorSetLayout);

            VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 4 * sizeof(uint32_t) };
            VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            plInfo.setLayoutCount = 1;
            plInfo.pSetLayouts = &s_DownsweepDescriptorSetLayout;
            plInfo.pushConstantRangeCount = 1;
            plInfo.pPushConstantRanges = &pcRange;
            vkCreatePipelineLayout(device, &plInfo, nullptr, &s_DownsweepPipelineLayout);

            auto code = ReadShaderFile("shaders/GaussianSortDownsweep.comp.spv");
            CreateComputePipeline(device, code, s_DownsweepPipelineLayout, s_DownsweepPipeline);

            VkDescriptorBufferInfo partHistInfo{ s_PartitionHistogramBuffer, 0, VkDeviceSize(numPartitions) * 256 * sizeof(uint32_t) };

            for (int i = 0; i < 2; i++)
            {
                int j = 1 - i; // opposite buffer
                VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
                allocInfo.descriptorPool = context->GetDescriptorPool();
                allocInfo.descriptorSetCount = 1;
                allocInfo.pSetLayouts = &s_DownsweepDescriptorSetLayout;
                vkAllocateDescriptorSets(device, &allocInfo, &s_DownsweepDescriptorSets[i]);

                VkDescriptorBufferInfo keysInInfo{ s_DistancesBuffer[i], 0, bufferSize };
                VkDescriptorBufferInfo valuesInInfo{ s_IndicesBuffer[i], 0, bufferSize };
                VkDescriptorBufferInfo keysOutInfo{ s_DistancesBuffer[j], 0, bufferSize };
                VkDescriptorBufferInfo valuesOutInfo{ s_IndicesBuffer[j], 0, bufferSize };

                VkWriteDescriptorSet writes[5] = {};
                writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_DownsweepDescriptorSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &keysInInfo, nullptr };
                writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_DownsweepDescriptorSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &valuesInInfo, nullptr };
                writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_DownsweepDescriptorSets[i], 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &partHistInfo, nullptr };
                writes[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_DownsweepDescriptorSets[i], 3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &keysOutInfo, nullptr };
                writes[4] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_DownsweepDescriptorSets[i], 4, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &valuesOutInfo, nullptr };
                vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
            }
        }
    }

    void Renderer::CreateGaussianSortDescriptorSets()
    {
        // Descriptor sets are created in CreateGaussianSortPipelines
        // This function updates them for the current frame's ping-pong state
    }

    // ============================================================
    // Destroy GPU Sort Resources
    // ============================================================
    void Renderer::DestroyGaussianSortResources()
    {
        auto device = VulkanContext::Get()->GetDevice();

        // Model Transform SSBO
        if (s_ModelTransformSSBO) vkDestroyBuffer(device, s_ModelTransformSSBO, nullptr);
        if (s_ModelTransformSSBOMemory) vkFreeMemory(device, s_ModelTransformSSBOMemory, nullptr);
        s_ModelTransformSSBO = VK_NULL_HANDLE;
        s_ModelTransformSSBOMemory = VK_NULL_HANDLE;

        for (int i = 0; i < 2; i++)
        {
            if (s_DistancesBuffer[i]) vkDestroyBuffer(device, s_DistancesBuffer[i], nullptr);
            if (s_DistancesBufferMemory[i]) vkFreeMemory(device, s_DistancesBufferMemory[i], nullptr);
            s_DistancesBuffer[i] = VK_NULL_HANDLE;
            s_DistancesBufferMemory[i] = VK_NULL_HANDLE;

            if (s_IndicesBuffer[i]) vkDestroyBuffer(device, s_IndicesBuffer[i], nullptr);
            if (s_IndicesBufferMemory[i]) vkFreeMemory(device, s_IndicesBufferMemory[i], nullptr);
            s_IndicesBuffer[i] = VK_NULL_HANDLE;
            s_IndicesBufferMemory[i] = VK_NULL_HANDLE;
        }

        if (s_GlobalHistogramBuffer) vkDestroyBuffer(device, s_GlobalHistogramBuffer, nullptr);
        if (s_GlobalHistogramBufferMemory) vkFreeMemory(device, s_GlobalHistogramBufferMemory, nullptr);
        s_GlobalHistogramBuffer = VK_NULL_HANDLE;
        s_GlobalHistogramBufferMemory = VK_NULL_HANDLE;

        if (s_PartitionHistogramBuffer) vkDestroyBuffer(device, s_PartitionHistogramBuffer, nullptr);
        if (s_PartitionHistogramBufferMemory) vkFreeMemory(device, s_PartitionHistogramBufferMemory, nullptr);
        s_PartitionHistogramBuffer = VK_NULL_HANDLE;
        s_PartitionHistogramBufferMemory = VK_NULL_HANDLE;

        if (s_DistComputePipeline) vkDestroyPipeline(device, s_DistComputePipeline, nullptr);
        if (s_DistComputePipelineLayout) vkDestroyPipelineLayout(device, s_DistComputePipelineLayout, nullptr);
        if (s_DistDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, s_DistDescriptorSetLayout, nullptr);
        s_DistComputePipeline = VK_NULL_HANDLE;
        s_DistComputePipelineLayout = VK_NULL_HANDLE;
        s_DistDescriptorSetLayout = VK_NULL_HANDLE;

        if (s_UpsweepPipeline) vkDestroyPipeline(device, s_UpsweepPipeline, nullptr);
        if (s_UpsweepPipelineLayout) vkDestroyPipelineLayout(device, s_UpsweepPipelineLayout, nullptr);
        if (s_UpsweepDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, s_UpsweepDescriptorSetLayout, nullptr);
        s_UpsweepPipeline = VK_NULL_HANDLE;
        s_UpsweepPipelineLayout = VK_NULL_HANDLE;
        s_UpsweepDescriptorSetLayout = VK_NULL_HANDLE;

        if (s_SpinePipeline) vkDestroyPipeline(device, s_SpinePipeline, nullptr);
        if (s_SpinePipelineLayout) vkDestroyPipelineLayout(device, s_SpinePipelineLayout, nullptr);
        if (s_SpineDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, s_SpineDescriptorSetLayout, nullptr);
        s_SpinePipeline = VK_NULL_HANDLE;
        s_SpinePipelineLayout = VK_NULL_HANDLE;
        s_SpineDescriptorSetLayout = VK_NULL_HANDLE;

        if (s_DownsweepPipeline) vkDestroyPipeline(device, s_DownsweepPipeline, nullptr);
        if (s_DownsweepPipelineLayout) vkDestroyPipelineLayout(device, s_DownsweepPipelineLayout, nullptr);
        if (s_DownsweepDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, s_DownsweepDescriptorSetLayout, nullptr);
        s_DownsweepPipeline = VK_NULL_HANDLE;
        s_DownsweepPipelineLayout = VK_NULL_HANDLE;
        s_DownsweepDescriptorSetLayout = VK_NULL_HANDLE;

        // Free descriptor sets (they go back to pool on pool destruction)
        for (int i = 0; i < 2; i++)
        {
            s_DistDescriptorSets[i] = VK_NULL_HANDLE;
            s_UpsweepDescriptorSets[i] = VK_NULL_HANDLE;
            s_DownsweepDescriptorSets[i] = VK_NULL_HANDLE;
        }
        s_SpineDescriptorSet = VK_NULL_HANDLE;
    }

    // ============================================================
    // GPU Sorting
    // ============================================================
    void Renderer::SortGaussiansOnGPU()
    {
        if (s_TotalGaussianCount == 0) return;

        auto context = VulkanContext::Get();
        VkCommandBuffer cmd = context->GetCurrentCommandBuffer();
        uint32_t N = s_TotalGaussianCount;
        uint32_t numPartitions = (N + 511) / 512;

        // ============================================================
        // Step 1: Compute distances + initial indices
        // ============================================================
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_DistComputePipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                s_DistComputePipelineLayout, 0, 1, &s_DistDescriptorSets[0], 0, nullptr);

            struct DistPushConstants {
                glm::mat4 viewMatrix;
                glm::mat4 projectionMatrix;
            };
            DistPushConstants pc{};
            pc.viewMatrix = s_SceneData.ViewMatrix;
            pc.projectionMatrix = s_SceneData.ProjectionMatrix;
            vkCmdPushConstants(cmd, s_DistComputePipelineLayout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DistPushConstants), &pc);

            vkCmdDispatch(cmd, (N + 255) / 256, 1, 1);

            VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }

        // ============================================================
        // Step 2: 4-pass Radix Sort (NO vkUpdateDescriptorSets!)
        // ============================================================
        uint32_t currentBuffer = 0;

        for (uint32_t passNum = 0; passNum < 4; passNum++)
        {
            // --- 2a: Clear global histogram ---
            vkCmdFillBuffer(cmd, s_GlobalHistogramBuffer, 0, 1024 * sizeof(uint32_t), 0);

            {
                VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            }

            // --- 2b: Upsweep ---
            {
                uint32_t pushConstants[4] = { passNum, N, 0, 0 };
                vkCmdPushConstants(cmd, s_UpsweepPipelineLayout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), pushConstants);

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_UpsweepPipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                    s_UpsweepPipelineLayout, 0, 1, &s_UpsweepDescriptorSets[currentBuffer], 0, nullptr);
                vkCmdDispatch(cmd, numPartitions, 1, 1);

                VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            }

            // --- 2c: Spine (prefix sum) ---
            {
                uint32_t pushConstants[4] = { passNum, N, numPartitions, 0 };
                vkCmdPushConstants(cmd, s_SpinePipelineLayout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), pushConstants);

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_SpinePipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                    s_SpinePipelineLayout, 0, 1, &s_SpineDescriptorSet, 0, nullptr);
                vkCmdDispatch(cmd, 1, 1, 1);

                VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            }

            // --- 2d: Downsweep (scatter) ---
            {
                uint32_t pushConstants[4] = { passNum, N, numPartitions, 0 };
                vkCmdPushConstants(cmd, s_DownsweepPipelineLayout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), pushConstants);

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_DownsweepPipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                    s_DownsweepPipelineLayout, 0, 1, &s_DownsweepDescriptorSets[currentBuffer], 0, nullptr);
                vkCmdDispatch(cmd, numPartitions, 1, 1);

                if (passNum < 3)
                {
                    VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
                    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
                }

                currentBuffer = 1 - currentBuffer;
            }
        }

        // After 4 passes, currentBuffer holds the sorted output
        s_CurrentSortBuffer = currentBuffer;

        // Final barrier: compute → vertex (for GaussianSplat.vert to read indices)
        VkMemoryBarrier finalBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        finalBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        finalBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &finalBarrier, 0, nullptr, 0, nullptr);

        // Update GaussianSplat descriptor set binding 2 → sorted indices buffer
        // This is the ONLY vkUpdateDescriptorSets call per frame
        {
            VkDescriptorBufferInfo sortedBufInfo{ s_IndicesBuffer[s_CurrentSortBuffer], 0, N * sizeof(uint32_t) };
            VkWriteDescriptorSet sortedWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            sortedWrite.dstSet = s_GaussianSplatDescriptorSet;
            sortedWrite.dstBinding = 2;
            sortedWrite.descriptorCount = 1;
            sortedWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            sortedWrite.pBufferInfo = &sortedBufInfo;
            vkUpdateDescriptorSets(context->GetDevice(), 1, &sortedWrite, 0, nullptr);
        }
    }

    // ============================================================
    // Scene lifecycle
    // ============================================================
	void Renderer::BeginScene(const EditorCamera& camera)
	{
        s_MeshRenderQueue.clear();
        s_GaussianRenderQueue.clear();

        s_SceneData.CameraPosition = camera.GetPosition();
        s_SceneData.CameraForward  = camera.GetForwardDirection();
        s_SceneData.ViewMatrix     = camera.GetViewMatrix();
        s_SceneData.ProjectionMatrix = camera.GetProjection();
        s_SceneData.ProjectionMatrix[1][1] *= -1; // Vulkan Y-flip

        switch (Renderer::GetAPI())
        {
        case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
        case Renderer::API::Vulkan:  VulkanRenderer::BeginScene(camera); return;
        }

        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return;
	}

	void Renderer::EndScene()
	{
		switch (Renderer::GetAPI())
		{
		case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
		case Renderer::API::Vulkan:
			{
				// Update light uniforms before mesh pass
				{
					auto context = VulkanContext::Get();
					glm::vec4 cameraPos(s_SceneData.CameraPosition, 0.0f);
					glm::vec4 lightPos(s_SceneData.LightPosition, 0.0f);
					glm::vec4 lightCol(s_SceneData.LightColor, s_SceneData.LightIntensity);
					context->UpdateGlobalLightUniforms(cameraPos, lightPos, lightCol);
				}

				VulkanRenderer::BeginMeshRenderPass(); 
				FlushMeshPass();
				VulkanRenderer::EndRenderPass();

				// GPU compute (sort) must happen OUTSIDE render pass
				FlushGaussianPass();  // now handles sort (outside RP) + draw (inside RP)
				break;
			}
		default: EG_CORE_ASSERT(false, "Unknown RendererAPI!"); return;
		}
	}

	void Renderer::PresentFrame()
	{
		switch (Renderer::GetAPI())
		{
		case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
		case Renderer::API::Vulkan:
			{
				VulkanRenderer::DrawImGui();
				VulkanRenderer::EndRenderPass();
				VulkanRenderer::EndScene();
				break;
			}
		default: EG_CORE_ASSERT(false, "Unknown RendererAPI!"); return;
		}
	}

    void Renderer::SubmitMesh(const glm::mat4& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<MeshRendererComponent>& rendererComponent, int entityID)
    {
        for (const auto& submesh : mesh->GetSubmeshes())
        {
            std::shared_ptr<Material> material = rendererComponent->GetMaterial(submesh.MaterialIndex);
            if (!material) { EG_CORE_WARN("Can't match material!"); continue; }

            glm::mat4 finalTransform = transform * submesh.Transform;

            MeshRenderCommandRequest request;
            request.Mesh = mesh;
            request.Material = material;
            request.Transform = finalTransform;
            request.EntityID = entityID;
            request.SubmeshIndexCount = submesh.IndexCount;
            request.SubmeshFirstIndex = submesh.FirstIndex;
            request.SubmeshFirstVertex = submesh.FirstVertex;

            s_MeshRenderQueue.push_back(request);
        }
    }

    void Renderer::SubmitGaussian(const glm::mat4& transform, const std::shared_ptr<GaussianModel>& model, int entityID)
    {
        GaussianRenderCommandRequest request;
        request.Transform = transform;
        request.Model = model;
        request.EntityID = entityID;

        s_GaussianRenderQueue.push_back(request);
    }

    void Renderer::SetPointLightData(const glm::vec3& position, const glm::vec3& color, float intensity)
    {
        s_SceneData.LightPosition = position;
        s_SceneData.LightColor = color;
        s_SceneData.LightIntensity = intensity;
    }

    void Renderer::FlushMeshPass()
    {
        if (s_MeshRenderQueue.empty()) return;

        std::sort(s_MeshRenderQueue.begin(), s_MeshRenderQueue.end(),
            [](const MeshRenderCommandRequest& a, const MeshRenderCommandRequest& b) {
                uint32_t shaderA = a.Material->GetShader()->GetRendererID();
                uint32_t shaderB = b.Material->GetShader()->GetRendererID();
                if (shaderA != shaderB) return shaderA < shaderB;
                return a.Material->GetRendererID() < b.Material->GetRendererID();
            });

        std::shared_ptr<Shader>   lastShader   = nullptr;
        std::shared_ptr<Material> lastMaterial  = nullptr;

        for (auto& command : s_MeshRenderQueue)
        {
            if (command.Material->GetShader() != lastShader)
            {
                lastShader = command.Material->GetShader();
                lastShader->Bind();
                lastMaterial = nullptr;
            }

            if (command.Material != lastMaterial)
            {
                lastMaterial = command.Material;
                lastMaterial->Bind();
            }
            DrawMesh(command);
        }
    }

    void Renderer::FlushGaussianPass()
    {
        if (s_GaussianRenderQueue.empty()) return;

        // Rebuild global SSBO if dirty (invalidates sort cache since buffers are recreated)
        if (s_GaussianSSBODirty)
        {
            RebuildGlobalGaussianSSBO();
            s_SortCacheValid = false;
        }

        if (!s_GlobalGaussianSSBO || s_TotalGaussianCount == 0) return;

        auto context = VulkanContext::Get();
        auto device = context->GetDevice();
        auto extent = context->GetSwapChainExtent();
        float viewportW = (float)extent.width;
        float viewportH = (float)extent.height;

        glm::mat4 viewMatrix = s_SceneData.ViewMatrix;
        glm::mat4 projMatrix = s_SceneData.ProjectionMatrix;

        float focalX = projMatrix[0][0] * viewportW * 0.5f;
        float focalY = projMatrix[1][1] * viewportH * 0.5f;

        // Update FrameInfo UBO (host-visible, immediate upload)
        s_FrameInfoData.viewMatrix = viewMatrix;
        s_FrameInfoData.projectionMatrix = projMatrix;
        s_FrameInfoData.cameraPosAndScale = glm::vec4(s_SceneData.CameraPosition, 1.0f);
        s_FrameInfoData.focal = glm::vec4(focalX, focalY, 0.0f, 0.0f);
        s_FrameInfoData.viewportInfo = glm::vec4(viewportW, viewportH, 1.0f / viewportW, 1.0f / viewportH);
        s_FrameInfoData.extraInfo = glm::vec4(1.0f / 255.0f, 0.0f, 0.0f, 0.0f);

        void* data;
        vkMapMemory(device, s_FrameInfoUBOMemory, 0, sizeof(GaussianFrameInfo), 0, &data);
        memcpy(data, &s_FrameInfoData, sizeof(GaussianFrameInfo));
        vkUnmapMemory(device, s_FrameInfoUBOMemory);

        // Build current model transforms
        uint32_t modelCount = (uint32_t)s_RegisteredGaussianModels.size();
        std::vector<glm::mat4> modelTransforms(modelCount, glm::mat4(1.0f));
        for (const auto& cmd : s_GaussianRenderQueue)
        {
            uint32_t mIdx = cmd.Model->GetModelIndex();
            if (mIdx < modelCount)
                modelTransforms[mIdx] = cmd.Transform;
        }

        // Check if sorting is needed by comparing with cached state
        bool needsResort = !s_SortCacheValid
            || viewMatrix != s_PrevViewMatrix
            || projMatrix != s_PrevProjectionMatrix
            || modelTransforms != s_PrevModelTransforms;

        // Update Model Transform SSBO (needed even if sort is cached, for vertex shader)
        if (s_ModelTransformSSBO != VK_NULL_HANDLE)
        {
            vkMapMemory(device, s_ModelTransformSSBOMemory, 0,
                modelCount * sizeof(glm::mat4), 0, &data);
            memcpy(data, modelTransforms.data(), modelCount * sizeof(glm::mat4));
            vkUnmapMemory(device, s_ModelTransformSSBOMemory);
        }

        if (needsResort)
        {
            // === COMPUTE: GPU Sort (must be OUTSIDE render pass) ===
            SortGaussiansOnGPU();

            // Update sort cache
            s_PrevViewMatrix = viewMatrix;
            s_PrevProjectionMatrix = projMatrix;
            s_PrevModelTransforms = modelTransforms;
            s_SortCacheValid = true;
        }

        // === Barrier: ensure mesh pass depth writes are visible to gaussian pass fragment shader ===
        {
            VkMemoryBarrier depthBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
            depthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            depthBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(context->GetCurrentCommandBuffer(),
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 1, &depthBarrier, 0, nullptr, 0, nullptr);
        }

        // === GRAPHICS: Draw (inside render pass) ===
        VulkanRenderer::BeginGaussianRenderPass();

        auto splatShader = s_ShaderLibrary->Get("GaussianSplat.vert.spv");
        splatShader->Bind();

        VkCommandBuffer cmd = context->GetCurrentCommandBuffer();

        // Bind Global DescriptorSet (set=0)
        VkDescriptorSet globalDescriptorSet = context->GetCurrentDescriptorSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            splatShader->GetPipelineLayout(), 0, 1, &globalDescriptorSet, 0, nullptr);

        // Bind GaussianSplat DescriptorSet (set=1)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            splatShader->GetPipelineLayout(), 1, 1, &s_GaussianSplatDescriptorSet, 0, nullptr);

        // Draw instanced quads
        vkCmdDraw(cmd, 6, s_TotalGaussianCount, 0, 0);

        // Note: EndRenderPass is NOT called here.
        // ImGui will be drawn in the same render pass by PresentFrame(),
        // then EndRenderPass + EndScene are called.
    }
	
	void Renderer::DrawMesh(const MeshRenderCommandRequest& request)
    {
    	switch (GetAPI())
    	{
    	case API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
    	case API::Vulkan:  VulkanRenderer::DrawMesh(request); return;
    	}
    	EG_CORE_ASSERT(false, "Unknown RendererAPI!");
    	return;
    }

	void Renderer::DrawGaussian(const GaussianRenderCommandRequest& request)
    {
    	switch (GetAPI())
    	{
    	case API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
    	case API::Vulkan:  VulkanRenderer::DrawGaussian(request); return;
    	}
    	EG_CORE_ASSERT(false, "Unknown RendererAPI!");
    	return;
    }
	
}
