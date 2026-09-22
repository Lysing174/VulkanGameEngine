#include "pch.h"
#include "EditorLayer.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Scene/Components.h" 
#include "Engine/Events/ApplicationEvent.h"
#include "Engine/Renderer/Model.h"
#include "Engine/Core/Application.h"
#include "Engine/ImGui/ImGuiLayer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "imgui.h"
#include "imgui_internal.h"

#include "Engine/Core/ImGuiLog.h"

#include <glm/gtc/type_ptr.hpp>

// glTF PBR model 路径统一由 ModelLibrary 按 models/ 之后的相对路径管理
// (见 EditorLayer::OnAttach 中的 HELMET_PATH / BUST_PATH)

namespace Engine {

    EditorLayer::EditorLayer()
        : Layer("EditorLayer")
    {
        
    }

    void EditorLayer::OnAttach()
    {
    	m_LastFrameTime = std::chrono::high_resolution_clock::now();
    	
        m_EditorCamera = EditorCamera(45.0f, 1.778f, 0.1f, 1000.0f);

        m_CubeMesh = std::make_shared<Mesh>(Mesh::CreateCube());
        auto shader = Renderer::GetShaderLibrary()->Get("Mesh.vert.spv");
        m_RedMaterial = std::make_shared<Material>(shader);
        m_RedMaterial->SetAlbedoColor(glm::vec4(0, 1, 1, 1));

        // 仅扫描模型库 (建立下拉框资产清单 + 持久化 .meta UUID)，不预加载二进制。
        // 实际模型在 SubmitMesh 时按 UUID 懒加载，避免启动把所有模型读进内存。
        auto* modelLibrary = Renderer::GetModelLibrary();
        modelLibrary->ScanDirectory("models");

        m_ActiveScene = std::make_shared<Scene>();

        m_CubeEntity = m_ActiveScene->CreateEntity("Red Cube");
        m_CubeEntity.AddComponent<MeshFilterComponent>();   // 空 UUID → 不渲染
    	m_CubeEntity.AddComponent<MeshRendererComponent>();

        // Set MSAA samples before creating pipeline (must match render pass sample count)
        shader->SetSamples(VulkanContext::Get()->GetMSAASamples());
        shader->CreatePipeline(m_CubeMesh->GetVertexBuffer()->GetLayout(), "Mesh");
        auto& transform = m_CubeEntity.GetComponent<TransformComponent>();

        // Shadow pass pipeline (方向光阴影贴图, 深度-only, 与 "Shadow" render pass 匹配)
        {
            auto shadowShader = Renderer::GetShaderLibrary()->Get("Shadow.vert.spv");
            shadowShader->CreatePipeline(m_CubeMesh->GetVertexBuffer()->GetLayout(), "Shadow");
        }
        transform.Translation = { 0.0f, 30.0f, 0.0f };
        transform.Scale = { 2.0f, 2.0f, 2.0f };

        // 模型来源用相对路径反查 ModelLibrary 中的 UUID (组件只存 UUID)
        const std::string HELMET_PATH = "helmat/models/DamagedHelmet/glTF-Embedded/DamagedHelmet.gltf";
        const std::string BUST_PATH   = "marble_bust/marble_bust_01_4k.gltf";
    	const std::string CUBE_PATH   = "helmat/models/Box/glTF-Embedded/Box.gltf";

        UUID helmetID = modelLibrary->FindIDByPath(HELMET_PATH);
        UUID bustID   = modelLibrary->FindIDByPath(BUST_PATH);
    	UUID cubeID   = modelLibrary->FindIDByPath(CUBE_PATH);

        Entity HelmetEntity = m_ActiveScene->CreateEntity("Helmet");
        HelmetEntity.AddComponent<MeshFilterComponent>(helmetID);
        HelmetEntity.AddComponent<MeshRendererComponent>(helmetID);
    	HelmetEntity.GetComponent<TransformComponent>().Translation={0.0f,0.0f,0.0f};
    	HelmetEntity.GetComponent<TransformComponent>().Scale={3.0f,3.0f,3.0f};
    	HelmetEntity.GetComponent<TransformComponent>().Rotation={0.0f,0.0f,0.0f};

    	Entity BustEntity = m_ActiveScene->CreateEntity("Bust");
    	BustEntity.AddComponent<MeshFilterComponent>(bustID);
    	BustEntity.AddComponent<MeshRendererComponent>(bustID);
    	BustEntity.GetComponent<TransformComponent>().Translation={4.0f,-3.0f,0.0f};
    	BustEntity.GetComponent<TransformComponent>().Scale={10.0f,10.0f,10.0f};
    	BustEntity.GetComponent<TransformComponent>().Rotation={0.0f,0.0f,0.0f};
 
    	Entity CubeEntity = m_ActiveScene->CreateEntity("Cube");
    	CubeEntity.AddComponent<MeshFilterComponent>(cubeID);
    	CubeEntity.AddComponent<MeshRendererComponent>(cubeID);
    	CubeEntity.GetComponent<TransformComponent>().Translation={0.0f,-3.5f,0.0f};
    	CubeEntity.GetComponent<TransformComponent>().Scale={15.0f,1.0f,15.0f};
    	CubeEntity.GetComponent<TransformComponent>().Rotation={0.0f,0.0f,0.0f};
 
    	
        Entity cameraEntity = m_ActiveScene->CreateEntity("Main Camera");
        cameraEntity.AddComponent<CameraComponent>();
        cameraEntity.GetComponent<TransformComponent>().Translation = { 0.0f, 2.0f, 5.0f };

        // Create a point light entity
        Entity directLightEntity = m_ActiveScene->CreateEntity("Direct Light");
        directLightEntity.AddComponent<DirectLightComponent>(
            glm::vec3(1.0f, 1.0f, 1.0f), 15.0f,true,0.001f);
        // Transform: 位置=阴影盒近端面中心, 旋转定光方向(+Z), Scale=阴影盒半长
        auto& dlTransform = directLightEntity.GetComponent<TransformComponent>();
        dlTransform.Translation = { 2.3f, 2.0f, 2.8f };
        dlTransform.Scale       = { 5.0f, 5.0f, 20.0f };
    	dlTransform.Rotation = { -2.2f, 0.0f, -3.14f };

        m_SceneHierarchyPanel = SceneHierarchyPanel(m_ActiveScene);

        // ============================================================
        // Create offscreen framebuffer for mesh pass, store in Renderer
        // ============================================================
        FramebufferSpecification fbSpec;
        fbSpec.Width = (uint32_t)m_ViewportSize.x;
        fbSpec.Height = (uint32_t)m_ViewportSize.y;
        fbSpec.Samples = VulkanContext::Get()->GetMSAASamples();
        fbSpec.RenderPass = VulkanContext::Get()->GetRenderPass("Mesh")->GetVkRenderPass();
        auto fb = Framebuffer::Create(fbSpec);
        Renderer::SetOffscreenFramebuffer(fb);

        // Initialize camera projection to match actual FBO dimensions
        m_EditorCamera.SetViewportSize((float)fbSpec.Width, (float)fbSpec.Height);

        // ============================================================
        // Initialize Environment Image (HDR → cubemap → mipmap → SH)
        // ============================================================
        m_EnvironmentImage = std::make_shared<EnvironmentImage>("HDRs/citrus_orchard_road_puresky.hdr");
        m_EnvironmentImage->Initialize();

        // Pass IBL data to VulkanContext (SH + prefiltered cubemap)
        {
            auto envMapInfo = m_EnvironmentImage->GetCubeMap()->GetDescriptorImageInfo();
            envMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            auto shData = m_EnvironmentImage->GetSHData();
            VulkanContext::Get()->SetEnvironmentMap(envMapInfo, shData);
        }

        // Setup skybox rendering
        {
            auto skyboxShader = Renderer::GetShaderLibrary()->Get("Skybox.vert.spv");
            auto skyboxMesh   = std::make_shared<Mesh>(Mesh::CreateCube());
            skyboxShader->SetSamples(VulkanContext::Get()->GetMSAASamples());
            skyboxShader->CreatePipeline(skyboxMesh->GetVertexBuffer()->GetLayout(), "Mesh");
            Renderer::SetSkyboxData(skyboxShader, skyboxMesh);
        }
    }

