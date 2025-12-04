#pragma once
#include "Engine/Renderer/GraphicsContext.h"
#include "Engine/Renderer/RendererAPI.h"
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
            glm::mat4 model;
            glm::mat4 view;
            glm::mat4 proj;
        };


    public:
        VulkanContext(GLFWwindow* windowHandle);
        ~VulkanContext();

        virtual void Init() override;
        virtual void BeginFrame(const EditorCamera& camera) override;
        virtual void DrawFrame() override;
        virtual void EndFrame() override;
        virtual void DrawModel(uint32_t indexCount)override;

        void OnWindowResized(int width, int height);

        void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
        void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);


        VkInstance GetInstance() { return instance; }
        VkPhysicalDevice GetPhysicalDevice() { return physicalDevice; }
        VkDevice GetDevice() { return device; }
        VkQueue GetGraphicsQueue() { return graphicsQueue; }
        VkDescriptorPool GetDescriptorPool() { return descriptorPool; } // 刚加的
        uint32_t GetMinImageCount() { return 2; } // 通常是 2 或 3，看你的 swapchain 设置
        uint32_t GetImageCount() { return (uint32_t)swapChainImages.size(); } // 你 swapchain 里的图片数量
        VkRenderPass GetRenderPass() { return renderPass; } // 必须！ImGui 需要知道它在哪个 RenderPass 里画
        VkCommandBuffer GetCurrentCommandBuffer() { return commandBuffers[currentImageIndex]; }
        VkDescriptorSet GetCurrentDescriptorSet() { return descriptorSets[currentImageIndex]; }
        VkDescriptorSetLayout GetDescriptorSetLayout() { return descriptorSetLayout; }
        VkExtent2D GetSwapChainExtent() { return swapChainExtent; }

        static VulkanContext* Get() { return s_Instance; }

    private:
        VkInstance instance;
        GLFWwindow* window;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkSurfaceKHR surface;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device;
        VkSwapchainKHR swapChain;
        VkRenderPass renderPass;
        VkDescriptorSetLayout descriptorSetLayout;
        VkCommandPool commandPool;
        VkDescriptorPool descriptorPool;
        std::vector<VkDescriptorSet> descriptorSets;

        std::vector<VkBuffer> uniformBuffers;
        std::vector<VkDeviceMemory> uniformBuffersMemory;
        VkImage textureImage;
        VkDeviceMemory textureImageMemory;
        VkImageView textureImageView;
        VkSampler textureSampler;
        VkImage depthImage;
        VkDeviceMemory depthImageMemory;
        VkImageView depthImageView;

        VkQueue graphicsQueue;
        VkQueue presentQueue;
        std::vector<VkImage> swapChainImages;
        std::vector<VkImageView> swapChainImageViews;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;
        std::vector<VkFramebuffer> swapChainFramebuffers;
        std::vector<VkCommandBuffer> commandBuffers;
        VkSemaphore imageAvailableSemaphore;
        VkSemaphore renderFinishedSemaphore;
        uint32_t currentImageIndex = 0;


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
        void createRenderPass();
        void createFramebuffers();
        void createCommandPool();
        void createDepthResources();
        void createTextureImage();
        void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
        VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
        void createTextureSampler();
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
        void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
        void createTextureImageView();

        void updateUniformBuffer();
        void updateGlobalUniforms(const EditorCamera& camera);
    };

}