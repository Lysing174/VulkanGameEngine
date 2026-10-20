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
		// 来源模型资产 UUID (由 AssetLibrary<Model> 持久化在 .meta 中)。
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
	struct GaussianComponent
	{
		// 来源高斯资产 UUID (由 AssetLibrary<GaussianModel> 持久化在 .meta 中)。
		// 组件只存 UUID，不存路径；运行时按 UUID 从 AssetLibrary<GaussianModel> 取 GaussianModel。
		UUID GaussianID;

		GaussianComponent() : GaussianID(0) {}
		GaussianComponent(const UUID& gaussianID) : GaussianID(gaussianID) {}

		bool HasGaussian() const { return (uint64_t)GaussianID != 0; }
		const UUID& GetGaussianID() const { return GaussianID; }
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
		// 方向光方向 = 其 Transform 本地 +Z 轴 (光从原点照向 +Z)。
		// 阴影范围 = Transform 的 Scale 分量 (半长, 即立方体半尺寸)，立方体朝向 = Transform 旋转。
		// Transform.Translation = 阴影盒近端中心 (光所在处, 位于正交近平面)。
		glm::vec3 Color = { 1.0f, 1.0f, 1.0f };
		float Intensity = 5.0f;

		bool  CastShadow = false;   // 是否投射阴影
		float ShadowBias = 0.0f; // 阴影偏移，缓解 shadow acne

		DirectLightComponent() = default;
		DirectLightComponent(const glm::vec3& color, float intensity, bool castShadow=false, float shadowBias=0.0f)
			: Color(color), Intensity(intensity),CastShadow(castShadow), ShadowBias(shadowBias) {}
	};
}