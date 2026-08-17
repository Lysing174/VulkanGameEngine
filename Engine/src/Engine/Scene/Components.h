#pragma once

#include "Engine/Scene/SceneCamera.h"
#include "Engine/Renderer/Mesh.h" 
#include "Engine/Renderer/Material.h"
#include "Engine/Scene/UUID.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace Engine {
	
	struct IDComponent
	{
		UUID ID;

		IDComponent() = default;
		IDComponent(const IDComponent&) = default;
	};
	struct TagComponent
	{
		std::string Tag;

		TagComponent() = default;
		TagComponent(const TagComponent&) = default;
		TagComponent(const std::string& tag) : Tag(tag) {}
	};

	struct TransformComponent
	{
		glm::vec3 Translation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 Rotation = { 0.0f, 0.0f, 0.0f }; // 欧拉角 (弧度)
		glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

		TransformComponent() = default;
		TransformComponent(const TransformComponent&) = default;
		TransformComponent(const glm::vec3& translation) : Translation(translation) {}

		// 计算模型矩阵
		glm::mat4 GetTransform() const
		{
			glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));

			return glm::translate(glm::mat4(1.0f), Translation)
				* rotation
				* glm::scale(glm::mat4(1.0f), Scale);
		}
	};

	struct MeshFilterComponent
	{
		std::shared_ptr<Mesh> Mesh;

		MeshFilterComponent() = default;
		MeshFilterComponent(const std::shared_ptr<Engine::Mesh>& mesh)
			: Mesh(mesh) {}
	};

	struct MeshRendererComponent
	{
		std::vector<std::shared_ptr<Material>> Materials;

		std::shared_ptr<Material> GetMaterial(uint32_t index = 0)
		{
			if (index < Materials.size()) return Materials[index];
			return nullptr; 
		}

		bool CastShadows = true;    // 是否投射阴影
		bool ReceiveShadows = true; // 是否接收阴影
		uint32_t MaxLights = 4;     // 最多作用该物体的光源数量

		MeshRendererComponent() = default;
		MeshRendererComponent(const std::vector<std::shared_ptr<Material>>& materials)
			: Materials(materials) {}
		MeshRendererComponent(std::shared_ptr<Material> material)
		{
			Materials.push_back(material);
		}
	};
	struct CameraComponent
	{
		SceneCamera Camera;
		bool Primary = true; // 是否为主相机
		bool FixedAspectRatio = false;

		CameraComponent() = default;
		CameraComponent(const CameraComponent&) = default;
	};

	struct PointLightComponent
	{
		glm::vec3 Color = { 1.0f, 1.0f, 1.0f };
		float Intensity = 10.0f;
		float Range = 20.0f;

		PointLightComponent() = default;
		PointLightComponent(const glm::vec3& color, float intensity, float range)
			: Color(color), Intensity(intensity), Range(range) {}
	};

	struct DirectLightComponent
	{
		glm::vec3 Direction = { 0.0f, -1.0f, 0.0f }; // 光照方向（世界空间，从光源指向场景）
		glm::vec3 Color = { 1.0f, 1.0f, 1.0f };
		float Intensity = 5.0f;

		DirectLightComponent() = default;
		DirectLightComponent(const glm::vec3& direction, const glm::vec3& color, float intensity)
			: Direction(glm::normalize(direction)), Color(color), Intensity(intensity) {}
	};
}