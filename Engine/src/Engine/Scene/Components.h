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
		// 来源模型资产 UUID (由 ModelLibrary 持久化在 .meta 中)。
		// 组件只存 UUID，不存路径，改名/移动文件不影响引用。
		UUID MeshID;

		MeshFilterComponent() : MeshID(0) {}
		MeshFilterComponent(const UUID& meshID) : MeshID(meshID) {}

		bool HasMesh() const { return (uint64_t)MeshID != 0; }
		const UUID& GetMeshID() const { return MeshID; }
	};

	struct MeshRendererComponent
	{
		// 材质来源模型资产 UUID (与 Mesh 可来自不同模型，支持自由组合)。
		UUID MaterialID;
		
		uint32_t MaxLights = 4;     // 最多作用该物体的光源数量

		MeshRendererComponent() : MaterialID(0) {}
		MeshRendererComponent(const UUID& materialID) : MaterialID(materialID) {}

		bool HasMaterial() const { return (uint64_t)MaterialID != 0; }
		const UUID& GetMaterialID() const { return MaterialID; }
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