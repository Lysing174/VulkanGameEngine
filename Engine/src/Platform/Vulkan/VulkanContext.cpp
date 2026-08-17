#include "pch.h"
#include "VulkanContext.h"
#pragma once
#include "Engine/Renderer/GraphicsContext.h"
#include "Engine/Renderer/Renderer.h"
#include "backends/imgui_impl_vulkan.h"
#include "Platform/Vulkan/VulkanShader.h"


#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include <vector>
#include <set>
#include <unordered_map>
#include <array>
#include <iostream>
#include <stdexcept>
#include <functional>
#include <fstream>
#include <chrono>


namespace Engine {
    VulkanContext* VulkanContext::s_Instance = nullptr;

    VulkanContext::VulkanContext(GLFWwindow* windowHandle)
    {
        EG_CORE_ASSERT(!s_Instance, "VulkanContext already exists!");
        s_Instance = this;

        window = windowHandle;
    }
    VulkanContext::~VulkanContext()
    {
        cleanup();
        s_Instance = nullptr;
    }

    void VulkanContext::Init()
    {
        initVulkan();
    }
    void VulkanContext::BeginFrame(glm::mat4 projView)
    {
        vkQueueWaitIdle(presentQueue);
        vkAcquireNextImageKHR(device, swapChain, std::numeric_limits<uint64_t>::max(), imageAvailableSemaphore, VK_NULL_HANDLE, &currentImageIndex);

        updateGlobalUniforms(projView);

        vkResetCommandBuffer(commandBuffers[currentImageIndex], 0);//清空命令

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        beginInfo.pInheritanceInfo = nullptr; // Optional

        //开始录制命令
        vkBeginCommandBuffer(commandBuffers[currentImageIndex], &beginInfo);

    }

    void VulkanContext::BeginMeshRenderPass(std::shared_ptr<VulkanFramebuffer> offscreenFB)
    {
        // ========================================
        // Mesh RenderPass: 清屏颜色+深度, 写入颜色和深度
        // ========================================
        currentRenderPassName = "Mesh";

        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        int x, y;
        glfwGetWindowPos(window, &x, &y);
        renderPassInfo.renderArea.offset = { 0,0 };

        // ========================================
        // Offscreen path: render to custom FBO
        // ========================================

        // Transition resolve/color image from SHADER_READ_ONLY_OPTIMAL
        // (after previous frame's render pass) back to COLOR_ATTACHMENT_OPTIMAL
        {
            VkImageMemoryBarrier barrier = {};
            barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.newLayout                       = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                           = offscreenFB->GetResolveImage();
            barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;
            barrier.srcAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask                   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            vkCmdPipelineBarrier(commandBuffers[currentImageIndex],
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        renderPassInfo.renderPass = offscreenFB->GetRenderPass();
        renderPassInfo.framebuffer = offscreenFB->GetVulkanFramebuffer();
        renderPassInfo.renderArea.extent = offscreenFB->GetExtent();

        if (offscreenFB->IsMSAA())
        {
            // MSAA: [MSAA Color, Resolve Color, MSAA Depth] — 3 clear values
            VkClearValue clearValues[3] = {};
            clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };   // MSAA Color
            clearValues[1].color = { 0.0f, 0.0f, 0.0f, 1.0f };   // Resolve (ignored, DONT_CARE)
            clearValues[2].depthStencil = { 1.0f, 0 };             // MSAA Depth
            renderPassInfo.clearValueCount = 3;
            renderPassInfo.pClearValues = clearValues;
        }
        else
        {
            VkClearValue clearValues[2] = {};
            clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
            clearValues[1].depthStencil = { 1.0f, 0 };
            renderPassInfo.clearValueCount = 2;
            renderPassInfo.pClearValues = clearValues;
        }

        vkCmdBeginRenderPass(commandBuffers[currentImageIndex], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Set viewport and scissor to offscreen FBO size
        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)offscreenFB->GetWidth();
        viewport.height = (float)offscreenFB->GetHeight();
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffers[currentImageIndex], 0, 1, &viewport);

        VkRect2D scissor = {};
        scissor.offset = { 0, 0 };
        scissor.extent = offscreenFB->GetExtent();
        vkCmdSetScissor(commandBuffers[currentImageIndex], 0, 1, &scissor);
        
    }

