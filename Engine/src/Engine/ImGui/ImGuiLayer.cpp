#include "pch.h"
#include "ImGuiLayer.h"
#include "imgui.h"
#include "Engine/Application.h"
#include "Platform/Vulkan/VulkanContext.h" // 必须引用

namespace Engine {
	ImGuiLayer::ImGuiLayer()
		:Layer("ImGuiLayer")
	{

	}
	ImGuiLayer::~ImGuiLayer()
	{
	}

    void ImGuiLayer::OnAttach()
    {
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGuiIO& io = ImGui::GetIO();
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

        Application& app = Application::Get();
        GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());

        // 1. 初始化 GLFW 后端 (Vulkan 模式)
        ImGui_ImplGlfw_InitForVulkan(window, true);

        // 2. 获取 VulkanContext
        auto* context = static_cast<VulkanContext*>(app.GetWindow().GetContext());

        // 3. 填充初始化结构体 (这是最麻烦的一步)
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = context->GetInstance();
        init_info.PhysicalDevice = context->GetPhysicalDevice();
        init_info.Device = context->GetDevice();
        init_info.QueueFamily = 0;//context->GetGraphicsQueueFamilyIndex(); // 你需要确保有这个函数，或者写死 0
        init_info.Queue = context->GetGraphicsQueue();
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = context->GetDescriptorPool();
        init_info.MinImageCount = context->GetMinImageCount();
        init_info.ImageCount = context->GetImageCount();
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = nullptr;
        init_info.PipelineInfoMain.RenderPass = context->GetRenderPass();
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.PipelineInfoMain.Subpass = 0;

        // 4. 初始化 Vulkan 后端
        ImGui_ImplVulkan_Init(&init_info);

        // 5. 上传字体 (Vulkan 必须手动做这步，还要用 CommandBuffer)
        // 简单起见，我们可以借用 context 里的辅助函数，或者在这里临时搞一个
        // 这里为了代码简洁，建议在 VulkanContext 里加一个 UploadFonts 函数，或者暂时不写，字体可能会崩。
        // 正规写法需要 begin command buffer -> create fonts texture -> end -> submit -> wait idle
        {
            //VkCommandBuffer command_buffer = context->beginSingleTimeCommands(); // 你 1700 行代码里应该有这个辅助函数
            //ImGui_ImplVulkan_CreateFontsTexture(command_buffer);
            //context->endSingleTimeCommands(command_buffer);
            //ImGui_ImplVulkan_DestroyFontUploadObjects();
        }
    }
	void ImGuiLayer::OnDetach()
	{
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
	}
	void ImGuiLayer::OnUpdate()
	{

	}
	void ImGuiLayer::OnEvent(Event& event)
	{
	}
    void ImGuiLayer::Begin()
    {
        ImGui_ImplVulkan_NewFrame(); // 告诉 Vulkan 后端：新的一帧
        ImGui_ImplGlfw_NewFrame();   // 告诉 GLFW 后端：读取输入
        ImGui::NewFrame();           // 告诉 ImGui 核心：开始逻辑计算
    }
    void ImGuiLayer::End()
    {
        // 1. 根据 ImGuiLayer::OnImGuiRender 里写的逻辑，计算顶点数据
        ImGui::Render();

        // 注意：这里不再调用 RenderDrawData 了！
        // 绘制操作推迟到了 VulkanContext::SwapBuffers 里的 RenderPass 内部

        // 2. 处理拖出主窗口的那些浮动 UI (Docking/Viewports)
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            // 这一步是必须的，否则浮动窗口会黑屏或不更新
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    ImGuiContext* ImGuiLayer::GetContext()
    {
        return ImGui::GetCurrentContext();
    }
}