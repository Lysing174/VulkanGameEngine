#pragma once
#include "Engine/Renderer/Renderer.h"
#include "Platform/Vulkan/VulkanFramebuffer.h"
#include <Engine/Renderer/Mesh.h>
#include <glm/glm.hpp>


namespace Engine {
	class VulkanRenderer:public Renderer
	{
	public:
		virtual ~VulkanRenderer() = default;

		//static void BeginScene(const Camera& camera, const glm::mat4& transform);
		static void BeginScene(const EditorCamera& camera);
		static void EndScene();
		static void BeginMeshRenderPass(std::shared_ptr<VulkanFramebuffer> offscreenFB);
		static void BeginUIRenderPass();
		static void EndRenderPass();
		static void DrawImGui();

		static void DrawMesh(const MeshRenderCommandRequest& request);

	private:

	};

}