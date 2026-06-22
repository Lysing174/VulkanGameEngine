#pragma once
#include "Engine/Renderer/GraphicsContext.h"
//#include "Engine/Renderer/Renderer.h"
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stb_image.h>

#include <vector>
#include <set>
#include <array>
#include <iostream>
#include <stdexcept>
#include <functional>
#include <fstream>
#include <chrono>
#include <entt.hpp>

#include "VulkanRenderPass.h"

#ifdef EG_DEBUG
const bool enableValidationLayers = true;
#else
const bool enableValidationLayers = false;
#endif


namespace Engine {


    class VulkanContext : public GraphicsContext
    {

        const std::vector<const char*> validationLayers = {
            "VK_LAYER_KHRONOS_validation"
        };

        const std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };


        /// <summary>
        /// 命令队列簇
        /// </summary>
        struct QueueFamilyIndices {
            int graphicsFamily = -1; //-1表示没找到
            int presentFamily = -1;

            bool isComplete() {
                return graphicsFamily >= 0 && presentFamily >= 0;
            }
        };
        /// <summary>
        /// 交换链
        /// </summary>
        struct SwapChainSupportDetails {
            VkSurfaceCapabilitiesKHR capabilities;
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> presentModes;
        };

        /// <summary>
        /// 创建编译信息发送器
        /// </summary>
        VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
            auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
            if (func != nullptr) {
                std::cout << "\033[33mdebug messenger created.\033[0m" << std::endl;
                return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
            }
            else {
                std::cout << "\033[33mdebug messenger cannot create.\033[0m" << std::endl;
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            }
        }
        /// <summary>
        /// 销毁编译信息发送器
        /// </summary>
        void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
            auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
            if (func != nullptr) {
                func(instance, debugMessenger, pAllocator);
                std::cout << "\033[33mdebug messenger destroyed.\033[0m" << std::endl;

            }
        }

        struct UniformBufferObject {
            glm::mat4 projView;             // offset 0,  64 bytes
            glm::vec4 cameraPosition;        // offset 64, 16 bytes (xyz = camera world pos)
            glm::vec4 lightPosition;         // offset 80, 16 bytes (xyz = light world pos)
            glm::vec4 lightColor;            // offset 96, 16 bytes (rgb = color, a = intensity)
        };


    public:
        VulkanContext(GLFWwindow* windowHandle);
        ~VulkanContext();

        virtual void Init() override;
        virtual void BeginFrame(glm::mat4 projView) override;
        virtual void EndFrame() override;
        void BeginMeshRenderPass();
        void BeginGaussianRenderPass();
        void EndRenderPass();
        void DrawImGui();

        void UpdateGlobalLightUniforms(const glm::vec4& cameraPos, const glm::vec4& lightPos, const glm::vec4& lightColor);

        void OnWindowResized(int width, int height);

		void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
		void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
		void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
		void CreateImage(uint32_t width, uint32_t height, VkSampleCountFlagBits samples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
		VkSampleCountFlagBits GetMaxUsableSampleCount();
        void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
        void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
        VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);


        VkInstance GetInstance() { return instance; }
        VkPhysicalDevice GetPhysicalDevice() { return physicalDevice; }
        VkDevice GetDevice() { return device; }
        VkQueue GetGraphicsQueue() { return graphicsQueue; }
        VkDescriptorPool GetDescriptorPool() { return descriptorPool; } // 刚加的
        uint32_t GetMinImageCount() { return 2; } // 通常是 2 或 3，看你的 swapchain 设置
        uint32_t GetImageCount() { return (uint32_t)swapChainImages.size(); } // 你 swapchain 里的图片数量
        std::shared_ptr<VulkanRenderPass> GetCurrentRenderPass() {
            if (renderPassesMap.find(currentRenderPassName)==renderPassesMap.end())
                return nullptr;
            return renderPassesMap[currentRenderPassName];
        }
        std::shared_ptr<VulkanRenderPass> GetRenderPass(const std::string& renderPassName) {
            if (renderPassesMap.find(renderPassName)==renderPassesMap.end())
                return nullptr;
            return renderPassesMap[renderPassName];
        }
        VkCommandBuffer GetCurrentCommandBuffer() { return commandBuffers[currentImageIndex]; }
        VkDescriptorSet GetCurrentDescriptorSet() { return descriptorSets[currentImageIndex]; }
        VkDescriptorSetLayout GetGlobalDescriptorSetLayout() { return descriptorSetLayout; }
        VkExtent2D GetSwapChainExtent() { return swapChainExtent; }
		VkImageView GetDepthImageView() { return depthImageView; }
		VkSampler GetDepthSampler() { return depthSampler; }
		VkSampleCountFlagBits GetMSAASamples() const { return m_MSAASamples; }
		void SetMSAASamples(VkSampleCountFlagBits samples) { m_MSAASamples = samples; }

        static VulkanContext* Get() { return s_Instance; }

    private:
        VkInstance instance;
        GLFWwindow* window;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkSurfaceKHR surface;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device;
        VkSwapchainKHR swapChain;
        std::unordered_map<std::string,std::shared_ptr<VulkanRenderPass>> renderPassesMap;
        VkDescriptorSetLayout descriptorSetLayout;
        VkCommandPool commandPool;
        VkDescriptorPool descriptorPool;
        std::vector<VkDescriptorSet> descriptorSets;

        std::vector<VkBuffer> uniformBuffers;
        std::vector<VkDeviceMemory> uniformBuffersMemory;
		VkImage depthImage;
		VkDeviceMemory depthImageMemory;
		VkImageView depthImageView;
		VkFormat depthImageFormat;
		VkSampler depthSampler;  // 深度纹理采样器 (Gaussian shader 用)

		// MSAA resources
		VkSampleCountFlagBits m_MSAASamples = VK_SAMPLE_COUNT_4_BIT;
		VkImage m_ColorImageMSAA = VK_NULL_HANDLE;
		VkDeviceMemory m_ColorImageMemoryMSAA = VK_NULL_HANDLE;
		VkImageView m_ColorImageViewMSAA = VK_NULL_HANDLE;
		VkImage m_DepthImageMSAA = VK_NULL_HANDLE;
		VkDeviceMemory m_DepthImageMemoryMSAA = VK_NULL_HANDLE;
		VkImageView m_DepthImageViewMSAA = VK_NULL_HANDLE;

        VkQueue graphicsQueue;
        VkQueue presentQueue;
        std::vector<VkImage> swapChainImages;
        std::vector<VkImageView> swapChainImageViews;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;
        //std::vector<VkFramebuffer> swapChainFramebuffers;
        std::vector<VkCommandBuffer> commandBuffers;
        VkSemaphore imageAvailableSemaphore;
        VkSemaphore renderFinishedSemaphore;
        uint32_t currentImageIndex = 0;

        std::string currentRenderPassName="";

        const int WIDTH = 800;
        const int HEIGHT = 600;

        const std::string MODEL_PATH = "models/cottage_obj.obj";
        const std::string TEXTURE_PATH = "textures/cottage_diffuse.png";

        static VulkanContext* s_Instance;

    private:
        void initVulkan();
        void cleanupSwapChain();
        void recreateSwapChain();
        void cleanup();

        void createInstance();
        void setupDebugMessenger();
        void createSurface();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createSwapChain();
        void createImageViews();
        void createDescriptorSetLayout();
        void createCommandPool();
		void createDepthResources();
		void createMSAAResources();
		void createUniformBuffer();
        void createDescriptorPool();
        void createDescriptorSets();
        void createCommandBuffers();
        void createSemaphores();

        std::vector<const char*> getRequiredExtensions();
        bool checkValidationLayerSupport();
        void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);  
        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);
        bool isDeviceSuitable(VkPhysicalDevice device);
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        bool checkDeviceExtensionSupport(VkPhysicalDevice device);
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
        VkFormat findDepthFormat();
        VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
        bool hasStencilComponent(VkFormat format);

        void updateGlobalUniforms(glm::mat4 projView);
    };

}