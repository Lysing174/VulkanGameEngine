#include "pch.h"
#include "Material.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanShader.h"

namespace Engine {

    Material::Material(const std::shared_ptr<Shader>& shader)
        : m_Shader(shader)
    {
    }

    void Material::Bind()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
        case RendererAPI::API::Vulkan:  
            m_Shader->Bind();

            auto vkShader = std::static_pointer_cast<VulkanShader>(m_Shader);
            VkCommandBuffer cmd = VulkanContext::Get()->GetCurrentCommandBuffer();

            vkCmdPushConstants(cmd, vkShader->GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4), &m_Color);
            return;
        }

        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return;


    }
}