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
        VkDeviceSize bufferSize = sizeof(MaterialUniformBuffer);
        VulkanContext::Get()->CreateBuffer(
                bufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                m_UniformBuffer,
                m_UniformBufferMemory
            );

        //std::hash<std::string> hasher;
        m_RendererID = shader->GetRendererID();
        AllocateDescriptorSet();
        m_Textures[0] = Texture2D::GetWhiteTexture();
        m_Textures[1] = Texture2D::GetBlueTexture();
        m_Textures[2] = Texture2D::GetWhiteTexture();
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
        //vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        //for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        //    vkDestroyBuffer(device, uniformBuffers[i], nullptr);
        //    vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
        //}

    }

    void Material::Bind()
    {
        switch (Renderer::GetAPI())
        {
        case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
        case Renderer::API::Vulkan:  
            VkCommandBuffer cmd = VulkanContext::Get()->GetCurrentCommandBuffer();
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Shader->GetPipelineLayout(), 1, 1,&m_DescriptorSet, 0, nullptr);
            auto vkShader = std::static_pointer_cast<VulkanShader>(m_Shader);

            vkCmdPushConstants(cmd, vkShader->GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 64, sizeof(glm::vec4), &m_UniformData.AlbedoColor);
            return;
        }

        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return;
    }
    void Material::PushColor(const glm::vec4& color)
    {
        m_UniformData.AlbedoColor = color;
        auto vkShader = std::static_pointer_cast<VulkanShader>(m_Shader);
        VkCommandBuffer cmd = VulkanContext::Get()->GetCurrentCommandBuffer();

        vkCmdPushConstants(cmd, vkShader->GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 64, sizeof(glm::vec4), &m_UniformData.AlbedoColor);

    }
    void Material::AllocateDescriptorSet()
    {
        auto vkShader = std::static_pointer_cast<VulkanShader>(m_Shader);
        auto device = VulkanContext::Get()->GetDevice();

        // 1. 获取 Shader 的 Layout
        VkDescriptorSetLayout layout = vkShader->GetMaterialDescriptorSetLayout();

        // 2. 配置分配信息
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        // 假设 VulkanContext 里有一个全局的 DescriptorPool (通常要有)
        allocInfo.descriptorPool = VulkanContext::Get()->GetDescriptorPool();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        // 3. 分配 Set
        if (vkAllocateDescriptorSets(device, &allocInfo, &m_DescriptorSet) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate material descriptor set!");
        }

    }

    void Material::UploadUniformBuffer()
    {
        // 只有当 Buffer 存在时才上传
        if (m_UniformBuffer)
        {
            void* data;
            vkMapMemory(VulkanContext::Get()->GetDevice(), m_UniformBufferMemory, 0, sizeof(m_UniformData), 0, &data);
            memcpy(data, &m_UniformData, sizeof(m_UniformData));
            vkUnmapMemory(VulkanContext::Get()->GetDevice(), m_UniformBufferMemory);

        }
    }
    void Material::SetTexture(const std::string& name, const std::shared_ptr<Texture2D> texture)
    {
        auto vkShader = std::static_pointer_cast<VulkanShader>(m_Shader);
        uint32_t binding = vkShader->GetBinding(name);

        if (binding == -1)
        {
            EG_CORE_ERROR("Can't find texture binding index!");
            return;
        }

        m_Textures[binding] = texture;

        WriteTextureDescriptor(binding, texture);
    }

    void Material::WriteTextureDescriptor(uint32_t binding, const std::shared_ptr<Texture2D>& texture)
    {
        auto device = VulkanContext::Get()->GetDevice();

        VkWriteDescriptorSet descriptorWrite = {};

        auto vkTexture = std::static_pointer_cast<VulkanTexture2D>(texture);
        VkDescriptorImageInfo imageInfo = vkTexture->GetDescriptorImageInfo();

        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_DescriptorSet;   // 写入哪个 Set
        descriptorWrite.dstBinding = binding;       // 写入哪个 Binding (0, 1, 2...)
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    }
    void Material::WriteTextureDescriptors()
    {
        auto device = VulkanContext::Get()->GetDevice();

        std::vector<VkWriteDescriptorSet> descriptorWrites;
        descriptorWrites.reserve(m_Textures.size());

        std::vector<VkDescriptorImageInfo> imageInfos;
        imageInfos.reserve(m_Textures.size());

        for (auto& [binding, texture] : m_Textures)
        {
            if (!texture) continue;

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
        vkUpdateDescriptorSets(
            device,
            static_cast<uint32_t>(descriptorWrites.size()),
            descriptorWrites.data(),
            0,
            nullptr
        );
    }
    void Material::WriteUniformDescriptors()
    {
        auto device = VulkanContext::Get()->GetDevice();

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_UniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(MaterialUniformBuffer);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_DescriptorSet;
        descriptorWrite.dstBinding = 3; // 你的 UBO Binding
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
        if (m_UniformData.UseNormalMap != useInt)
        {
            m_UniformData.UseNormalMap = useInt;
            UploadUniformBuffer();
        }
    }
}