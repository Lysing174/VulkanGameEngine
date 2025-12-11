#include "pch.h"
#include "EditorLayer.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Scene/Components.h" 
#include "Engine/Events/ApplicationEvent.h"
#include "Engine/Renderer/Model.h"
#include "imgui.h"

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
        auto shader = Renderer::GetShaderLibrary()->Get("Mesh.vert.spv");
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

        m_SceneHierarchyPanel = SceneHierarchyPanel(m_ActiveScene);
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
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& e) {
            if (e.GetWidth() <= 0 || e.GetHeight() <= 0)return false;

            m_ActiveScene->OnViewportResize(e.GetWidth(), e.GetHeight());
            m_EditorCamera.SetViewportSize((float)e.GetWidth(), (float)e.GetHeight());
            return false;
            });

        m_EditorCamera.OnEvent(e);
    }
    void EditorLayer::OnImGuiRender()
    {
		//// Note: Switch this to true to enable dockspace
		//static bool dockspaceOpen = false;
		//static bool opt_fullscreen_persistant = true;
		//bool opt_fullscreen = opt_fullscreen_persistant;
		//static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

		//// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
		//// because it would be confusing to have two docking targets within each others.
		//ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		//if (opt_fullscreen)
		//{
		//	ImGuiViewport* viewport = ImGui::GetMainViewport();
		//	ImGui::SetNextWindowPos(viewport->Pos);
		//	ImGui::SetNextWindowSize(viewport->Size);
		//	ImGui::SetNextWindowViewport(viewport->ID);
		//	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		//	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		//	window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		//	window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		//}

		//// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background and handle the pass-thru hole, so we ask Begin() to not render a background.
		//if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
		//	window_flags |= ImGuiWindowFlags_NoBackground;

		//// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
		//// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive, 
		//// all active windows docked into it will lose their parent and become undocked.
		//// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise 
		//// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
		//ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		//ImGui::Begin("DockSpace Demo", &dockspaceOpen, window_flags);
		//ImGui::PopStyleVar();

		//if (opt_fullscreen)
		//	ImGui::PopStyleVar(2);

		//// DockSpace
		//ImGuiIO& io = ImGui::GetIO();
		//ImGuiStyle& style = ImGui::GetStyle();
		//float minWinSizeX = style.WindowMinSize.x;
		//style.WindowMinSize.x = 370.0f;
		//if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		//{
		//	ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		//	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
		//}

		//style.WindowMinSize.x = minWinSizeX;

		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Open Project...", "Ctrl+O"))
				{
					//OpenProject();
				}

				ImGui::Separator();

				if (ImGui::MenuItem("New Scene", "Ctrl+N"))
				{
					//NewScene();
				}

				if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
				{
					//SaveScene();
				}

				if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
				{
					//SaveSceneAs();
				}

				ImGui::Separator();

				if (ImGui::MenuItem("Exit"))
				{
					//Application::Get().Close();
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Script"))
			{
				if (ImGui::MenuItem("Reload assembly", "Ctrl+R")) 
				{
					//ScriptEngine::ReloadAssembly();
				}

				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}

		m_SceneHierarchyPanel.OnImGuiRender();
		//m_ContentBrowserPanel->OnImGuiRender();

		ImGui::Begin("Stats");

#if 0
		std::string name = "None";
		if (m_HoveredEntity)
			name = m_HoveredEntity.GetComponent<TagComponent>().Tag;
		ImGui::Text("Hovered Entity: %s", name.c_str());
#endif

		//auto stats = Renderer2D::GetStats();
		ImGui::Text("Renderer Stats:( not ready yet)");
		//ImGui::Text("Draw Calls: %d", stats.DrawCalls);
		//ImGui::Text("Quads: %d", stats.QuadCount);
		//ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
		//ImGui::Text("Indices: %d", stats.GetTotalIndexCount());

		ImGui::End();

		ImGui::Begin("Settings( not ready yet)");
		//ImGui::Checkbox("Show physics colliders", &m_ShowPhysicsColliders);

		//ImGui::Image((ImTextureID)s_Font->GetAtlasTexture()->GetRendererID(), { 512,512 }, { 0, 1 }, { 1, 0 });


		ImGui::End();

		//ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
		//ImGui::Begin("Viewport");
		//auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
		//auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
		//auto viewportOffset = ImGui::GetWindowPos();
		//m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
		//m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };

		//m_ViewportFocused = ImGui::IsWindowFocused();
		//m_ViewportHovered = ImGui::IsWindowHovered();

		//Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportHovered);

		//ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
		//m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

		//uint64_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
		//ImGui::Image(reinterpret_cast<void*>(textureID), ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

		//if (ImGui::BeginDragDropTarget())
		//{
		//	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
		//	{
		//		const wchar_t* path = (const wchar_t*)payload->Data;
		//		OpenScene(path);
		//	}
		//	ImGui::EndDragDropTarget();
		//}

		//// Gizmos
		//Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
		//if (selectedEntity && m_GizmoType != -1)
		//{
		//	ImGuizmo::SetOrthographic(false);
		//	ImGuizmo::SetDrawlist();

		//	ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y, m_ViewportBounds[1].x - m_ViewportBounds[0].x, m_ViewportBounds[1].y - m_ViewportBounds[0].y);

		//	// Camera

		//	// Runtime camera from entity
		//	// auto cameraEntity = m_ActiveScene->GetPrimaryCameraEntity();
		//	// const auto& camera = cameraEntity.GetComponent<CameraComponent>().Camera;
		//	// const glm::mat4& cameraProjection = camera.GetProjection();
		//	// glm::mat4 cameraView = glm::inverse(cameraEntity.GetComponent<TransformComponent>().GetTransform());

		//	// Editor camera
		//	const glm::mat4& cameraProjection = m_EditorCamera.GetProjection();
		//	glm::mat4 cameraView = m_EditorCamera.GetViewMatrix();

		//	// Entity transform
		//	auto& tc = selectedEntity.GetComponent<TransformComponent>();
		//	glm::mat4 transform = tc.GetTransform();

		//	// Snapping
		//	bool snap = Input::IsKeyPressed(Key::LeftControl);
		//	float snapValue = 0.5f; // Snap to 0.5m for translation/scale
		//	// Snap to 45 degrees for rotation
		//	if (m_GizmoType == ImGuizmo::OPERATION::ROTATE)
		//		snapValue = 45.0f;

		//	float snapValues[3] = { snapValue, snapValue, snapValue };

		//	ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
		//		(ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::LOCAL, glm::value_ptr(transform),
		//		nullptr, snap ? snapValues : nullptr);

		//	if (ImGuizmo::IsUsing())
		//	{
		//		glm::vec3 translation, rotation, scale;
		//		Math::DecomposeTransform(transform, translation, rotation, scale);

		//		glm::vec3 deltaRotation = rotation - tc.Rotation;
		//		tc.Translation = translation;
		//		tc.Rotation += deltaRotation;
		//		tc.Scale = scale;
		//	}
		//}


		//ImGui::End();
		//ImGui::PopStyleVar();

		//UI_Toolbar();

		//ImGui::End();

    }
}