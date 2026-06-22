#include "pch.h"
#include "Material.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Engine/Renderer/Renderer.h"

namespace Engine {

    Material::Material(const std::shared_ptr<Shader>& shader)
        : m_Shader(shader)
    {
        const ShaderConfig& config = shader->GetConfig();

        // 只在配置中存在 UBO binding 时才创建 UniformBuffer
        auto uboBindings = config.GetBindingsByType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        if (!uboBindings.empty())
        {
            VkDeviceSize bufferSize = sizeof(MaterialUniformBuffer);
            VulkanContext::Get()->CreateBuffer(
                    bufferSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    m_UniformBuffer,
                    m_UniformBufferMemory
                );
        }

        m_RendererID = shader->GetRendererID();
        AllocateDescriptorSet();

        // 根据配置初始化默认纹理 (只对存在的 texture binding 赋值)
        for (const auto& bindingInfo : config.MaterialBindings)
        {
            if (bindingInfo.Type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            {
                // 默认纹理: 白色
                m_Textures[bindingInfo.Binding] = Texture2D::GetWhiteTexture();
            }
        }

        WriteTextureDescriptors();
        WriteUniformDescriptors();

        m_UniformData = { glm::vec4(1.0f,1.0f,1.0f,1.0f),   // 基础颜色 (RGBA)

        0.0f,         // 金属度系数
        0.0f,         // 粗糙度系数
        0.0f, // 自发光强度
        0.0f,        // AO 强度

        glm::vec4(1.0f,1.0f,1.0f,1.0f), // 自发光颜色

        0,        // 布尔值 (作为int传递)
        0,
        0,
        0
        };
        UploadUniformBuffer();
    }

    Material::~Material()
    {
        auto device = VulkanContext::Get()->GetDevice();

        // 释放 UniformBuffer
        const ShaderConfig& config = m_Shader->GetConfig();
        auto uboBindings = config.GetBindingsByType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        if (!uboBindings.empty())
        {
            vkDestroyBuffer(device, m_UniformBuffer, nullptr);
            vkFreeMemory(device, m_UniformBufferMemory, nullptr);
        }

        // 释放 DescriptorSet
        if (m_DescriptorSet != VK_NULL_HANDLE)
        {
            vkFreeDescriptorSets(device, VulkanContext::Get()->GetDescriptorPool(), 1, &m_DescriptorSet);
        }
    }

    void Material::Bind()
    {
        switch (Renderer::GetAPI())
        {
        case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
        case Renderer::API::Vulkan:  
            VkCommandBuffer cmd = VulkanContext::Get()->GetCurrentCommandBuffer();
            const ShaderConfig& config = m_Shader->GetConfig();

            // 只在配置中存在 MaterialBindings 时才绑定 DescriptorSet
            if (!config.MaterialBindings.empty())
            {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Shader->GetPipelineLayout(), 1, 1, &m_DescriptorSet, 0, nullptr);
            }

            // 只在配置中存在 fragment push constant range 时才推送颜色
            for (const auto& pcRange : config.PushConstantRanges)
            {
                if (pcRange.StageFlags & VK_SHADER_STAGE_FRAGMENT_BIT && pcRange.Size == sizeof(glm::vec4))
                {
                    vkCmdPushConstants(cmd, m_Shader->GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, pcRange.Offset, sizeof(glm::vec4), &m_UniformData.AlbedoColor);
                    break;
                }
            }
            return;
        }

        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return;
    }

    void Material::PushColor(const glm::vec4& color)
    {
        m_UniformData.AlbedoColor = color;
        VkCommandBuffer cmd = VulkanContext::Get()->GetCurrentCommandBuffer();

        const ShaderConfig& config = m_Shader->GetConfig();
        for (const auto& pcRange : config.PushConstantRanges)
        {
            if (pcRange.StageFlags & VK_SHADER_STAGE_FRAGMENT_BIT && pcRange.Size == sizeof(glm::vec4))
            {
                vkCmdPushConstants(cmd, m_Shader->GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, pcRange.Offset, sizeof(glm::vec4), &m_UniformData.AlbedoColor);
                break;
            }
        }
    }

    void Material::AllocateDescriptorSet()
    {
        auto vkShader = std::static_pointer_cast<VulkanShader>(m_Shader);
        auto device = VulkanContext::Get()->GetDevice();

        VkDescriptorSetLayout layout = vkShader->GetMaterialDescriptorSetLayout();

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = VulkanContext::Get()->GetDescriptorPool();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        if (vkAllocateDescriptorSets(device, &allocInfo, &m_DescriptorSet) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate material descriptor set!");
        }
    }

    void Material::UploadUniformBuffer()
    {
        if (m_BatchUpdate || !m_UniformBuffer)
            return;

        void* data;
        vkMapMemory(VulkanContext::Get()->GetDevice(), m_UniformBufferMemory, 0, sizeof(m_UniformData), 0, &data);
        memcpy(data, &m_UniformData, sizeof(m_UniformData));
        vkUnmapMemory(VulkanContext::Get()->GetDevice(), m_UniformBufferMemory);
    }

    void Material::BeginBatchUpdate()
    {
        m_BatchUpdate = true;
    }

    void Material::EndBatchUpdate()
    {
        m_BatchUpdate = false;
        UploadUniformBuffer();
    }

