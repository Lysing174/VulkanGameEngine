#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <Engine/Renderer/Buffer.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace Engine {

	// ============================================================
	// Shader 配置系统 — 让 Shader 按用户自定义配置创建
	// DescriptorSetLayout / PushConstant / PipelineState
	// ============================================================

	// 单个 Descriptor Binding 描述
	struct DescriptorBindingInfo
	{
		uint32_t Binding;
		VkDescriptorType Type;
		VkShaderStageFlags StageFlags;
		uint32_t DescriptorCount = 1;
		std::string Name; // 语义名 (用于 Material::SetTexture 查找)
	};

	// Push Constant 范围描述
	struct PushConstantRangeInfo
	{
		VkShaderStageFlags StageFlags;
		uint32_t Offset;
		uint32_t Size;
	};

	// 管线状态配置
	struct PipelineStateInfo
	{
		VkPrimitiveTopology Topology    = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		bool BlendEnable                = true;
		bool DepthTestEnable            = true;
		bool DepthWriteEnable           = true;
		VkCullModeFlags CullMode        = VK_CULL_MODE_BACK_BIT;
		VkFrontFace FrontFace           = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		bool HasVertexInput             = true;  // false = 无 VBO, 用 gl_VertexIndex 生成顶点
	};

	// Shader 完整配置
	struct ShaderConfig
	{
		std::vector<DescriptorBindingInfo> MaterialBindings;
		std::vector<PushConstantRangeInfo> PushConstantRanges;
		PipelineStateInfo PipelineState;

		// 按 binding 号查找是否存在
		bool HasBinding(uint32_t binding) const
		{
			for (const auto& b : MaterialBindings)
				if (b.Binding == binding) return true;
			return false;
		}

		// 按语义名查找 binding 号 (返回 -1 表示未找到)
		int FindBindingByName(const std::string& name) const
		{
			for (const auto& b : MaterialBindings)
				if (b.Name == name) return (int)b.Binding;
			return -1;
		}

		// 按类型筛选所有 binding
		std::vector<uint32_t> GetBindingsByType(VkDescriptorType type) const
		{
			std::vector<uint32_t> result;
			for (const auto& b : MaterialBindings)
				if (b.Type == type) result.push_back(b.Binding);
			return result;
		}

		// ======== 预置配置工厂 ========

		// 默认 PBR 材质: Albedo + Normal + ORM + MaterialUBO
		static ShaderConfig DefaultPBR()
		{
			ShaderConfig config;
			config.MaterialBindings = {
				{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, "u_AlbedoMap"     },
				{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, "u_NormalMap"     },
				{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, "u_MetalRoughAO"  },
				{ 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_FRAGMENT_BIT, 1, "u_Material"      },
			};
			config.PushConstantRanges = {
				{ VK_SHADER_STAGE_VERTEX_BIT,   0,              sizeof(glm::mat4) },
				{ VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::mat4), sizeof(glm::vec4) },
			};
			config.PipelineState.BlendEnable = false;
			return config;
		}

		// 简化 PBR: Albedo + MaterialUBO (无 NormalMap / ORM)
		static ShaderConfig SimplePBR()
		{
			ShaderConfig config;
			config.MaterialBindings = {
				{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, "u_AlbedoMap" },
				{ 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_FRAGMENT_BIT, 1, "u_Material"  },
			};
			config.PushConstantRanges = {
				{ VK_SHADER_STAGE_VERTEX_BIT,   0,              sizeof(glm::mat4) },
				{ VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::mat4), sizeof(glm::vec4) },
			};
			config.PipelineState.BlendEnable = false;
			return config;
		}

		// 最小配置: 仅 Push Constants, 无 Descriptor (全屏四边形等)
		static ShaderConfig Minimal()
		{
			ShaderConfig config;
			config.PipelineState.HasVertexInput = false;
			config.PipelineState.DepthTestEnable = false;
			config.PipelineState.DepthWriteEnable = false;
			config.PipelineState.CullMode = VK_CULL_MODE_NONE;
			return config;
		}

		// 深度可视化: 一个深度纹理采样器 + Push Constants
		static ShaderConfig DepthVisualizer()
		{
			ShaderConfig config;
			config.MaterialBindings = {
				{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, "u_DepthMap" },
			};
			config.PushConstantRanges = {
				{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec2) * 2 },
			};
			config.PipelineState.HasVertexInput = false;
			config.PipelineState.DepthTestEnable = false;
			config.PipelineState.DepthWriteEnable = false;
			config.PipelineState.CullMode = VK_CULL_MODE_NONE;
			return config;
		}

		// 高斯点云: 深度纹理采样器 + GaussianData SSBO + Push Constants, POINT_LIST拓扑
		static ShaderConfig GaussianPointCloud()
		{
			ShaderConfig config;
			config.MaterialBindings = {
				{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, "u_DepthMap" },
				{ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1, "GaussianDataBuffer" },
			};
			config.PushConstantRanges = {
				{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4) },
			};
			config.PipelineState.HasVertexInput = false;
			config.PipelineState.DepthTestEnable = false;
			config.PipelineState.DepthWriteEnable = false;
			config.PipelineState.CullMode = VK_CULL_MODE_NONE;
			config.PipelineState.Topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
			return config;
		}

		// 3DGS Splat 渲染: FrameInfo UBO + GaussianData SSBO + SortedIndices SSBO + DepthMap + ModelTransforms SSBO
		// TRIANGLE_LIST 拓扑, 实例化 Quad 绘制, Alpha 混合
		static ShaderConfig GaussianSplat()
		{
			ShaderConfig config;
			config.MaterialBindings = {
				{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1, "FrameInfo" },
				{ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1, "GaussianDataBuffer" },
				{ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1, "SortedIndicesBuffer" },
				{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, "u_DepthMap" },
				{ 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1, "ModelTransformBuffer" },
			};
			config.PipelineState.HasVertexInput = false;
			config.PipelineState.DepthTestEnable = false;
			config.PipelineState.DepthWriteEnable = false;
			config.PipelineState.CullMode = VK_CULL_MODE_NONE;
			config.PipelineState.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			config.PipelineState.BlendEnable = true;
			return config;
		}
	};

	// ============================================================
	// Shader 基类
	// ============================================================

	class Shader
	{
	public:
		virtual ~Shader() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual void CreatePipeline(const std::shared_ptr<BufferLayout>& bufferLayout, std::string pipelineName) = 0;

		virtual const std::string& GetName() const = 0;
		virtual uint32_t GetRendererID() const = 0;
		virtual const VkPipelineLayout GetPipelineLayout() const = 0;

		// 获取 Shader 的配置
		virtual const ShaderConfig& GetConfig() const = 0;

		// 按语义名查找 texture binding (兼容旧接口)
		uint32_t GetTextureBinding(const std::string& textureName) const
		{
			int binding = GetConfig().FindBindingByName(textureName);
			if (binding >= 0) return (uint32_t)binding;

			EG_CORE_WARN("Shader resource not found: {0}", textureName);
			return (uint32_t)-1;
		}

		static std::shared_ptr<Shader> Create(const std::string& vertexSrc, const std::string& fragmentSrc, const ShaderConfig& config = ShaderConfig::DefaultPBR());

	};

	// ============================================================
	// Shader 库
	// ============================================================

	class ShaderLibrary
	{
	public:
		void Add(const std::shared_ptr<Shader>& shader); 

		std::shared_ptr<Shader> Load(const std::string& vertexSrc, const std::string& fragmentSrc, const ShaderConfig& config = ShaderConfig::DefaultPBR());

		std::shared_ptr<Shader> Get(const std::string& name);
		bool Exists(const std::string& name) const;

	private:
		std::unordered_map<std::string, std::shared_ptr<Shader>> m_Shaders;
	};
}
