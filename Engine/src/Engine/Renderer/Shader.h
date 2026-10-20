#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <Engine/Renderer/Buffer.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <Engine/Renderer/GlobalBindings.h>

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
		VkSampleCountFlagBits Samples   = VK_SAMPLE_COUNT_1_BIT;  // MSAA, VK_SAMPLE_COUNT_4_BIT etc.
	};

	// Shader 完整配置
	struct ShaderConfig
	{
		std::vector<DescriptorBindingInfo> MaterialBindings;
		std::vector<PushConstantRangeInfo> PushConstantRanges;
		PipelineStateInfo PipelineState;

		// material descriptor set 所在的 set 序号 (默认 1)
		uint32_t MaterialSetIndex = MATERIAL_SET;

		// 本 shader 的 pipeline layout 引用的全部 set 序号 (升序, 含 material set 若适用).
		// 序号见 GlobalBindings.h:
		//   0 = GaussianRenderer (高斯 SSBO, 仅高斯 shader 绑)
		//   1 = material       (shader 自身持有)
		//   2 = light/shadow    (ShadowRenderer)
		//   3 = env            (EnvRenderer: SH 辐照度 + 预滤波 cubemap)
		// 各 set 的实际 VkDescriptorSetLayout 由各 set 拥有子系统经 Renderer::GetDescriptorSetLayout() 解析.
		// 注意: set0 为高斯专属, 普通 PBR/Mesh/Skybox/Shadow 都不应绑 set0.
		std::vector<uint32_t> SetLayouts = { MATERIAL_SET };  // 默认仅 material; 其他 set 由各 shader 显式声明

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

		// 将“全局 set 序号”(见 GlobalBindings.h) 映射为本 shader pipeline layout 的“局部 set 序号”.
		// SetLayouts 即按局部顺序排列、紧密无空隙的全局 set 列表, 故其下标就是局部序号.
		// 返回局部序号; 若 globalSet 不在 SetLayouts 中返回 -1.
		int GetLocalSetIndex(uint32_t globalSet) const
		{
			for (size_t i = 0; i < SetLayouts.size(); ++i)
				if (SetLayouts[i] == globalSet) return (int)i;
			return -1;
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
				{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, "u_AOMap"         },
				{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, "u_EmissiveMap"   },
				{ 5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_FRAGMENT_BIT, 1, "u_Material"      },
			};
		// GLSL 中 push_constant 为 vert/frag 共享的单个 block, SPIR-V 报出其范围为 [0,192],
		// 故用一个覆盖 [0,192] 的 range 同时允许 VERTEX|FRAGMENT, 避免按 stage 拆分导致 block 越界 (10069).
		config.PushConstantRanges = {
			{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 192 },
		};
		config.PipelineState.BlendEnable = false;
		config.SetLayouts = { MATERIAL_SET, LIGHT_SET, ENV_SET };  // material + light(set2) + env(set3); set0 为高斯专属, PBR 不绑
		return config;
	}

	// 简化 PBR: Albedo + MaterialUBO (无 NormalMap / ORM)
	// 	static ShaderConfig SimplePBR()
	// 	{
	// 		ShaderConfig config;
	// 		config.MaterialBindings = {
	// 			{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, "u_AlbedoMap" },
	// 			{ 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_FRAGMENT_BIT, 1, "u_Material"  },
	// 		};
	// 		config.PushConstantRanges = {
	// 			{ VK_SHADER_STAGE_VERTEX_BIT,   0,              sizeof(glm::mat4) },
	// 			{ VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::mat4), sizeof(glm::vec4) },
	// 		};
	// 	config.PipelineState.BlendEnable = false;
	// 	config.SetLayouts = { MATERIAL_SET, LIGHT_SET, ENV_SET };  // material + light(set2) + env(set3); set0 为高斯专属
	// 	return config;
	// }

		// Skybox: cubemap from env descriptor set (set=3) + VP push constant
		static ShaderConfig Skybox()
		{
			ShaderConfig config;
			// Cubemap (预滤波环境) 来自 set3 (EnvRenderer), 无逐材质绑定
		// Skybox 的 push_constant 为单个 [0,128] 的 vertex block (viewProj 等), 与 SPIR-V 报出的 [0,128] 对齐.
		config.PushConstantRanges = {
			{ VK_SHADER_STAGE_VERTEX_BIT, 0, 128 },
		};
			config.PipelineState.DepthTestEnable = true;
			config.PipelineState.DepthWriteEnable = false;
			config.PipelineState.CullMode = VK_CULL_MODE_NONE;
			config.SetLayouts = { ENV_SET };  // 仅 env(set3); material 为空不计入
			return config;
		}

		// Shadow: depth-only (方向光阴影贴图生成 pass)
		// 仅顶点输入(位置) + 两个 mat4 push constant (model, lightViewProj)
		// 无颜色输出, 仅写深度
		static ShaderConfig Shadow()
		{
			ShaderConfig config;
		// Shadow 的 push_constant 为单个 [0,128] 的 vertex block (model + lightViewProj),
		// 合并为单个 VERTEX range, 避免两个 VERTEX range 冲突 (00292) 且越界 (10069).
		config.PushConstantRanges = {
			{ VK_SHADER_STAGE_VERTEX_BIT, 0, 128 },
		};
			config.PipelineState.BlendEnable = false;
			config.PipelineState.DepthTestEnable = true;
			config.PipelineState.DepthWriteEnable = true;
			config.PipelineState.CullMode = VK_CULL_MODE_BACK_BIT;
			config.PipelineState.Samples = VK_SAMPLE_COUNT_1_BIT;
			config.SetLayouts = { };  // 仅 push constant 
			return config;
		}
		
		// 3DGS Splat 渲染: set0 = GAUSSIAN_SET (高斯专属, 含 FrameInfo UBO + GaussianData SSBO +
		// SortedIndices SSBO + DepthMap + ModelTransform SSBO); 相机矩阵 (view + proj) 经 push constant 传入.
		// TRIANGLE_LIST 拓扑, 实例化 Quad 绘制, Alpha 混合
		static ShaderConfig GaussianSplat()
		{
			ShaderConfig config;
			config.PipelineState.HasVertexInput = false;
			config.PipelineState.DepthTestEnable = false;
			config.PipelineState.DepthWriteEnable = false;
			config.PipelineState.CullMode = VK_CULL_MODE_NONE;
			config.PipelineState.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			config.PipelineState.BlendEnable = true;
			// 相机矩阵 (view + proj, 各 64B) 走 push constant; set0 仅放高斯相关资源 (见 GlobalBindings.h)
			config.PushConstantRanges = {
				{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 },
			};
			config.SetLayouts = { GAUSSIAN_SET };
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
		// 按 RenderPass 名绑定对应 pipeline (找不到则回退到 Bind())
		virtual void Bind(const std::string& renderpassName) const = 0;
		virtual void Unbind() const = 0;

		virtual void CreatePipeline(const std::shared_ptr<BufferLayout>& bufferLayout, std::string pipelineName) = 0;

		virtual const std::string& GetName() const = 0;
		virtual uint32_t GetRendererID() const = 0;
		virtual const VkPipelineLayout GetPipelineLayout() const = 0;

		// 获取 Shader 的配置
		virtual const ShaderConfig& GetConfig() const = 0;

		// 设置 MSAA 采样数 (必须在 CreatePipeline 之前调用)
		virtual void SetSamples(VkSampleCountFlagBits samples) = 0;

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

	class VulkanComputeShader;  // 前向声明: 计算着色器 (区别于 VS/FS, 由 ShaderLibrary 单独管理)

	class ShaderLibrary
	{
	public:
		void Add(const std::shared_ptr<Shader>& shader); 

		std::shared_ptr<Shader> Load(const std::string& vertexSrc, const std::string& fragmentSrc, const ShaderConfig& config = ShaderConfig::DefaultPBR());

		std::shared_ptr<Shader> Get(const std::string& name);
		bool Exists(const std::string& name) const;

		// 计算着色器 (区别于 VS/FS): 仅 .spv 路径 + ShaderConfig, 通过 VulkanComputeShader 的
		// 构造函数重载 (path, config) 创建, 存入独立的 compute map (类型不同于 Shader).
		std::shared_ptr<VulkanComputeShader> LoadCompute(const std::string& spvPath, const ShaderConfig& config = ShaderConfig{});
		std::shared_ptr<VulkanComputeShader> GetCompute(const std::string& name);
		bool ComputeExists(const std::string& name) const;

	private:
		std::unordered_map<std::string, std::shared_ptr<Shader>> m_Shaders;
		std::unordered_map<std::string, std::shared_ptr<VulkanComputeShader>> m_ComputeShaders;
	};
}
