#pragma once
#include "Engine/Layer/Layer.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Entity.h"
#include "Engine/Renderer/EditorCamera.h"
#include "Engine/Renderer/EnvironmentImage.h"
#include "Editor/SceneHierarchyPanel.h"
#include <chrono>
#include <memory>

namespace Engine {

    class EditorLayer : public Layer
    {
    public:
        EditorLayer();
        virtual ~EditorLayer() = default;

        virtual void OnAttach() override; 
        virtual void OnDetach() override; 
        virtual void OnUpdate() override; 
        virtual void OnFixedUpdate() override;
        virtual void OnEvent(Event& e) override; 
        virtual void OnImGuiRender() override;

        EditorCamera GetEditorCamera() { return m_EditorCamera; }

    private:
        std::shared_ptr<Scene> m_ActiveScene; 
        enum class SceneState
        {
            Edit = 0, Play = 1, Simulate = 2
        };
        SceneState m_SceneState = SceneState::Edit;
        EditorCamera m_EditorCamera;

        Entity m_CubeEntity; 
        std::shared_ptr<Mesh> m_CubeMesh;
        std::shared_ptr<Material> m_RedMaterial;

        glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };
        glm::vec2 m_WindowSize   = { 1280.0f, 720.0f };
        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;

        // FPS
        std::chrono::time_point<std::chrono::high_resolution_clock> m_LastFrameTime;
        float m_Fps = 0.0f;
        float m_FrameTime = 0.0f;
        float m_FpsAccumulator = 0.0f;
        int m_FrameCount = 0;

        SceneHierarchyPanel m_SceneHierarchyPanel;

        // IBL environment
        std::shared_ptr<EnvironmentImage> m_EnvironmentImage;
    };
}
