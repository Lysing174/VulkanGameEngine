#include "pch.h"
#include "VulkanShader.h"
#include "Engine/Core/Application.h"
#include "Platform/Vulkan/VulkanContext.h" 

#include <fstream>

namespace Engine {

	VulkanShader::VulkanShader(const std::string& vertexSrc, const std::string& fragmentSrc, const ShaderConfig& config)
		: m_Config(config)
	{
		size_t found = vertexSrc.find_last_of("/\\");
		m_Name = vertexSrc.substr(found + 1);
        std::hash<std::string> hasher;
        m_RendererID = (uint32_t)(hasher(vertexSrc) ^ (hasher(fragmentSrc) << 1));

		auto vertCode = ReadFile(vertexSrc);
		auto fragCode = ReadFile(fragmentSrc);

		m_VertexShaderModule = CreateShaderModule(vertCode);
		m_FragmentShaderModule = CreateShaderModule(fragCode);
        CreateDescriptorSetLayout();
		EG_CORE_INFO("Created Vulkan Shader Modules from: {0} & {1}", vertexSrc, fragmentSrc);
	}

	VulkanShader::~VulkanShader()
	{
		auto device = VulkanContext::Get()->GetDevice();

		for (auto& [name, pipeline] : m_PipelineCache)
			vkDestroyPipeline(device, pipeline, nullptr);
		m_PipelineCache.clear();

        vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
		vkDestroyShaderModule(device, m_VertexShaderModule, nullptr);
		vkDestroyShaderModule(device, m_FragmentShaderModule, nullptr);
        vkDestroyDescriptorSetLayout(device, m_MaterialDescriptorSetLayout, nullptr);
	}

	void VulkanShader::Bind() const {
		auto cmd = VulkanContext::Get()->GetCurrentCommandBuffer();

		// 使用当前 PipelineCache 中的第一个 pipeline (由 CreatePipeline 时设置)
		// 实际上 Bind 时应该知道用哪个 pipeline，这里保留原逻辑
		// 后续可以改为 Bind(const std::string& renderPassName)
		VkPipeline pipeline = VK_NULL_HANDLE;
		if (!m_PipelineCache.empty())
			pipeline = m_PipelineCache.begin()->second;

		if (pipeline == VK_NULL_HANDLE)
		{
			EG_CORE_ERROR("VulkanShader::Bind() - No pipeline available for shader: {0}", m_Name);
			return;
		}

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        //动态设置视口
        // VkViewport viewport{};
        // viewport.x = 0.0f;
        // viewport.y = 0.0f;
        // auto extent = VulkanContext::Get()->GetSwapChainExtent();
        //
        // viewport.width = (float)extent.width;
        // viewport.height = (float)extent.height;
        // viewport.minDepth = 0.0f;
        // viewport.maxDepth = 1.0f;
        // vkCmdSetViewport(cmd, 0, 1, &viewport);
        //
        // VkRect2D scissor{};
        // scissor.offset = { 0, 0 };
        // scissor.extent = extent;
        // vkCmdSetScissor(cmd, 0, 1, &scissor);
	}

	void VulkanShader::Unbind() const
	{
	}

	VkPipeline VulkanShader::GetPipeline(const std::string& name) const
	{
		auto it = m_PipelineCache.find(name);
		if (it != m_PipelineCache.end()) return it->second;
		EG_CORE_WARN("Pipeline not found: {0}", name);
		return VK_NULL_HANDLE;
	}

	void VulkanShader::CreatePipeline(const std::shared_ptr<BufferLayout>& bufferLayout, std::string pipelineName)
	{
		VkRenderPass renderPass = VK_NULL_HANDLE;
		auto rp = VulkanContext::Get()->GetRenderPass(pipelineName);
		if (rp) renderPass = rp->GetVkRenderPass();

		if (renderPass == VK_NULL_HANDLE)
		{
			EG_CORE_ERROR("VulkanShader::CreatePipeline - RenderPass not found: {0}", pipelineName);
			throw std::runtime_error("RenderPass not found for pipeline creation");
		}

		VkPipeline pipeline = VulkanPipeline::Create(
			m_PipelineLayout, *this, renderPass,
			bufferLayout ? *bufferLayout : BufferLayout{},
			m_MaterialDescriptorSetLayout
		);

		m_PipelineCache[pipelineName] = pipeline;
	}

