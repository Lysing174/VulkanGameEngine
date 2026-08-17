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

	void VulkanRenderer::BeginMeshRenderPass(std::shared_ptr<VulkanFramebuffer> offscreenFB)
	{
		VulkanContext::Get()->BeginMeshRenderPass(offscreenFB);
	}

	void VulkanRenderer::BeginUIRenderPass()
	{
		VulkanContext::Get()->BeginUIRenderPass();
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
		VkPipelineLayout layout = request.Material->GetShader()->GetPipelineLayout();

		request.Mesh->Bind();

		// Push Transform (Vertex Shader)
		vkCmdPushConstants(
			cmd,
			layout,
			VK_SHADER_STAGE_VERTEX_BIT,
			0,                          // 偏移量 0
			sizeof(glm::mat4),          // 大小
			&(request.Transform)
		);

		// Push LightCount + LightIndices (Fragment Shader, offset = 80)
		if (request.LightCount > 0)
		{
			struct LightPushData {
				uint32_t lightCount;
				uint32_t lightIndices[4]; // 4 * 4 = 16 bytes
			};
			LightPushData lpd;
			lpd.lightCount = request.LightCount;
			memcpy(lpd.lightIndices, request.LightIndices, sizeof(request.LightIndices));
			// Fill remaining slots with 0
			for (uint32_t i = request.LightCount; i < 4; i++)
				lpd.lightIndices[i] = 0;

			vkCmdPushConstants(
				cmd,
				layout,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				sizeof(glm::mat4) + sizeof(glm::vec4),  // offset = 64 + 16 = 80
				sizeof(LightPushData),
				&lpd
			);
		}

		vkCmdDrawIndexed(cmd, request.SubmeshIndexCount, 1, request.SubmeshFirstIndex, request.SubmeshFirstVertex, 0);
	}

}