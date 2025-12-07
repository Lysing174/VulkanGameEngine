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

	void Scene::OnUpdateRuntime()
	{	
		//Scripts****************************Need Add

		//Phisics****************************Need Add

		// Render 
		Camera* mainCamera = nullptr;
		glm::mat4 cameraTransform;
		{
			auto view = m_Registry.view<TransformComponent, CameraComponent>();
			for (auto entity : view)
			{
				auto [transform, camera] = view.get<TransformComponent, CameraComponent>(entity);

				if (camera.Primary)
				{
					mainCamera = &camera.Camera;
					cameraTransform = transform.GetTransform();
					break;
				}
			}
		}

		if (mainCamera)
		{

			// Draw meshes
			auto view = m_Registry.view<TransformComponent, MeshRendererComponent, MeshFilterComponent>();
			for (auto entity : view)
			{
				auto [transform, renderer, filter] = view.get<TransformComponent, MeshRendererComponent, MeshFilterComponent>(entity);
				if (filter.Mesh) // 稍微做个空指针检查，防止崩溃
				{
					Renderer::SubmitMesh(
						transform.GetTransform(),
						filter.Mesh,
						renderer.Material,
						(int)entity
					);
				}
			}


		}

	}

	void Scene::OnUpdateSimulation(EditorCamera& camera)
	{
		//Phisics****************************Need Add
		


		// Render
		RenderScene(camera);
	}

	void Scene::OnUpdateEditor(EditorCamera& camera)
	{
		// Render
		RenderScene(camera);
	}

	void Scene::OnViewportResize(uint32_t width, uint32_t height)
	{
		if (m_ViewportWidth == width && m_ViewportHeight == height)
			return;

		m_ViewportWidth = width;
		m_ViewportHeight = height;

		// Resize our non-FixedAspectRatio cameras
		auto view = m_Registry.view<CameraComponent>();
		for (auto entity : view)
		{
			auto& cameraComponent = view.get<CameraComponent>(entity);
			if (!cameraComponent.FixedAspectRatio)
				cameraComponent.Camera.SetViewportSize(width, height);
		}
	}

	void Scene::RenderScene(EditorCamera& camera)
	{
		auto view = m_Registry.view<TransformComponent, MeshRendererComponent, MeshFilterComponent>();
		for (auto entity : view)
		{
			auto [transform, renderer, filter] = view.get<TransformComponent, MeshRendererComponent, MeshFilterComponent>(entity);
			if (filter.Mesh) // 稍微做个空指针检查，防止崩溃
			{
				Renderer::SubmitMesh(
					transform.GetTransform(),
					filter.Mesh,      
					renderer.Material, 
					(int)entity
				);
			}
		}

	}

	template<typename T>
	void Scene::OnComponentAdded(Entity entity, T& component)
	{
		// static_assert(false); // 如果你想强制每个组件都必须手动特化，可以取消注释这行
	}

	template<>
	void Scene::OnComponentAdded<IDComponent>(Entity entity, IDComponent& component)
	{
	}

	// 3. Tag (名字) 组件
	template<>
	void Scene::OnComponentAdded<TagComponent>(Entity entity, TagComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<TransformComponent>(Entity entity, TransformComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<MeshFilterComponent>(Entity entity, MeshFilterComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<MeshRendererComponent>(Entity entity, MeshRendererComponent& component)
	{
	}

	// 注意：这里通常需要处理视口大小初始化，防止相机宽高比不对
	template<>
	void Scene::OnComponentAdded<CameraComponent>(Entity entity, CameraComponent& component)
	{
		if (m_ViewportWidth > 0 && m_ViewportHeight > 0)
			component.Camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
	}
}