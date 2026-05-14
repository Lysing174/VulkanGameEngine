#include "pch.h"
#include "VulkanRenderer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanShader.h"

namespace Engine
{
	// void VulkanRenderer::BeginScene(const Camera& camera, const glm::mat4& transform)
	// {
	// 	glm::mat4 projection = camera.GetProjection();
	// 	projection[1][1] *= -1;
	// 	glm::mat4 projView = projection * glm::inverse(transform);
	//
	// 	VulkanContext::Get()->BeginFrame(projView);
	// }
	void VulkanRenderer::BeginScene(const EditorCamera& camera)
	{
		glm::mat4 projection = camera.GetProjection();
		projection[1][1] *= -1;
		glm::mat4 projView = projection * camera.GetViewMatrix();

		VulkanContext::Get()->BeginFrame(projView);
		VkCommandBuffer cmd = VulkanContext::Get()->GetCurrentCommandBuffer();
		VkDescriptorSet globalDescriptorSet = VulkanContext::Get()->GetCurrentDescriptorSet();
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Renderer::GetShaderLibrary()->Get("Mesh.vert.spv")->GetPipelineLayout(), 0, 1, &globalDescriptorSet, 0, nullptr);
	}
	void VulkanRenderer::EndScene()
	{
		VulkanContext::Get()->EndFrame();
	}

	void VulkanRenderer::BeginMeshRenderPass()
	{
		VulkanContext::Get()->BeginMeshRenderPass();
	}
	
	void VulkanRenderer::BeginGaussianRenderPass()
	{
		VulkanContext::Get()->BeginGaussianRenderPass();
	}
	
	void VulkanRenderer::EndRenderPass()
	{
		VulkanContext::Get()->EndRenderPass();
	}
	
	void VulkanRenderer::DrawImGui()
	{
		VulkanContext::Get()->DrawImGui();
	}
	
	void VulkanRenderer::DrawMesh(const MeshRenderCommandRequest& request)
	{
		VkCommandBuffer cmd = VulkanContext::Get()->GetCurrentCommandBuffer();

		request.Mesh->Bind();

		// 5. 推送 Push Constants (核心步骤)
		// 注意：这里假设你的 Shader 里 PushConstant 包含了 Transform 和 EntityID
		// 如果你的 Shader 里只有 Transform，就只 Push Transform

		// Push Transform (Vertex Shader)
		vkCmdPushConstants(
			cmd,
			request.Material->GetShader()->GetPipelineLayout(),
			VK_SHADER_STAGE_VERTEX_BIT, // 假设 Transform 在顶点着色器
			0,                          // 偏移量 0
			sizeof(glm::mat4),          // 大小
			&(request.Transform)
		);

		// (可选) Push EntityID (Fragment Shader / Vertex Shader)
		if (request.EntityID != -1)
		{
			// 注意：你需要确保 shader 里定义了 EntityID 且计算好了 offset
			// 这里为了简单，假设它紧跟在 mat4 后面，或者你自己管理 offset
			// vkCmdPushConstants(cmd, layout, stage, sizeof(glm::mat4), sizeof(int), &entityID);
		}

		vkCmdDrawIndexed(cmd, request.SubmeshIndexCount, 1, request.SubmeshFirstIndex, request.SubmeshFirstVertex, 0);
	}
	void VulkanRenderer::DrawGaussian(const GaussianRenderCommandRequest& request)
	{
		VkCommandBuffer cmd = VulkanContext::Get()->GetCurrentCommandBuffer();

		// Push Constants: Transform matrix (64 字节, VS)
		vkCmdPushConstants(
			cmd,
			Renderer::GetShaderLibrary()->Get("Gaussian.vert.spv")->GetPipelineLayout(),
			VK_SHADER_STAGE_VERTEX_BIT,
			0,
			sizeof(glm::mat4),
			&(request.Transform)
		);

		// POINT_LIST: 每个 Gaussian 中心绘制为一个点
		// firstVertex = model 的全局偏移, gl_VertexIndex 自动从 offset 开始索引全局 SSBO
		uint32_t vertexCount = request.Model ? request.Model->GetGaussianCount() : 0;
		uint32_t firstVertex = request.Model ? request.Model->GetGlobalOffset() : 0;
		if (vertexCount > 0)
			vkCmdDraw(cmd, vertexCount, 1, firstVertex, 0);
	}
}