	void VulkanShader::CreateDescriptorSetLayout() {
		// 从 ShaderConfig 中的 MaterialBindings 构建 VkDescriptorSetLayoutBinding
		std::vector<VkDescriptorSetLayoutBinding> bindings;
		bindings.reserve(m_Config.MaterialBindings.size());

		for (const auto& bindingInfo : m_Config.MaterialBindings)
		{
			VkDescriptorSetLayoutBinding layoutBinding = {};
			layoutBinding.binding = bindingInfo.Binding;
			layoutBinding.descriptorCount = bindingInfo.DescriptorCount;
			layoutBinding.descriptorType = bindingInfo.Type;
			layoutBinding.pImmutableSamplers = nullptr;
			layoutBinding.stageFlags = bindingInfo.StageFlags;
			bindings.push_back(layoutBinding);
		}

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.empty() ? nullptr : bindings.data();

		if (vkCreateDescriptorSetLayout(VulkanContext::Get()->GetDevice(), &layoutInfo, nullptr, &m_MaterialDescriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create material descriptor set layout!");
        }
    }

    // ============================================================
    // VulkanPipeline::Create — 由 ShaderConfig 驱动的通用实现
    // ============================================================
    VkPipeline VulkanPipeline::Create(VkPipelineLayout& out_pipelineLayout,
                                      const VulkanShader& shader, const VkRenderPass& renderPass,
                                      const BufferLayout& bufferLayout,
                                      const VkDescriptorSetLayout& descriptorSetLayout)
    {
		const ShaderConfig& config = shader.GetConfig();
		const PipelineStateInfo& ps = config.PipelineState;

		// --- Shader Stages ---
        VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = shader.GetVertexShaderModule();
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = shader.GetFragmentShaderModule();
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

		// --- Vertex Input (由配置决定) ---
        std::vector<VkVertexInputBindingDescription> bindingDescription = {};
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions = {};
        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};

		if (ps.HasVertexInput && !bufferLayout.GetElements().empty())
		{
			vertexInputInfo = VulkanVertexBuffer::CreateVertexInputInfo(bufferLayout, bindingDescription, attributeDescriptions);
		}
		else
		{
			// 无顶点输入 (procedural vertex shader)
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = 0;
			vertexInputInfo.vertexAttributeDescriptionCount = 0;
		}

		// --- Input Assembly ---
        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = ps.Topology;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

		// --- Viewport ---
        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

		// --- Rasterizer ---
        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = ps.CullMode;
        rasterizer.frontFace = ps.FrontFace;
        rasterizer.depthBiasEnable = VK_FALSE;

		// --- Multisampling ---
        VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = ps.Samples;
        multisampling.minSampleShading = 1.0f;

		// --- Color Blend ---
        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = ps.BlendEnable ? VK_TRUE : VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

		// --- Depth Stencil (由配置决定) ---
        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = ps.DepthTestEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = ps.DepthWriteEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

		// --- Dynamic State ---
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

		// --- Push Constants (从配置读取) ---
		std::vector<VkPushConstantRange> pushConstantRanges;
		pushConstantRanges.reserve(config.PushConstantRanges.size());
		for (const auto& pcRange : config.PushConstantRanges)
		{
			VkPushConstantRange range = {};
			range.stageFlags = pcRange.StageFlags;
			range.offset = pcRange.Offset;
			range.size = pcRange.Size;
			pushConstantRanges.push_back(range);
		}

		// --- Pipeline Layout ---
        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        VkDescriptorSetLayout globalLayout = VulkanContext::Get()->GetGlobalDescriptorSetLayout();

		// set=0: Global, set=1: Material
		// 如果 MaterialBindings 为空, 只绑定 globalLayout
		std::vector<VkDescriptorSetLayout> setLayouts;
		setLayouts.push_back(globalLayout);
		if (!config.MaterialBindings.empty())
			setLayouts.push_back(descriptorSetLayout);

        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        pipelineLayoutInfo.pSetLayouts = setLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
        pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.empty() ? nullptr : pushConstantRanges.data();

        if (vkCreatePipelineLayout(VulkanContext::Get()->GetDevice(), &pipelineLayoutInfo, nullptr, &out_pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }

		// --- Graphics Pipeline ---
        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = out_pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex = -1;

		VkPipeline pipeline;
        if (vkCreateGraphicsPipelines(VulkanContext::Get()->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create graphics pipeline!");
        }
		return pipeline;
    }

    std::vector<char> VulkanShader::ReadFile(const std::string& filename)
	{
		std::ifstream file(filename, std::ios::ate | std::ios::binary);

		if (!file.is_open()) {
			EG_CORE_ERROR("Failed to open shader file: {0}", filename);
			throw std::runtime_error("failed to open file!");
		}

		size_t fileSize = (size_t)file.tellg();
		std::vector<char> buffer(fileSize);

		file.seekg(0);
		file.read(buffer.data(), fileSize);
		file.close();

		return buffer;
	}

	VkShaderModule VulkanShader::CreateShaderModule(const std::vector<char>& code)
	{
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		if (vkCreateShaderModule(VulkanContext::Get()->GetDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
			EG_CORE_ERROR("Failed to create shader module!");
			throw std::runtime_error("failed to create shader module!");
		}

		return shaderModule;
	}


}
