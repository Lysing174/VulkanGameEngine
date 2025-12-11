#pragma once
#include "Engine/Layer/Layer.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Entity.h"
#include "Engine/Renderer/EditorCamera.h"
#include "Engine/Renderer/FrameBuffer.h"
#include "Editor/SceneHierarchyPanel.h"

namespace Engine {

    class EditorLayer : public Layer
    {
    public:
        EditorLayer();
        virtual ~EditorLayer() = default;

        virtual void OnAttach() override; 
        virtual void OnDetach() override; 
        virtual void OnUpdate() override; 
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
        std::shared_ptr<Framebuffer> m_Framebuffer;
        EditorCamera m_EditorCamera;

        Entity m_CubeEntity; 
        std::shared_ptr<Mesh> m_CubeMesh;
        std::shared_ptr<Material> m_RedMaterial;

        glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };


        SceneHierarchyPanel m_SceneHierarchyPanel;
    };
}