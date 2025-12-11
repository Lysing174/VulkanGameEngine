#pragma once
#include "Engine/Renderer/Texture.h"
#include "vulkan/vulkan.h"

namespace Engine {

    class VulkanTexture2D : public Texture2D
    {
    public:
        VulkanTexture2D(const std::string& path);
        // 特殊构造函数：用于创建纯色默认贴图 (1x1像素)
        VulkanTexture2D(uint32_t width, uint32_t height, void* data);
        virtual ~VulkanTexture2D();

        virtual void Bind(uint32_t slot = 0) const override {}

        virtual uint32_t GetWidth() const override { return m_Width; }
        virtual uint32_t GetHeight() const override { return m_Height; }
        virtual uint32_t GetRendererID() const override { return (uint32_t)m_Image; }
        virtual std::string GetPath() const override { return m_Path; }

        VkImage GetImage() const { return m_Image; }
        VkImageView GetImageView() const { return m_ImageView; }
        VkSampler GetSampler() const { return m_Sampler; }
        VkDescriptorSet GetDescriptorSet() const { return m_MaterialDescriptorSet; }        // ImGui 需要 DescriptorSet 才能显示图片
        const VkDescriptorImageInfo& GetDescriptorImageInfo() const { return m_DescriptorImageInfo; }

    private:
        // 内部辅助函数
        void CreateTextureImageView();
        void CreateTextureSampler();

    private:
        std::string m_Path;
        uint32_t m_Width, m_Height, m_Channels;

        VkImage m_Image = VK_NULL_HANDLE;
        VkDeviceMemory m_ImageMemory = VK_NULL_HANDLE;
        VkImageView m_ImageView = VK_NULL_HANDLE;
        VkSampler m_Sampler = VK_NULL_HANDLE;
        VkFormat m_ImageFormat = VK_FORMAT_R8G8B8A8_SRGB; // 或者 UNORM

        // 用于 ImGui 显示或绑定到材质的 DescriptorSet
        VkDescriptorSet m_MaterialDescriptorSet = VK_NULL_HANDLE;

        VkDescriptorImageInfo m_DescriptorImageInfo;
    };
}