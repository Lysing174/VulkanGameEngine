#include "pch.h"
#include "VulkanRenderPass.h"

namespace Engine
{
    VulkanRenderPass::VulkanRenderPass(std::string passName,VkFormat swapChainImageFormat,VkFormat depthImageFormat,std::shared_ptr<VkDevice> device,
                                       VkSampleCountFlagBits msaaSamples)
    {
        m_Name = passName;
        this->device = device;
        m_MSAASamples = msaaSamples;

        if (passName == "ImGui")
        {
            // ========================================================
            // ImGui Pass — renders UI on top of cleared swapchain
            // Single color attachment (swapchain image), CLEAR + UNDEFINED
            // Works for both offscreen (mesh → offscreen FBO) and inline
            // ========================================================
            VkAttachmentDescription colorAttachment = {};
            colorAttachment.format = swapChainImageFormat;
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentReference colorAttachmentRef = {};
            colorAttachmentRef.attachment = 0;
            colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass = {};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorAttachmentRef;

            VkSubpassDependency dependency = {};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            VkAttachmentDescription attachments[] = { colorAttachment };

            VkRenderPassCreateInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = 1;
            renderPassInfo.pAttachments = attachments;
            renderPassInfo.subpassCount = 1;
            renderPassInfo.pSubpasses = &subpass;
            renderPassInfo.dependencyCount = 1;
            renderPassInfo.pDependencies = &dependency;

            if (vkCreateRenderPass(*device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
                throw std::runtime_error("failed to create imgui render pass!");
            }
        }
        else if (passName=="Mesh")
        {
            bool useMSAA = (msaaSamples != VK_SAMPLE_COUNT_1_BIT);

            if (useMSAA)
            {
                // ========================================================
                // MSAA Path — Vulkan 1.2 core API (RenderPass2)
                // Attachments: [0]=MSAA Color, [1]=Resolve Color, [2]=MSAA Depth
                // ========================================================

                // Attachment 0: MSAA Color
                VkAttachmentDescription2 colorAttachment = {};
                colorAttachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
                colorAttachment.format = swapChainImageFormat;
                colorAttachment.samples = msaaSamples;
                colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                VkAttachmentReference2 colorAttachmentRef = {};
                colorAttachmentRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
                colorAttachmentRef.attachment = 0;
                colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                // Attachment 1: Resolve (for ImGui display)
                VkAttachmentDescription2 resolveAttachment = {};
                resolveAttachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
                resolveAttachment.format = swapChainImageFormat;
                resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
                resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                resolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                resolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                resolveAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                resolveAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkAttachmentReference2 resolveAttachmentRef = {};
                resolveAttachmentRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
                resolveAttachmentRef.attachment = 1;
                resolveAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                // Attachment 2: MSAA Depth
                VkAttachmentDescription2 depthAttachment = {};
                depthAttachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
                depthAttachment.format = depthImageFormat;
                depthAttachment.samples = msaaSamples;
                depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                VkAttachmentReference2 depthAttachmentRef = {};
                depthAttachmentRef.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
                depthAttachmentRef.attachment = 2;
                depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                VkSubpassDescription2 subpass = {};
                subpass.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
                subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                subpass.colorAttachmentCount = 1;
                subpass.pColorAttachments = &colorAttachmentRef;
                subpass.pResolveAttachments = &resolveAttachmentRef;
                subpass.pDepthStencilAttachment = &depthAttachmentRef;

                VkSubpassDependency2 dependency = {};
                dependency.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
                dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
                dependency.dstSubpass = 0;
                dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
                dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
                dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

                VkAttachmentDescription2 attachments[] = { colorAttachment, resolveAttachment, depthAttachment };

                VkRenderPassCreateInfo2 renderPassInfo = {};
                renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
                renderPassInfo.attachmentCount = 3;
                renderPassInfo.pAttachments = attachments;
                renderPassInfo.subpassCount = 1;
                renderPassInfo.pSubpasses = &subpass;
                renderPassInfo.dependencyCount = 1;
                renderPassInfo.pDependencies = &dependency;

                if (vkCreateRenderPass2(*device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
                    throw std::runtime_error("failed to create mesh render pass!");
                }
            }
            else
            {
                // ========================================================
                // Non-MSAA Path — Vulkan 1.0 API
                // Attachments: [0]=Color(SwapChain), [1]=Depth
                // ========================================================

                VkAttachmentDescription colorAttachment = {};
                colorAttachment.format = swapChainImageFormat;
                colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
                colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkAttachmentReference colorAttachmentRef = {};
                colorAttachmentRef.attachment = 0;
                colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                VkAttachmentDescription depthAttachment = {};
                depthAttachment.format = depthImageFormat;
                depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
                depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

                VkAttachmentReference depthAttachmentRef = {};
                depthAttachmentRef.attachment = 1;
                depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                VkSubpassDescription subpass = {};
                subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                subpass.colorAttachmentCount = 1;
                subpass.pColorAttachments = &colorAttachmentRef;
                subpass.pDepthStencilAttachment = &depthAttachmentRef;

                VkSubpassDependency dependency = {};
                dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
                dependency.dstSubpass = 0;
                dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
                dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
                dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

                VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };

                VkRenderPassCreateInfo renderPassInfo = {};
                renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
                renderPassInfo.attachmentCount = 2;
                renderPassInfo.pAttachments = attachments;
                renderPassInfo.subpassCount = 1;
                renderPassInfo.pSubpasses = &subpass;
                renderPassInfo.dependencyCount = 1;
                renderPassInfo.pDependencies = &dependency;

                if (vkCreateRenderPass(*device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
                    throw std::runtime_error("failed to create mesh render pass!");
                }
            }
        }
        else
            throw std::runtime_error("unknown render pass name!");
        
    }

    VulkanRenderPass::~VulkanRenderPass()
    {
    }

    void VulkanRenderPass::cleanup()
    {
        for (size_t i = 0; i < swapchainFrameBuffers.size(); i++) 
        {
            vkDestroyFramebuffer(*device, swapchainFrameBuffers[i], nullptr);
        }
        vkDestroyRenderPass(*device, renderPass, nullptr);

    }
    
    
    void VulkanRenderPass::createFramebuffers(VkExtent2D swapChainExtent, 
                                              const std::vector<VkImageView>& swapChainImageViews, 
                                              VkImageView depthImageView,
                                              VkImageView colorMSAA,
                                              VkImageView depthMSAA)
    {
        swapchainFrameBuffers.resize(swapChainImageViews.size());
        bool hasDepth = (depthImageView != VK_NULL_HANDLE);
        bool useMSAA = (colorMSAA != VK_NULL_HANDLE && depthMSAA != VK_NULL_HANDLE);

        for (size_t i = 0; i < swapChainImageViews.size(); i++) {
            std::vector<VkImageView> attachments;
            if (useMSAA) {
                // MSAA: [MSAA Color, Resolve(SwapChain), MSAA Depth] — 3 attachments
                attachments = { colorMSAA, swapChainImageViews[i], depthMSAA };
            } else if (hasDepth) {
                // No MSAA, with depth: [Color(SwapChain), Depth] — 2 attachments
                attachments = { swapChainImageViews[i], depthImageView };
            } else {
                // No MSAA, no depth : [Color(SwapChain)] — 1 attachment
                attachments = { swapChainImageViews[i] };
            }

            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapChainExtent.width;
            framebufferInfo.height = swapChainExtent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(*device, &framebufferInfo, nullptr, &swapchainFrameBuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create framebuffer!");
            }
        }
    }
    
    
    
}
