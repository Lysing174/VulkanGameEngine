#pragma once

#include "Engine/Renderer/Renderer.h"

#include <Engine/Renderer/Mesh.h>
#include <glm/glm.hpp>


namespace Engine
{
    class VulkanRenderPass {
    public:
        VulkanRenderPass(std::string passName,VkFormat swapChainImageFormat,VkFormat depthImageFormat,std::shared_ptr<VkDevice> device,
                         VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);
        ~VulkanRenderPass();
        void cleanup();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createDescriptorSets();


        void createFramebuffers(VkExtent2D swapChainExtent, 
                                const std::vector<VkImageView>& swapChainImageViews, 
                                VkImageView depthImageView,
                                VkImageView colorMSAA = VK_NULL_HANDLE,
                                VkImageView depthMSAA = VK_NULL_HANDLE);
        

        // 辅助函数：简化命令录制
        void begin(VkCommandBuffer cmd, VkFramebuffer framebuffer, VkExtent2D extent, 
                   VkClearValue* clearValues, uint32_t clearCount);
        void end(VkCommandBuffer cmd);
        VkRenderPass GetVkRenderPass() const { return renderPass; }
        VkFramebuffer GetFramebuffer(uint32_t index) const { return swapchainFrameBuffers[index]; }
        uint32_t GetFramebufferCount() const { return (uint32_t)swapchainFrameBuffers.size(); }

    private:
        std::string m_Name;
        std::shared_ptr<VkDevice> device;
        VkRenderPass renderPass;
        VkSampleCountFlagBits m_MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        std::vector<VkFramebuffer> swapchainFrameBuffers;
        
    };
}