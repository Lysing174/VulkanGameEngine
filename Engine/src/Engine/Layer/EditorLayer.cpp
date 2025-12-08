#include "pch.h"
#include "EditorLayer.h"
#include "Engine/Scene/Components.h" 
#include "Engine/Events/ApplicationEvent.h"
#include "Engine/Renderer/Model.h"
const std::string MODEL_PATH = "models/cottage_obj.obj";
const std::string TEXTURE_PATH = "textures/cottage_diffuse.png";

namespace Engine {

    EditorLayer::EditorLayer()
        : Layer("EditorLayer")
    {
    }

    void EditorLayer::OnAttach()
    {
        m_EditorCamera = EditorCamera(45.0f, 1.778f, 0.1f, 1000.0f);

        m_CubeMesh = std::make_shared<Mesh>(Mesh::CreateCube());
        auto shader = Shader::Create("shaders/Mesh.vert.spv", "shaders/Mesh.frag.spv");
        m_RedMaterial = std::make_shared<Material>(shader);
        m_RedMaterial->SetColor(glm::vec4(0, 1, 1, 1));

        m_ActiveScene = std::make_shared<Scene>();

        m_CubeEntity = m_ActiveScene->CreateEntity("Red Cube");
        m_CubeEntity.AddComponent<MeshFilterComponent>(m_CubeMesh);
        m_CubeEntity.AddComponent<MeshRendererComponent>(m_RedMaterial);
        shader->CreatePipeline(m_CubeMesh->GetVertexBuffer()->GetLayout());
        auto& transform = m_CubeEntity.GetComponent<TransformComponent>();
        transform.Translation = { 0.0f, 30.0f, 0.0f };
        transform.Scale = { 2.0f, 2.0f, 2.0f };

        Entity houseEntity = m_ActiveScene->CreateEntity("House");
        Model houseModel = Model(MODEL_PATH, shader);
        houseEntity.AddComponent<MeshFilterComponent>(houseModel.GetMesh());
        houseEntity.AddComponent<MeshRendererComponent>(houseModel.GetMaterials());

        Entity cameraEntity = m_ActiveScene->CreateEntity("Main Camera");
        cameraEntity.AddComponent<CameraComponent>();
        cameraEntity.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 5.0f };
    }

    void EditorLayer::OnDetach() {}

    void EditorLayer::OnUpdate()
    {
        // 1. 如果视口大小变了，通知 Scene (处理 ImGui 窗口缩放的情况)
        // if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f && ...变了...) {
        //     m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        //     m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
        // }

        m_EditorCamera.OnUpdate();

        // 3. 【核心调用】
        // 场景 A: 编辑器模式 (我们现在主要用这个)
        // 使用 EditorCamera 渲染，不运行物理模拟
        m_ActiveScene->OnUpdateEditor(m_EditorCamera);

        // 场景 B: 游戏运行模式 (当你按下 Play 按钮时切换到这个)
        // 使用场景内的 CameraComponent 渲染，运行物理模拟和脚本
        // m_ActiveScene->OnUpdateRuntime(ts);
    }

    void EditorLayer::OnEvent(Event& e)
    {
        // 如果不用 ImGui，而直接用窗口，就在这里处理 Resize
        m_EditorCamera.OnEvent(e); // 处理滚轮缩放

        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& e) {
            m_ActiveScene->OnViewportResize(e.GetWidth(), e.GetHeight());
            m_EditorCamera.SetViewportSize((float)e.GetWidth(), (float)e.GetHeight());
            return false;
            });
    }
}