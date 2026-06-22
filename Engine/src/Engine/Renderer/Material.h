#pragma once

#include "Shader.h"
#include "Texture.h"
#include <glm/glm.hpp>
#include <memory>

namespace Engine {
    // 对应 Shader 中的 layout(set = 1, binding = 0) uniform MaterialData
    struct MaterialUniformBuffer
    {
        glm::vec4 AlbedoColor;   // 基础颜色 (RGBA)

        float Metalness;         // 金属度系数
        float Roughness;         // 粗糙度系数
        float EmissiveIntensity; // 自发光强度
        float AOStrength;        // AO 强度

        glm::vec4 EmissiveColor; // 自发光颜色
        
        int HasAlbedoMap;
        int HasNormalMap;
        int HasMetalRoughnessMap;
        int HasEmissiveMap;
    };
    class Material
    {
    public:
        Material() {}
        Material(const std::shared_ptr<Shader>& shader);
        virtual ~Material();

        void Bind();// 绑定 DescriptorSet

        void PushColor(const glm::vec4& color);

        void SetAlbedoColor(const glm::vec4& color);
        void SetMetalness(float value);
        void SetRoughness(float value);
        void SetEmissive(float intensity, const glm::vec3& color); // 方便起见传入 vec3
        void SetAOStrength(float value);
        void SetUseNormalMap(bool use);
        void SetHasAlbedoMap(bool has);
        void SetHasMetalRoughnessMap(bool has);
        void SetHasEmissiveMap(bool has);
        void SetTexture(const std::string& textureTypeName, std::shared_ptr<Texture2D> texture);

        // 批量更新：调用 Begin 后所有 setter 只写 CPU 侧数据，End 时一次性推到 GPU
        void BeginBatchUpdate();
        void EndBatchUpdate();

        glm::vec4 GetColor() const { return m_UniformData.AlbedoColor; }
        virtual uint32_t GetRendererID() const { return m_RendererID; }
        std::shared_ptr<Shader> GetShader() const { return m_Shader; }
        VkDescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }

    private:
        // 真正的写入 Vulkan DescriptorSet 的函数
        void WriteTextureDescriptor(uint32_t binding, const std::shared_ptr<Texture2D>& texture);
        void WriteTextureDescriptors();
        void WriteUniformDescriptors();
        void UploadUniformBuffer();

        void AllocateDescriptorSet();
    private:
        uint32_t m_RendererID = 0;

        std::shared_ptr<Shader> m_Shader;

        MaterialUniformBuffer m_UniformData;
        std::unordered_map<uint32_t, std::shared_ptr<Texture2D>> m_Textures;
        VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
        VkBuffer m_UniformBuffer;
        VkDeviceMemory m_UniformBufferMemory;
        bool m_BatchUpdate = false;
    };
}