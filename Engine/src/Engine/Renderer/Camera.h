#pragma once

#include <glm/glm.hpp>

namespace Engine {

	class Camera
	{
	public:
		Camera() = default;
		Camera(const glm::mat4& projection)
			: m_Projection(projection) {}

		virtual ~Camera() = default;

		const glm::mat4& GetProjection() const { return m_Projection; }

		// ---- 位姿接口 ----
		// Renderer::BeginScene 只依赖这三个接口取相机信息, 因此除 EditorCamera 外,
		// 外部相机 (如 DatasetCamera — 由数据集内参/位姿直接构造) 也能驱动渲染。
		// 默认实现给无世界位姿的相机 (如 SceneCamera, 其位姿来自 entity transform) 使用;
		// 需要真实位姿的子类应重写这三个函数。
		virtual const glm::mat4& GetViewMatrix() const
		{
			static const glm::mat4 s_Identity(1.0f);
			return s_Identity;
		}
		virtual const glm::vec3& GetPosition() const
		{
			static const glm::vec3 s_Origin(0.0f);
			return s_Origin;
		}
		virtual glm::vec3 GetForwardDirection() const { return glm::vec3(0.0f, 0.0f, -1.0f); }

	protected:
		glm::mat4 m_Projection = glm::mat4(1.0f);
	};

}