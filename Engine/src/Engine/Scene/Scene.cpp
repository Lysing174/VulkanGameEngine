#include "pch.h"
#include "Scene.h"

#include "Components.h" 
#include "Entity.h"     // 必须包含实体包装类 (见下文)
#include "Engine/Renderer/Renderer.h" 

#include <glm/glm.hpp>

namespace Engine {

	Scene::Scene()
	{
	}

	Scene::~Scene()
	{
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		return CreateEntityWithUUID(UUID(), name); 
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		Entity entity = { m_Registry.create(), this };
		entity.AddComponent<IDComponent>(uuid);
		entity.AddComponent<TransformComponent>();
		auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = "None";

		m_EntityMap[uuid] = entity;

		return entity;
	}

	void Scene::DestroyEntity(Entity entity)
	{
		m_EntityMap.erase(entity.GetUUID());
		m_Registry.destroy(entity);
	}
	//void Scene::OnUpdateEditor(EditorCamera& camera)
	//{
	//	// 1. 开始场景渲染 (传入编辑器相机)
	//	Renderer::BeginScene(camera);

	//	// 2. 遍历所有带有 [Mesh] 和 [Transform] 的实体
	//	// group 是一种优化遍历，比 view 更快
	//	auto group = m_Registry.group<TransformComponent>(entt::get<MeshComponent>);

	//	for (auto entity : group)
	//	{
	//		// 获取组件引用
	//		auto [transform, mesh] = group.get<TransformComponent, MeshComponent>(entity);

	//		// 3. 提交给 Vulkan 渲染器
	//		// 这里的 mesh.Mesh 是你之前定义的 Mesh 指针
	//		// transform.GetTransform() 返回 glm::mat4 模型矩阵
	//		if (mesh.Mesh)
	//		{
	//			Renderer::Submit(mesh.Mesh->GetShader(), mesh.Mesh, transform.GetTransform());
	//		}
	//	}

	//	// 3. 结束场景
	//	Renderer::EndScene();
	//}

	//void Scene::OnUpdateRuntime(Timestep ts)
	//{
	//	// 1. 处理脚本/原生 C++ 脚本逻辑
	//	{
	//		// m_Registry.view<NativeScriptComponent>()...
	//		// 后面教程会教这个
	//	}

	//	// 2. 寻找主相机 (Primary Camera)
	//	Camera* mainCamera = nullptr;
	//	glm::mat4 cameraTransform;

	//	auto view = m_Registry.view<TransformComponent, CameraComponent>();
	//	for (auto entity : view)
	//	{
	//		auto [transform, camera] = view.get<TransformComponent, CameraComponent>(entity);

	//		if (camera.Primary)
	//		{
	//			mainCamera = &camera.Camera;
	//			cameraTransform = transform.GetTransform();
	//			break; // 找到一个主相机就停
	//		}
	//	}

	//	// 3. 渲染 (如果场景里有相机)
	//	if (mainCamera)
	//	{
	//		// 游戏相机的 View 矩阵是 Transform 的逆矩阵
	//		glm::mat4 viewProjection = mainCamera->GetProjection() * glm::inverse(cameraTransform);

	//		Renderer::BeginScene(*mainCamera, glm::inverse(cameraTransform));

	//		// 像上面一样遍历 Mesh 渲染
	//		auto group = m_Registry.group<TransformComponent>(entt::get<MeshComponent>);
	//		for (auto entity : group)
	//		{
	//			auto [transform, mesh] = group.get<TransformComponent, MeshComponent>(entity);
	//			if (mesh.Mesh)
	//			{
	//				Renderer::Submit(mesh.Mesh->GetShader(), mesh.Mesh, transform.GetTransform());
	//			}
	//		}

	//		Renderer::EndScene();
	//	}
	//}

	void Scene::OnViewportResize(uint32_t width, uint32_t height)
	{
		m_ViewportWidth = width;
		m_ViewportHeight = height;

		// 当视口变化时，更新所有拥有相机的实体，调整它们的宽高比
		auto view = m_Registry.view<CameraComponent>();
		for (auto entity : view)
		{
			auto& cameraComponent = view.get<CameraComponent>(entity);
			if (!cameraComponent.FixedAspectRatio)
			{
				cameraComponent.Camera.SetViewportSize(width, height);
			}
		}
	}

}