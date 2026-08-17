#pragma once
#include "Engine/Renderer/Texture.h"
#include "Platform/Vulkan/VulkanImage.h"
#include "vulkan/vulkan.h"
#include <memory>

namespace Engine {

    class VulkanTexture2D : public Texture2D
    {
    public:
        VulkanTexture2D(const std::string& path, VkFormat format);
        VulkanTexture2D(const void* data, size_t size, VkFormat format);  // 从压缩内存数据创建 (PNG/JPG/BMP...)
        VulkanTexture2D(uint32_t width, uint32_t height, void* data, VkFormat format); // 从原始 RGBA 像素创建
        virtual ~VulkanTexture2D();

        // 禁用拷贝构造函数和赋值运算符
        VulkanTexture2D(const VulkanTexture2D&) = delete;
        VulkanTexture2D& operator=(const VulkanTexture2D&) = delete;

        virtual void Bind(uint32_t slot = 0) const override {}

        virtual uint32_t GetWidth() const override { return m_Width; }
        virtual uint32_t GetHeight() const override { return m_Height; }
        virtual uint32_t GetRendererID() const override;
        virtual std::string GetPath() const override { return m_Path; }

        // Delegate to underlying VulkanImage
        VkImage GetImage() const { return m_Image->GetImage(); }
        VkImageView GetImageView() const { return m_Image->GetImageView(); }
        VkSampler GetSampler() const { return m_Sampler; }
        VkDescriptorSet GetDescriptorSet() const { return m_MaterialDescriptorSet; }
        const VkDescriptorImageInfo& GetDescriptorImageInfo() const { return m_DescriptorImageInfo; }

        // Access the underlying VulkanImage (for compute shader usage)
        const VulkanImage& GetVulkanImage() const { return *m_Image; }

    private:
        std::string m_Path;
        uint32_t m_Width = 0, m_Height = 0, m_Channels = 0;

        std::unique_ptr<VulkanImage> m_Image;
        VkSampler m_Sampler = VK_NULL_HANDLE;
        VkFormat m_ImageFormat = VK_FORMAT_R8G8B8A8_SRGB;

        // 用于 ImGui 显示或绑定到材质的 DescriptorSet
        VkDescriptorSet m_MaterialDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorImageInfo m_DescriptorImageInfo;
    };

} // namespace Engine
