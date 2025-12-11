#pragma once

//#include "Engine/Core/Base.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Entity.h"

namespace Engine {

	class SceneHierarchyPanel
	{
	public:
		SceneHierarchyPanel() = default;
		SceneHierarchyPanel(std::shared_ptr<Scene> scene);

		void SetContext(std::shared_ptr<Scene> scene);

		void OnImGuiRender();

		Entity GetSelectedEntity() const { return m_SelectionContext; }
		void SetSelectedEntity(Entity entity);
	private:
		template<typename T>
		void DisplayAddComponentEntry(const std::string& entryName);

		void DrawEntityNode(Entity entity);
		void DrawComponents(Entity entity);
	private:
		std::shared_ptr<Scene> m_Context;
		Entity m_SelectionContext;
	};

}