    void VulkanContext::BeginUIRenderPass()
    {
        // ========================================
        // ImGui RenderPass: CLEAR swapchain, draw UI on top
        // ========================================
        currentRenderPassName = "ImGui";

        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = GetCurrentRenderPass()->GetVkRenderPass();
        
        renderPassInfo.framebuffer = GetCurrentRenderPass()->GetFramebuffer(currentImageIndex);
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = swapChainExtent;

        // CLEAR_OP_CLEAR: clear to black
        VkClearValue clearValue = {};
        clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f };
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(commandBuffers[currentImageIndex], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    }
    
    void VulkanContext::EndRenderPass()
    {
        VkCommandBuffer cmd = commandBuffers[currentImageIndex];

        // 结束 Mesh RenderPass
        vkCmdEndRenderPass(cmd);
    }
    void VulkanContext::DrawImGui()
    {
        // ImGui 绘制在当前 RenderPass 内
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffers[currentImageIndex]);

    }
    void VulkanContext::EndFrame()
    {
        if (vkEndCommandBuffer(commandBuffers[currentImageIndex]) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer!");
        }

        // ========================================
        // 一次性提交! 一个 command buffer 包含了:
        //   Mesh RenderPass    → 画 mesh (写颜色+深度)
        //   ImGui              → UI 叠加
        // 全部录制完毕后才提交, GPU 按顺序执行
        // ========================================
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[currentImageIndex];

        VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
            throw std::runtime_error("failed to submit draw command buffer!");
        }

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        VkSwapchainKHR swapChains[] = { swapChain };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &currentImageIndex;
        presentInfo.pResults = nullptr; // Optional

        vkQueuePresentKHR(presentQueue, &presentInfo);

        // 重置标记
        currentRenderPassName = "";
    }

    void VulkanContext::initVulkan() {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        depthImageFormat = findDepthFormat();
        createImageViews();

        // Clamp MSAA to device maximum
        VkSampleCountFlagBits maxSamples = GetMaxUsableSampleCount();
        if (m_MSAASamples > maxSamples) m_MSAASamples = maxSamples;

        //createMeshRenderPass();
        renderPassesMap["Mesh"]=std::make_shared<VulkanRenderPass>("Mesh",swapChainImageFormat,depthImageFormat,std::make_shared<VkDevice>(device), m_MSAASamples);
        renderPassesMap["ImGui"]=std::make_shared<VulkanRenderPass>("ImGui",swapChainImageFormat,depthImageFormat,std::make_shared<VkDevice>(device), VK_SAMPLE_COUNT_1_BIT);
                
        createDescriptorSetLayout();
        createCommandPool();
        createDepthResources();
        createMSAAResources();
        
        // ImGui pass: 只需要 swapchain image，不需要 depth
        renderPassesMap["ImGui"]->createFramebuffers(swapChainExtent, swapChainImageViews, VK_NULL_HANDLE);
        createUniformBuffer();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSemaphores();
    }

    void VulkanContext::cleanupSwapChain() {
        for (auto& renderPass : renderPassesMap) {
            renderPass.second->cleanup();
        }

        vkDeviceWaitIdle(device);

        // Destroy light SSBO
        m_LightSSBO.Destroy();
        
        // Destroy IBL SH buffer
        m_IBL_SHBuffer.Destroy();
        
        // Destroy MSAA resources
        if (m_ColorImageViewMSAA != VK_NULL_HANDLE) { vkDestroyImageView(device, m_ColorImageViewMSAA, nullptr); m_ColorImageViewMSAA = VK_NULL_HANDLE; }
        if (m_ColorImageMSAA != VK_NULL_HANDLE)      { vkDestroyImage(device, m_ColorImageMSAA, nullptr); m_ColorImageMSAA = VK_NULL_HANDLE; }
        if (m_ColorImageMemoryMSAA != VK_NULL_HANDLE) { vkFreeMemory(device, m_ColorImageMemoryMSAA, nullptr); m_ColorImageMemoryMSAA = VK_NULL_HANDLE; }
        if (m_DepthImageViewMSAA != VK_NULL_HANDLE)   { vkDestroyImageView(device, m_DepthImageViewMSAA, nullptr); m_DepthImageViewMSAA = VK_NULL_HANDLE; }
        if (m_DepthImageMSAA != VK_NULL_HANDLE)        { vkDestroyImage(device, m_DepthImageMSAA, nullptr); m_DepthImageMSAA = VK_NULL_HANDLE; }
        if (m_DepthImageMemoryMSAA != VK_NULL_HANDLE)  { vkFreeMemory(device, m_DepthImageMemoryMSAA, nullptr); m_DepthImageMemoryMSAA = VK_NULL_HANDLE; }
        
        vkDestroyImageView(device, depthImageView, nullptr);
        vkDestroyImage(device, depthImage, nullptr);
        vkFreeMemory(device, depthImageMemory, nullptr);
        
        vkFreeCommandBuffers(device, commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
        for (size_t i = 0; i < swapChainImageViews.size(); i++) {
            vkDestroyImageView(device, swapChainImageViews[i], nullptr);
        }
        vkDestroySwapchainKHR(device, swapChain, nullptr);
    }

    void VulkanContext::recreateSwapChain() {
        vkDeviceWaitIdle(device);

        cleanupSwapChain();

        createSwapChain();
        createImageViews();
        //createMeshRenderPass();
        renderPassesMap["Mesh"]=std::make_shared<VulkanRenderPass>("Mesh",swapChainImageFormat,depthImageFormat,std::make_shared<VkDevice>(device), m_MSAASamples);
        renderPassesMap["ImGui"]=std::make_shared<VulkanRenderPass>("ImGui",swapChainImageFormat,depthImageFormat,std::make_shared<VkDevice>(device), VK_SAMPLE_COUNT_1_BIT);
        
        // Recreate ImGui main pipeline with the new render pass (old one was destroyed)
        {
            ImGui_ImplVulkan_PipelineInfo pipelineInfo = {};
            pipelineInfo.RenderPass = renderPassesMap["ImGui"]->GetVkRenderPass();
            pipelineInfo.Subpass = 0;
            pipelineInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            ImGui_ImplVulkan_CreateMainPipeline(&pipelineInfo);
        }
        
        createDepthResources();
        createMSAAResources();

        if (m_MSAASamples != VK_SAMPLE_COUNT_1_BIT)
            renderPassesMap["Mesh"]->createFramebuffers(swapChainExtent, swapChainImageViews, depthImageView,
                                                        m_ColorImageViewMSAA, m_DepthImageViewMSAA);
        else
            renderPassesMap["Mesh"]->createFramebuffers(swapChainExtent, swapChainImageViews, depthImageView);
        // ImGui pass: 只需要 swapchain image，不需要 depth
        renderPassesMap["ImGui"]->createFramebuffers(swapChainExtent, swapChainImageViews, VK_NULL_HANDLE);
        createCommandBuffers();

        // 通知 Renderer 更新深度纹理 descriptor
        Renderer::OnSwapchainRecreated();
    }

    void VulkanContext::cleanup() {
        vkDeviceWaitIdle(device);
        cleanupSwapChain();
        
        uniformBuffers.clear();
        vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
        vkDestroyCommandPool(device, commandPool, nullptr);
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        vkDestroyDevice(device, nullptr);
        if (enableValidationLayers) {
            DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        }
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);

        glfwDestroyWindow(window);

        glfwTerminate();
    }

    /// <summary>
    /// 创建vk实例
    /// </summary>
    void VulkanContext::createInstance()
    {
        //检查要开启的层是否支持
        if (enableValidationLayers && !checkValidationLayerSupport()) {
            throw std::runtime_error("validation layers requested, but not available!");
        }

        //APP信息
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; appInfo.pNext = nullptr;
        appInfo.pApplicationName = "Hello Triangle";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        //创建实例所需信息
        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;//填写app信息

        auto extensions = getRequiredExtensions();//获取所需扩展信息
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());//填写扩展数量信息
        createInfo.ppEnabledExtensionNames = extensions.data();//填写扩展名信息

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());//填写层数量信息
            createInfo.ppEnabledLayerNames = validationLayers.data();//填写层名信息

            populateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;//连接下一个信息结构体
        }
        else {
            createInfo.enabledLayerCount = 0;

            createInfo.pNext = nullptr;
        }

        //创建vk实例
        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error("failed to create instance!");
        }
        std::cout << "\033[33mvulkan instance created.\033[0m" << std::endl;
    }

    /// <summary>
