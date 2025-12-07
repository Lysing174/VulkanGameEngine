#include "pch.h"
#include "Material.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Engine/Renderer/Renderer.h"

namespace Engine {

    Material::Material(const std::shared_ptr<Shader>& shader)
        : m_Shader(shader)
    {
        //std::hash<std::string> hasher;
        m_RendererID = shader->GetRendererID();
    }

    void Material::Bind()
    {
        switch (Renderer::GetAPI())
        {
        case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
        case Renderer::API::Vulkan:  
            VkCommandBuffer cmd = VulkanContext::Get()->GetCurrentCommandBuffer();
            VkDescriptorSet currentDescriptorSet = VulkanContext::Get()->GetCurrentDescriptorSet();
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Shader->GetPipelineLayout(), 0, 1, &currentDescriptorSet, 0, nullptr);

            auto vkShader = std::static_pointer_cast<VulkanShader>(m_Shader);

            vkCmdPushConstants(cmd, vkShader->GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 64, sizeof(glm::vec4), &m_Color);
            return;
        }

        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return;


    }
    void Material::PushColor(const glm::vec4& color)
    {
        m_Color = color;
        auto vkShader = std::static_pointer_cast<VulkanShader>(m_Shader);
        VkCommandBuffer cmd = VulkanContext::Get()->GetCurrentCommandBuffer();

        vkCmdPushConstants(cmd, vkShader->GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 64, sizeof(glm::vec4), &m_Color);

    }
}