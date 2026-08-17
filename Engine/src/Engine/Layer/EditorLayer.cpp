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

// glTF PBR model paths (replace with your own .gltf/.glb files)
const std::string NANOSUIT_PATH = "models/nanosuit/nanosuit.gltf";
//const std::string MODEL_PATH = "models/pbr_porche/scene.gltf";

const std::string MODEL_PATH = "models/helmat/models/DamagedHelmet/glTF-Embedded/DamagedHelmet.gltf";
//const std::string TEXTURE_PATH = "models/cottage_diffuse.png";

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

        m_ActiveScene = std::make_shared<Scene>();

        m_CubeEntity = m_ActiveScene->CreateEntity("Red Cube");
        m_CubeEntity.AddComponent<MeshFilterComponent>(m_CubeMesh);
        m_CubeEntity.AddComponent<MeshRendererComponent>(m_RedMaterial);

        // Set MSAA samples before creating pipeline (must match render pass sample count)
        shader->SetSamples(VulkanContext::Get()->GetMSAASamples());
        shader->CreatePipeline(m_CubeMesh->GetVertexBuffer()->GetLayout(), "Mesh");
        auto& transform = m_CubeEntity.GetComponent<TransformComponent>();
        transform.Translation = { 0.0f, 30.0f, 0.0f };
        transform.Scale = { 2.0f, 2.0f, 2.0f };

		// 创建带纹理的材质
		//std::shared_ptr<Material> houseMat = std::make_shared<Material>(shader);
		//houseMat->SetTexture("u_AlbedoMap", Texture2D::Create(TEXTURE_PATH));

        Entity HelmetEntity = m_ActiveScene->CreateEntity("Helmet");
        Model helmetModel = Model(MODEL_PATH, shader);
        HelmetEntity.AddComponent<MeshFilterComponent>(helmetModel.GetMesh());

        // 使用 Model 加载的材质，如果模型没有材质则使用默认的 houseMat
        auto& helmetMaterials = helmetModel.GetMaterials();
		HelmetEntity.AddComponent<MeshRendererComponent>(helmetMaterials);
    	HelmetEntity.GetComponent<TransformComponent>().Translation={0.0f,0.0f,0.0f};
    	HelmetEntity.GetComponent<TransformComponent>().Scale={3.0f,3.0f,3.0f};
    	HelmetEntity.GetComponent<TransformComponent>().Rotation={0.0f,0.0f,0.0f};
    	
    	Entity BustEntity = m_ActiveScene->CreateEntity("Bust");
    	Model bustModel = Model("models/marble_bust/marble_bust_01_4k.gltf", shader);
    	BustEntity.AddComponent<MeshFilterComponent>(bustModel.GetMesh());

    	// 使用 Model 加载的材质，如果模型没有材质则使用默认的 houseMat
    	auto& bustMaterials = bustModel.GetMaterials();
    	BustEntity.AddComponent<MeshRendererComponent>(bustMaterials);
    	BustEntity.GetComponent<TransformComponent>().Translation={4.0f,-3.0f,0.0f};
    	BustEntity.GetComponent<TransformComponent>().Scale={10.0f,10.0f,10.0f};
    	BustEntity.GetComponent<TransformComponent>().Rotation={0.0f,0.0f,0.0f};
 
    	
        Entity cameraEntity = m_ActiveScene->CreateEntity("Main Camera");
        cameraEntity.AddComponent<CameraComponent>();
        cameraEntity.GetComponent<TransformComponent>().Translation = { 0.0f, 2.0f, 5.0f };

        // Create a point light entity
        Entity directLightEntity = m_ActiveScene->CreateEntity("Direct Light");
        directLightEntity.AddComponent<DirectLightComponent>(
            glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), 10.0f);
        directLightEntity.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, -5.0f };

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
        m_ActiveScene->OnUpdateEditor(m_EditorCamera);

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

        m_EditorCamera.OnEvent(e);
    }
    void EditorLayer::OnImGuiRender()
    {
		static bool dockspaceOpen = true;
		static bool firstFrame = true;

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
		ImGui::Begin("Editor", &dockspaceOpen, window_flags);
		ImGui::PopStyleVar(3);

		// ---- Dockspace ----
		ImGuiID dockspace_id = ImGui::GetID("EditorDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		if (firstFrame)
		{
			firstFrame = false;

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
			ImGui::DockBuilderDockWindow("Properties", dock_right);
			ImGui::DockBuilderDockWindow("Scene", dock_main);

			ImGui::DockBuilderFinish(dockspace_id);
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

		// ---- Dockable Windows ----
		m_SceneHierarchyPanel.OnImGuiRender();

		ImGui::Begin("Stats");
		ImGui::Text("FPS: %.1f", m_Fps);
		ImGui::Text("Frame Time: %.2f ms", m_FrameTime * 1000.0f);
		ImGui::Separator();
		ImGui::Text("Renderer Stats: (not ready yet)");
		ImGui::End();

		// ---- Scene Viewport ----
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
		ImGui::Begin("Scene");

		m_ViewportFocused = ImGui::IsWindowFocused();
		m_ViewportHovered = ImGui::IsWindowHovered();

		// 当 Scene viewport 被 hovered 或 focused 时，允许鼠标/键盘事件穿透到下层供相机旋转等操作
		Application::Get().GetImGuiLayer()->SetBlockEvents(!m_ViewportHovered && !m_ViewportFocused);

		ImVec2 viewportSize = ImGui::GetContentRegionAvail();
		m_ViewportSize = { viewportSize.x, viewportSize.y };

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

		ImGui::End();
		ImGui::PopStyleVar();

		ImGui::End(); // Editor dock space
    }
}