/// 获取需要的扩展列表
/// </summary>
/// <returns></returns>
    std::vector<const char*> VulkanContext::getRequiredExtensions() {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);//glfw扩展

        if (enableValidationLayers) {//若开启验证层则添加debug扩展
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    /// <summary>
    /// 启动编译信息发送器
    /// </summary>
    void VulkanContext::setupDebugMessenger()
    {
        if (!enableValidationLayers) return;

        VkDebugUtilsMessengerCreateInfoEXT createInfo;
        populateDebugMessengerCreateInfo(createInfo);

        if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("failed to set up debug messenger!");
        }
        std::cout << "\033[33mdebug messenger seted up.\033[0m" << std::endl;
    }

    /// <summary>
    /// 检查所需层（validationLayers）是否支持
    /// </summary>
    /// <returns>返回是否支持</returns>
    bool VulkanContext::checkValidationLayerSupport() {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char* layerName : validationLayers) {
            bool layerFound = false;

            for (const auto& layerProperties : availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound) {
                return false;
            }
        }

        return true;
    }
    /// <summary>
    /// 填充编译信息发送器的创建结构体
    /// </summary>
    void VulkanContext::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
    {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

        return VK_FALSE;
    }
    /// <summary>
    /// 创建surface
    /// </summary>
    void VulkanContext::createSurface() {
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
            throw std::runtime_error("failed to create window surface!");
        }
    }
    /// <summary>
    /// 选择物理设备
    /// </summary>
    void VulkanContext::pickPhysicalDevice() {
        //获取设备列表
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (deviceCount == 0) {
            throw std::runtime_error("failed to find GPUs with Vulkan support!");
        }
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        //检查有无合适设备，只要一个设备
        for (const auto& device : devices) {
            if (isDeviceSuitable(device)) {
                physicalDevice = device;
                break;
            }
        }
        //没查到合适设备
        if (physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
        std::cout << "\033[33mphysical devices picked.\033[0m" << std::endl;

    }

    /// <summary>
    /// 检查物理设备是否合适
    /// </summary>
    /// <param name="device"></param>
    /// <returns></returns>
    bool VulkanContext::isDeviceSuitable(VkPhysicalDevice device) {
        /*
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);
        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
        */
        QueueFamilyIndices indices = findQueueFamilies(device);
        bool extensionsSupported = checkDeviceExtensionSupport(device);
        bool swapChainAdequate = false;
        if (extensionsSupported) {
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }

        VkPhysicalDeviceFeatures supportedFeatures;
        vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

        return indices.isComplete() && extensionsSupported && supportedFeatures.samplerAnisotropy && swapChainAdequate;
    }

    /// <summary>
    /// 查找命令队列簇
    /// </summary>
    /// <param name="device"></param>
    /// <returns></returns>
    VulkanContext::QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice device) {
        QueueFamilyIndices indices;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        //查找队列指令容量大于0，且queueFlags且VK_QUEUE_GRAPHICS_BIT的队列簇
        int i = 0;
        VkBool32 presentSupport = false;
        for (const auto& queueFamily : queueFamilies) {

            if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {//查找图形队列
                indices.graphicsFamily = i;
            }

            presentSupport = false;//查找支持surface present的队列
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if (queueFamily.queueCount > 0 && presentSupport) {
                indices.presentFamily = i;
            }

            if (indices.isComplete()) {//找齐了就退出
                break;
            }
            i++;
        }
        return indices;
    }
    /// <summary>
    /// 检查物理设备扩展是否支持（创建物理设备时）
    /// </summary>
    /// <param name="device"></param>
    /// <returns></returns>
    bool VulkanContext::checkDeviceExtensionSupport(VkPhysicalDevice device) {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

        for (const auto& extension : availableExtensions) {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }
    /// <summary>
    /// 查询交换链是否支持（创建物理设备时检查）
    /// </summary>
    /// <param name="device"></param>
    /// <returns></returns>
    VulkanContext::SwapChainSupportDetails VulkanContext::querySwapChainSupport(VkPhysicalDevice device) {
        SwapChainSupportDetails details;

        //填写capabilities
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

        //填写formats
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
        }

        //填写presentModes
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }
    /// <summary>
    /// 创建逻辑设备
    /// </summary>
    void VulkanContext::createLogicalDevice() {
        //创建队列簇信息（多个队列，比如graphic队列，present队列，故用set定义，用for填充）
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<int> uniqueQueueFamilies = { indices.graphicsFamily, indices.presentFamily };
        float queuePriority = 1.0f;
        for (int queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo = {};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        //创建物理设备特征集
        VkPhysicalDeviceFeatures deviceFeatures = {};
        deviceFeatures.samplerAnisotropy = VK_TRUE;

        //创建逻辑设备信息
        VkDeviceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.queueCreateInfoCount = 1;
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        if (enableValidationLayers) {//开启验证层
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        }
        else {
            createInfo.enabledLayerCount = 0;
        }

        //创建逻辑设备
        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
            throw std::runtime_error("failed to create logical device!");
        }
        std::cout << "\033[33mlogical devices created.\033[0m" << std::endl;
        vkGetDeviceQueue(device, indices.graphicsFamily, 0, &graphicsQueue);
        vkGetDeviceQueue(device, indices.presentFamily, 0, &presentQueue);

    }

    /// <summary>
    /// 创建交换链
    /// </summary>
    void VulkanContext::createSwapChain() {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        uint32_t queueFamilyIndices[] = { (uint32_t)indices.graphicsFamily, (uint32_t)indices.presentFamily };
        if (indices.graphicsFamily != indices.presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0; // Optional
            createInfo.pQueueFamilyIndices = nullptr; // Optional
        }

        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
            throw std::runtime_error("failed to create swap chain!");
        }

        std::cout << "\033[33mswap chain created.\033[0m" << std::endl;

        vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
        swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());
        swapChainImageFormat = surfaceFormat.format;
        swapChainExtent = extent;

    }
    /// <summary>
    /// 选择surface最合适的formats
    /// </summary>
    /// <param name="availableFormats"></param>
    /// <returns></returns>
    VkSurfaceFormatKHR VulkanContext::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
        //surface无格式偏好，可直接用
        if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED) {
            return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        }
        //否则选出符合要求的
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }
        //若无符合要求的则返回第一个格式
        return availableFormats[0];
    }
    /// <summary>
    /// 选择surface最合适的presentMode
    /// </summary>
    /// <param name="availablePresentModes"></param>
    /// <returns></returns>
    VkPresentModeKHR VulkanContext::chooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes) {
        VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;

        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
            else if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                bestMode = availablePresentMode;
            }
        }

        return bestMode;
    }
    /// <summary>
    /// 选择surface最合适的swapExtent
    /// </summary>
    /// <param name="capabilities"></param>
    /// <returns></returns>
    VkExtent2D VulkanContext::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        else {
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            //glfwGetWindowSize(window, &width, &height);

            VkExtent2D actualExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };

            actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
            actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

            return actualExtent;
        }
    }

    void VulkanContext::createImageViews() {
        swapChainImageViews.resize(swapChainImages.size());

        for (uint32_t i = 0; i < swapChainImages.size(); i++) {
            VkImageViewCreateInfo viewInfo = {};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = swapChainImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = swapChainImageFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device, &viewInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS)
                throw std::runtime_error("failed to create swapchain image views!");
        }

        std::cout << "\033[33mimage views created.\033[0m" << std::endl;
    }


    void VulkanContext::createCommandPool() {
        QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create command pool!");
        }

    }

    
    bool VulkanContext::hasStencilComponent(VkFormat format) {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    void VulkanContext::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;

        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;

        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = {
            width,
            height,
            1
        };

        VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

        vkCmdCopyBufferToImage(
            commandBuffer,
            buffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region
        );

        EndSingleTimeCommands(commandBuffer);
    }
    void VulkanContext::createDepthResources() {
        VkFormat depthFormat = findDepthFormat();

        // --- Create depth image ---
        {
            VkImageCreateInfo imageInfo = {};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent = { swapChainExtent.width, swapChainExtent.height, 1 };
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = depthFormat;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateImage(device, &imageInfo, nullptr, &depthImage) != VK_SUCCESS)
                throw std::runtime_error("failed to create depth image!");

            VkMemoryRequirements memReq;
            vkGetImageMemoryRequirements(device, depthImage, &memReq);

            VkMemoryAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReq.size;
            allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            if (vkAllocateMemory(device, &allocInfo, nullptr, &depthImageMemory) != VK_SUCCESS)
                throw std::runtime_error("failed to allocate depth image memory!");

            vkBindImageMemory(device, depthImage, depthImageMemory, 0);
        }

        // --- Create depth image view ---
        {
            VkImageViewCreateInfo viewInfo = {};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = depthImage;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = depthFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device, &viewInfo, nullptr, &depthImageView) != VK_SUCCESS)
                throw std::runtime_error("failed to create depth image view!");
        }

        // --- Transition layout ---
        {
            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = depthImage;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (hasStencilComponent(depthFormat))
                barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            VkCommandBuffer cmd = BeginSingleTimeCommands();
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
            EndSingleTimeCommands(cmd);
        }
    }
    VkFormat VulkanContext::findDepthFormat() {
        return findSupportedFormat(
            { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
        );
    }

    VkSampleCountFlagBits VulkanContext::GetMaxUsableSampleCount() {
        VkPhysicalDeviceProperties physicalDeviceProperties;
        vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

        VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts &
                                     physicalDeviceProperties.limits.framebufferDepthSampleCounts;

        if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
        if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
        if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
        if (counts & VK_SAMPLE_COUNT_8_BIT)  { return VK_SAMPLE_COUNT_8_BIT;  }
        if (counts & VK_SAMPLE_COUNT_4_BIT)  { return VK_SAMPLE_COUNT_4_BIT;  }
        if (counts & VK_SAMPLE_COUNT_2_BIT)  { return VK_SAMPLE_COUNT_2_BIT;  }
        return VK_SAMPLE_COUNT_1_BIT;
    }

    void VulkanContext::createMSAAResources() {
        VkSampleCountFlagBits maxSamples = GetMaxUsableSampleCount();
        if (m_MSAASamples > maxSamples) {
            EG_CORE_WARN("Requested MSAA x{0} exceeds device max x{1}, clamping", (int)m_MSAASamples, (int)maxSamples);
            m_MSAASamples = maxSamples;
        }
        if (m_MSAASamples == VK_SAMPLE_COUNT_1_BIT) {
            return; // MSAA disabled
        }

        auto createImage = [&](VkImageUsageFlags usage, VkSampleCountFlagBits samples,
            VkFormat format, VkImage& image, VkDeviceMemory& memory)
        {
            VkImageCreateInfo imageInfo = {};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent = { swapChainExtent.width, swapChainExtent.height, 1 };
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = format;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = usage;
            imageInfo.samples = samples;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS)
                throw std::runtime_error("failed to create MSAA image!");

            VkMemoryRequirements memReq;
            vkGetImageMemoryRequirements(device, image, &memReq);

            VkMemoryAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReq.size;
            allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
                throw std::runtime_error("failed to allocate MSAA image memory!");

            vkBindImageMemory(device, image, memory, 0);
        };

        auto createView = [&](VkImage image, VkFormat format, VkImageAspectFlags aspect)
        {
            VkImageViewCreateInfo viewInfo = {};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format;
            viewInfo.subresourceRange.aspectMask = aspect;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            VkImageView imageView;
            if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
                throw std::runtime_error("failed to create MSAA image view!");

            return imageView;
        };

        // MSAA Color Image
        createImage(VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            m_MSAASamples, swapChainImageFormat, m_ColorImageMSAA, m_ColorImageMemoryMSAA);
        m_ColorImageViewMSAA = createView(m_ColorImageMSAA, swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        // MSAA Depth Image
        VkFormat depthFormat = findDepthFormat();
        createImage(VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            m_MSAASamples, depthFormat, m_DepthImageMSAA, m_DepthImageMemoryMSAA);
        m_DepthImageViewMSAA = createView(m_DepthImageMSAA, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

        // Transition layouts
        VkCommandBuffer cmd = BeginSingleTimeCommands();

        // Transition color
        VkImageMemoryBarrier colorBarrier = {};
        colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        colorBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorBarrier.image = m_ColorImageMSAA;
        colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorBarrier.subresourceRange.baseMipLevel = 0;
        colorBarrier.subresourceRange.levelCount = 1;
        colorBarrier.subresourceRange.baseArrayLayer = 0;
        colorBarrier.subresourceRange.layerCount = 1;
        colorBarrier.srcAccessMask = 0;
        colorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 1, &colorBarrier);

        // Transition depth
        VkImageMemoryBarrier depthBarrier = {};
        depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = m_DepthImageMSAA;
        depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthBarrier.subresourceRange.baseMipLevel = 0;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.baseArrayLayer = 0;
        depthBarrier.subresourceRange.layerCount = 1;
        depthBarrier.srcAccessMask = 0;
        depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &depthBarrier);

        // 1x depth 无需在此处理 — Mesh render pass 通过 depthStencilResolve
        // 自动将 MSAA depth resolve 到 1x depth，finalLayout 已在
        // render pass attachment 中设为 DEPTH_STENCIL_READ_ONLY_OPTIMAL

        EndSingleTimeCommands(cmd);
    }
    VkFormat VulkanContext::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
        for (VkFormat format : candidates) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
                return format;
            }
            else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
                return format;
            }
        }

        throw std::runtime_error("failed to find supported format!");
    }

    void VulkanContext::createUniformBuffer() {
        VkDeviceSize bufferSize = sizeof(UniformBufferObject);

        size_t imageCount = swapChainImages.size();
        uniformBuffers.clear();
        uniformBuffers.reserve(imageCount);

        for (size_t i = 0; i < imageCount; i++) {
            uniformBuffers.emplace_back(bufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        }
    }

    void VulkanContext::createDescriptorSetLayout() {
        std::array<VkDescriptorSetLayoutBinding, 4> bindings = {};

        // Binding 0: Global UBO (projView + camera position)
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[0].pImmutableSamplers = nullptr;

        // Binding 1: Light SSBO (all scene lights)
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].pImmutableSamplers = nullptr;

        // Binding 2: SH Irradiance UBO (9 × vec4 = 144 bytes)
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[2].pImmutableSamplers = nullptr;

        // Binding 3: Prefiltered environment cubemap
        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[3].pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout!");
        }
    }

    void VulkanContext::createDescriptorPool() {
        std::array<VkDescriptorPoolSize, 3> poolSizes = {};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = 100;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = 100;
        poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[2].descriptorCount = 100;

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 200;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool!");
        }
    }

    void VulkanContext::createDescriptorSets() {
        size_t imageCount = swapChainImages.size();

        // 预先创建 Light SSBO (确保 descriptor 不会被绑定到 null buffer)
        VkDeviceSize lightSSBOSize = 32 * 48; // MAX_LIGHTS * sizeof(GPULight) = 1536 bytes
        m_LightSSBO = VulkanBuffer(lightSSBOSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        // 预先创建 SH IBL buffer (9 × vec4 = 144 bytes)，初始化为 0
        m_IBL_SHBuffer = VulkanBuffer(9 * sizeof(glm::vec4),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        std::vector<VkDescriptorSetLayout> layouts(imageCount, descriptorSetLayout);

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(imageCount);
        allocInfo.pSetLayouts = layouts.data();

        descriptorSets.resize(imageCount);
        if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate descriptor set!");
        }

        // 注意: binding 3 (cubemap) 不在此处写入 — cubemap 在 EditorLayer::OnAttach
        // 加载完成后通过 SetEnvironmentMap() 更新，避免无效的 VK_NULL_HANDLE 导致验证层报错

        for (size_t i = 0; i < imageCount; i++)
        {
            std::array<VkWriteDescriptorSet, 3> descriptorWrites = {};

            // Binding 0: Global UBO
            VkDescriptorBufferInfo uboInfo = {};
            uboInfo.buffer = uniformBuffers[i].GetBuffer();
            uboInfo.offset = 0;
            uboInfo.range = sizeof(UniformBufferObject);
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = descriptorSets[i];
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pBufferInfo = &uboInfo;

            // Binding 1: Light SSBO
            VkDescriptorBufferInfo lightSSBOInfo = {};
            lightSSBOInfo.buffer = m_LightSSBO.GetBuffer();
            lightSSBOInfo.offset = 0;
            lightSSBOInfo.range = m_LightSSBO.GetSize();
            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = descriptorSets[i];
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pBufferInfo = &lightSSBOInfo;

            // Binding 2: SH Irradiance UBO
            VkDescriptorBufferInfo shInfo = {};
            shInfo.buffer = m_IBL_SHBuffer.GetBuffer();
            shInfo.offset = 0;
            shInfo.range = m_IBL_SHBuffer.GetSize();
            descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[2].dstSet = descriptorSets[i];
            descriptorWrites[2].dstBinding = 2;
            descriptorWrites[2].dstArrayElement = 0;
            descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[2].descriptorCount = 1;
            descriptorWrites[2].pBufferInfo = &shInfo;

            // Binding 3 (cubemap) 由 SetEnvironmentMap() 在 cubemap 加载完成后单独更新
            vkUpdateDescriptorSets(device, 3, descriptorWrites.data(), 0, nullptr);
        }
    }
    
    VkCommandBuffer VulkanContext::BeginSingleTimeCommands() {
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffer!");
        }

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin command buffer!");
        }

        return commandBuffer;
    }

    void VulkanContext::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    uint32_t VulkanContext::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }


        throw std::runtime_error("failed to find suitable memory type!");
    }

    void VulkanContext::createCommandBuffers() {
        commandBuffers.resize(swapChainImageViews.size());
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

        if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers!");
        }


    }
    void VulkanContext::createSemaphores() {
        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS) {

            throw std::runtime_error("failed to create semaphores!");
        }

    }
    //void VulkanContext::updateUniformBuffer() {
    //    static auto startTime = std::chrono::high_resolution_clock::now();

    //    auto currentTime = std::chrono::high_resolution_clock::now();
    //    float time = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count() / 1000.0f;

    //    UniformBufferObject ubo = {};
    //    ubo.model = glm::rotate(ubo.model, time * glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    //    ubo.view = glm::lookAt(glm::vec3(0.0f, 10.0f, 50.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    //    ubo.proj = glm::perspective(glm::radians(45.0f), swapChainExtent.width / (float)swapChainExtent.height, 0.1f, 100.0f);
    //    ubo.proj[1][1] *= -1;

    //    void* data;
    //    vkMapMemory(device, uniformBufferMemory, 0, sizeof(ubo), 0, &data);
    //    memcpy(data, &ubo, sizeof(ubo));
    //    vkUnmapMemory(device, uniformBufferMemory);

    //}

    void VulkanContext::updateGlobalUniforms(glm::mat4 projView)
    {
        UniformBufferObject ubo{};
        ubo.projView = projView;

        uniformBuffers[currentImageIndex].SetData(&ubo, sizeof(ubo));
    }

    void VulkanContext::UpdateGlobalCameraUniforms(const glm::vec4& cameraPos)
    {
        uniformBuffers[currentImageIndex].SetData(&cameraPos, sizeof(glm::vec4), offsetof(UniformBufferObject, cameraPosition));
    }

    void VulkanContext::OnWindowResized(int width, int height) {
        if (width == 0 || height == 0) return;

        recreateSwapChain();
    }

    void VulkanContext::SetEnvironmentMap(const VkDescriptorImageInfo& envMapInfo, const std::vector<glm::vec4>& shData)
    {
        m_EnvMapDescriptor = envMapInfo;

        // 此函数由 EditorLayer::OnAttach 在 descriptor sets 创建完成后调用，
        // 因此 descriptorSets 不会为空
        if (descriptorSets.empty())
        {
            EG_CORE_WARN("SetEnvironmentMap called before descriptor sets created, ignoring.");
            return;
        }

        // Update SH buffer data (shared across all frames)
        if (m_IBL_SHBuffer.IsValid() && !shData.empty())
        {
            m_IBL_SHBuffer.SetData(shData.data(), shData.size() * sizeof(glm::vec4));
        }

        // Update cubemap descriptor binding in all descriptor sets
        for (size_t i = 0; i < descriptorSets.size(); i++)
        {
            VkWriteDescriptorSet write = {};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSets[i];
            write.dstBinding = 3;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &m_EnvMapDescriptor;

            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }

}