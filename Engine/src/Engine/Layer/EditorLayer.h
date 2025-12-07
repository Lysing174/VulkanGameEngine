#pragma once
#include "Engine/Layer/Layer.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Entity.h"
#include "Engine/Renderer/EditorCamera.h"

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

        EditorCamera GetEditorCamera() { return m_EditorCamera; }

    private:
        std::shared_ptr<Scene> m_ActiveScene; 
        Entity m_CubeEntity; 

        EditorCamera m_EditorCamera;

        std::shared_ptr<Mesh> m_CubeMesh;
        std::shared_ptr<Material> m_RedMaterial;

        glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };
    };
}