#pragma once

#include "Engine/Renderer/Renderer.h"

#include <Engine/Renderer/Mesh.h>
#include <glm/glm.hpp>


namespace Engine
{
    class VulkanRenderPass {
    public:
        VulkanRenderPass(std::string passName,VkFormat colorFormat,VkFormat depthImageFormat,std::shared_ptr<VkDevice> device,
                         VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);
        ~VulkanRenderPass();
        void cleanup();

        // 辅助函数：简化命令录制
        void begin(VkCommandBuffer cmd, VkFramebuffer framebuffer, VkExtent2D extent, 
                   VkClearValue* clearValues, uint32_t clearCount);
        void end(VkCommandBuffer cmd);
        VkRenderPass GetVkRenderPass() const { return renderPass; }

    private:
        std::string m_Name;
        std::shared_ptr<VkDevice> device;
        VkRenderPass renderPass;
        VkSampleCountFlagBits m_MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        
    };
}