    void EditorLayer::InitDockspace()
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGuiID dockspace_id = ImGui::GetID("EditorDockSpace");

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

        // Layout: Left(sceneHierarchy+stats) | Center(scene) | Right(properties)
        ImGuiID dock_left;
        ImGuiID dock_main;
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.15f, &dock_left, &dock_main);
        ImGuiID dock_right;
        ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.176f, &dock_right, &dock_main);

        // Left column: top = Scene Hierarchy, bottom = Stats
        ImGuiID dock_bottom;
        ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.3f, &dock_bottom, &dock_main);

        ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_left);
        ImGui::DockBuilderDockWindow("Stats", dock_bottom);
        ImGui::DockBuilderDockWindow("Log", dock_bottom);
        ImGui::DockBuilderDockWindow("Properties", dock_right);
        ImGui::DockBuilderDockWindow("Scene", dock_main);

        ImGui::DockBuilderFinish(dockspace_id);
    }

    void EditorLayer::OnDetach() {}

	void EditorLayer::OnFixedUpdate()
    {
    	
    }
    void EditorLayer::OnUpdate()
    {
    	auto now = std::chrono::high_resolution_clock::now();
    	float dt = std::chrono::duration<float>(now - m_LastFrameTime).count();
    	m_LastFrameTime = now;

    	m_FrameTime = dt;
    	m_FpsAccumulator += dt;
    	m_FrameCount++;
    	if (m_FpsAccumulator >= 0.5f)
    	{
    		m_Fps = m_FrameCount / m_FpsAccumulator;
    		m_FrameCount = 0;
    		m_FpsAccumulator = 0.0f;
    	}

        m_EditorCamera.OnUpdate();

        // ============================================================
        // Resize offscreen FBO BEFORE rendering (must happen before 
        // command buffer recording, otherwise CB references destroyed images)
        // ============================================================
        auto fb = Renderer::GetOffscreenFramebuffer();
        if (fb)
        {
            uint32_t newW = (uint32_t)m_ViewportSize.x;
            uint32_t newH = (uint32_t)m_ViewportSize.y;
            if (newW > 0 && newH > 0 &&
                (fb->GetWidth() != newW || fb->GetHeight() != newH))
            {
                fb->Resize(newW, newH);
                m_EditorCamera.SetViewportSize((float)newW, (float)newH);
            }
        }

        // 3. 【核心调用】
        // 场景 A: 编辑器模式 (我们现在主要用这个)
        // 使用 EditorCamera 渲染，不运行物理模拟
        // 渲染生命周期由本 Layer 驱动：BeginScene → 收集 → RunMeshPass
        //Renderer::BeginScene(m_EditorCamera);
        m_ActiveScene->OnUpdateEditor(m_EditorCamera);  // 仅收集光源 / Mesh 数据
    	Renderer::EndShadowCollection(); // 依据各阴影光的范围盒结算光视矩阵
        //Renderer::RunMeshPass();                          // 录制 Mesh Pass

        // 场景 B: 游戏运行模式 (当你按下 Play 按钮时切换到这个)
        // 使用场景内的 CameraComponent 渲染，运行物理模拟和脚本
        // m_ActiveScene->OnUpdateRuntime(ts);
    }

    void EditorLayer::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& e) {
            if (e.GetWidth() <= 0 || e.GetHeight() <= 0) return false;

            // Estimate viewport size from previous frame's viewport/window ratio.
            // Dockspace layout keeps roughly the same proportions after resize,
            // so this avoids using raw window size (wrong aspect) for camera/FBO.
            float ratioX = (m_WindowSize.x > 0.0f) ? m_ViewportSize.x / m_WindowSize.x : 0.674f;
            float ratioY = (m_WindowSize.y > 0.0f) ? m_ViewportSize.y / m_WindowSize.y : 0.70f;
            uint32_t estW = (uint32_t)(e.GetWidth()  * ratioX);
            uint32_t estH = (uint32_t)(e.GetHeight() * ratioY);
            if (estW == 0) estW = 1;
            if (estH == 0) estH = 1;

            // Update window size tracking
            m_WindowSize = { (float)e.GetWidth(), (float)e.GetHeight() };

            // Resize offscreen FBO to estimated viewport size immediately
            auto fb = Renderer::GetOffscreenFramebuffer();
            if (fb && (fb->GetWidth() != estW || fb->GetHeight() != estH))
            {
                fb->Resize(estW, estH);
            }

            // Update camera projection + scene cameras with estimated viewport
            m_EditorCamera.SetViewportSize((float)estW, (float)estH);
            m_ActiveScene->OnViewportResize(estW, estH);

            // ImGui viewport will refine this in next OnImGuiRender()
            m_ViewportSize = { (float)estW, (float)estH };
            return false;
            });

        // gizmo 正在被拖拽时, 拦截鼠标/滚轮事件, 不让 EditorCamera 响应
        // (m_GizmoUsing 在 RenderSceneViewport 中于 Manipulate 之后更新)
        if (m_GizmoUsing && (e.GetEventType() == EventType::MouseMoved ||
                             e.GetEventType() == EventType::MouseScrolled))
            return;

        m_EditorCamera.OnEvent(e);
    }
    void EditorLayer::OnImGuiRender()
    {
		RenderDockspaceAndMenu();
		RenderSceneHierarchy();
		RenderStats();
		RenderLog();
		RenderSceneViewport();
	}

	void EditorLayer::RenderDockspaceAndMenu()
	{
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("Editor", &m_DockspaceOpen, window_flags);
		ImGui::PopStyleVar(3);

		// ---- Dockspace ----
		ImGuiID dockspace_id = ImGui::GetID("EditorDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    	// 一次性初始化 dockspace 布局 (懒初始化, 确保 ImGui 上下文已就绪)
    	if (!m_DockspaceInitialized)
    	{
    		InitDockspace();
    		m_DockspaceInitialized = true;
    	}
    	
		// ---- Menu Bar ----
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Open Project...", "Ctrl+O")) {}
				ImGui::Separator();
				if (ImGui::MenuItem("New Scene", "Ctrl+N")) {}
				if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {}
				if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {}
				ImGui::Separator();
				if (ImGui::MenuItem("Exit")) {}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Script"))
			{
				if (ImGui::MenuItem("Reload assembly", "Ctrl+R")) {}
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}
	}

	void EditorLayer::RenderSceneHierarchy()
	{
		m_SceneHierarchyPanel.OnImGuiRender();
	}

	void EditorLayer::RenderStats()
	{
		ImGui::Begin("Stats");
		ImGui::Text("FPS: %.1f", m_Fps);
		ImGui::Text("Frame Time: %.2f ms", m_FrameTime * 1000.0f);
		ImGui::Separator();
		ImGui::Text("Renderer Stats: (not ready yet)");
		ImGui::End();
	}

	void EditorLayer::RenderLog()
	{
		ImGuiLog::Get().Draw("Log");
	}

	void EditorLayer::RenderSceneViewport()
	{
		// ---- Scene Viewport ----
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
		ImGui::Begin("Scene");

		m_ViewportFocused = ImGui::IsWindowFocused();
		m_ViewportHovered = ImGui::IsWindowHovered();

		// 当 Scene viewport 被 hovered 或 focused 时，允许鼠标/键盘事件穿透到下层供相机旋转等操作
		Application::Get().GetImGuiLayer()->SetBlockEvents(!m_ViewportHovered && !m_ViewportFocused);

		ImVec2 viewportSize = ImGui::GetContentRegionAvail();
		m_ViewportSize = { viewportSize.x, viewportSize.y };
		// 视口像素矩形左上角: 窗口位置 + 内容区偏移(dock 边框/标题栏/padding)
		// 给 gizmo SetRect 用, 否则命中测试区域与视觉轴错位 (IsOver 恒为 false)
		ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
		ImVec2 sceneWindowPos = ImGui::GetWindowPos();
		sceneWindowPos.x += contentMin.x;
		sceneWindowPos.y += contentMin.y;
    	
		// Track window size for aspect-ratio estimation in WindowResizeEvent
		ImVec2 winSize = ImGui::GetWindowSize();
		m_WindowSize = { winSize.x, winSize.y };

		// Display offscreen framebuffer texture in the Scene viewport
		auto fb = Renderer::GetOffscreenFramebuffer();
		if (fb)
		{
			uint64_t texID = fb->GetColorAttachmentRendererID();
			ImGui::Image((ImTextureID)texID, viewportSize, ImVec2{0, 0}, ImVec2{1, 1});
		}

		// ---- Gizmo (绘在 Scene 视口之上) ----
		// 必须在 Scene 窗口内调用, 这样 gizmo 才会画进当前窗口的 drawlist 并叠在图像之上
		// 必须每帧调用: 初始化/衔接 gizmo 内部状态, 否则 IsOver/IsUsing 恒错甚至永久卡死
		ImGuizmo::BeginFrame();
		ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
		ImGuizmo::SetRect(sceneWindowPos.x, sceneWindowPos.y,
		                  m_ViewportSize.x, m_ViewportSize.y);

		// 模式切换热键 (仅在视口聚焦时生效)
		if (m_ViewportFocused)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_T)) m_GizmoOperation = ImGuizmo::TRANSLATE;
			if (ImGui::IsKeyPressed(ImGuiKey_R)) m_GizmoOperation = ImGuizmo::ROTATE;
			if (ImGui::IsKeyPressed(ImGuiKey_E)) m_GizmoOperation = ImGuizmo::SCALE;
			if (ImGui::IsKeyPressed(ImGuiKey_W)) m_GizmoMode = (m_GizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
		}

		Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
    	if (selectedEntity)
		{
			// 相机矩阵 (EditorCamera 的 projection 是 GLM OpenGL 风格, Z∈[-1,1],
			// 与 ImGuizmo 约定一致, 无需 Vulkan Z-flip)
			glm::mat4 view = m_EditorCamera.GetViewMatrix();
			glm::mat4 projection = m_EditorCamera.GetProjection();
			glm::mat4 model = selectedEntity.GetComponent<TransformComponent>().GetTransform();

			// 吸附步进 (按住 Ctrl 时启用)
			float snapTranslation[3] = { 0.5f, 0.5f, 0.5f };
			float snapRotation[3]    = { 15.0f, 15.0f, 15.0f };
			float snapScale[3]       = { 0.1f, 0.1f, 0.1f };
			float* snap = nullptr;
			if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
			{
				if (m_GizmoOperation == ImGuizmo::TRANSLATE) snap = snapTranslation;
				else if (m_GizmoOperation == ImGuizmo::ROTATE) snap = snapRotation;
				else if (m_GizmoOperation == ImGuizmo::SCALE) snap = snapScale;
			}
		if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),

			                         m_GizmoOperation, m_GizmoMode,
			                         glm::value_ptr(model), nullptr, snap))
			{
				// 把 gizmo 修改后的 model 矩阵分解回 TransformComponent
				glm::vec3 translation, rotationEuler, scale;
				ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model),
					glm::value_ptr(translation), glm::value_ptr(rotationEuler), glm::value_ptr(scale));
				auto& tc = selectedEntity.GetComponent<TransformComponent>();
				tc.Translation = translation;
				tc.Rotation = glm::radians(rotationEuler); // Decompose 返回角度(度)
				tc.Scale = scale;
			}
		}

		// 记录本帧 gizmo 是否正在被拖拽, 供 OnEvent 拦截相机输入
		m_GizmoUsing = ImGuizmo::IsUsing();

		ImGui::End();
		ImGui::PopStyleVar();

		ImGui::End(); // Editor dock space
	}
}
