#include "pch.h"
#include "ImGuiLayer.h"
#include "imgui.h"
#include "Platform/Vulkan/imgui_impl_vulkan.h"

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
	
		//ImGui_ImplVulkan_Init();
	}
	void ImGuiLayer::OnDetach()
	{
	}
	void ImGuiLayer::OnUpdate()
	{

	}
	void ImGuiLayer::OnEvent(Event& event)
	{
	}
}