    void Material::SetTexture(const std::string& textureTypeName, const std::shared_ptr<Texture2D> texture)
    {
        uint32_t binding = m_Shader->GetTextureBinding(textureTypeName);

        if (binding == (uint32_t)-1)
        {
            EG_CORE_ERROR("Can't find texture binding index for: {0}", textureTypeName);
            return;
        }

        m_Textures[binding] = texture;
        WriteTextureDescriptor(binding, texture);
    }

    void Material::WriteTextureDescriptor(uint32_t binding, const std::shared_ptr<Texture2D>& texture)
    {
        auto device = VulkanContext::Get()->GetDevice();

        // 确认配置中该 binding 确实存在
        const ShaderConfig& config = m_Shader->GetConfig();
        if (!config.HasBinding(binding))
        {
            EG_CORE_WARN("Material::WriteTextureDescriptor - Binding {0} not in shader config, skipping", binding);
            return;
        }

        auto vkTexture = std::static_pointer_cast<VulkanTexture2D>(texture);
        VkDescriptorImageInfo imageInfo = vkTexture->GetDescriptorImageInfo();

        VkWriteDescriptorSet descriptorWrite = {};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_DescriptorSet;
        descriptorWrite.dstBinding = binding;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }

    void Material::WriteTextureDescriptors()
    {
        auto device = VulkanContext::Get()->GetDevice();
        const ShaderConfig& config = m_Shader->GetConfig();

        std::vector<VkWriteDescriptorSet> descriptorWrites;
        descriptorWrites.reserve(m_Textures.size());

        std::vector<VkDescriptorImageInfo> imageInfos;
        imageInfos.reserve(m_Textures.size());

        for (auto& [binding, texture] : m_Textures)
        {
            if (!texture) continue;

            // 跳过配置中不存在的 binding
            if (!config.HasBinding(binding)) continue;

            auto vkTexture = std::static_pointer_cast<VulkanTexture2D>(texture);
            imageInfos.push_back(vkTexture->GetDescriptorImageInfo());

            VkWriteDescriptorSet write = {};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = m_DescriptorSet;
            write.dstBinding = binding;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfos.back();

            descriptorWrites.push_back(write);
        }

        if (!descriptorWrites.empty())
        {
            vkUpdateDescriptorSets(
                device,
                static_cast<uint32_t>(descriptorWrites.size()),
                descriptorWrites.data(),
                0,
                nullptr
            );
        }
    }

    void Material::WriteUniformDescriptors()
    {
        const ShaderConfig& config = m_Shader->GetConfig();

        // 只写入配置中存在的 UBO binding
        auto uboBindings = config.GetBindingsByType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        if (uboBindings.empty() || !m_UniformBuffer) return;

        auto device = VulkanContext::Get()->GetDevice();

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_UniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(MaterialUniformBuffer);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_DescriptorSet;
        descriptorWrite.dstBinding = uboBindings[0]; // 第一个 UBO binding
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }

    void Material::SetAlbedoColor(const glm::vec4& color)
    {
        m_UniformData.AlbedoColor = color;
        UploadUniformBuffer();
    }

    void Material::SetMetalness(float value)
    {
        if (m_UniformData.Metalness != value)
        {
            m_UniformData.Metalness = value;
            UploadUniformBuffer();
        }
    }

    void Material::SetRoughness(float value)
    {
        if (m_UniformData.Roughness != value)
        {
            m_UniformData.Roughness = value;
            UploadUniformBuffer();
        }
    }

    void Material::SetEmissive(float intensity, const glm::vec3& color)
    {
        bool changed = false;
        if (m_UniformData.EmissiveIntensity != intensity)
        {
            m_UniformData.EmissiveIntensity = intensity;
            changed = true;
        }

        if (glm::vec3(m_UniformData.EmissiveColor) != color)
        {
            m_UniformData.EmissiveColor = glm::vec4(color, 1.0f);
            changed = true;
        }

        if (changed) UploadUniformBuffer();
    }

    void Material::SetAOStrength(float value)
    {
        if (m_UniformData.AOStrength != value)
        {
            m_UniformData.AOStrength = value;
            UploadUniformBuffer();
        }
    }

    void Material::SetUseNormalMap(bool use)
    {
        int useInt = use ? 1 : 0;
        if (m_UniformData.HasNormalMap != useInt)
        {
            m_UniformData.HasNormalMap = useInt;
            UploadUniformBuffer();
        }
    }

    void Material::SetHasAlbedoMap(bool has)
    {
        int hasInt = has ? 1 : 0;
        if (m_UniformData.HasAlbedoMap != hasInt)
        {
            m_UniformData.HasAlbedoMap = hasInt;
            UploadUniformBuffer();
        }
    }

    void Material::SetHasMetalRoughnessMap(bool has)
    {
        int hasInt = has ? 1 : 0;
        if (m_UniformData.HasMetalRoughnessMap != hasInt)
        {
            m_UniformData.HasMetalRoughnessMap = hasInt;
            UploadUniformBuffer();
        }
    }
    
    void Material::SetHasEmissiveMap(bool has)
    {
        int hasInt = has ? 1 : 0;
        if (m_UniformData.HasEmissiveMap != hasInt)
        {
            m_UniformData.HasEmissiveMap = hasInt;
            UploadUniformBuffer();
        }
    }
}
