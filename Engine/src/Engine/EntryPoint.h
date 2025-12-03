#pragma once
#include "imgui.h"
#include "Engine/ImGui/ImGuiLayer.h"
#include "Engine/Application.h"
#include "Engine/Log.h"

#ifdef EG_PLATFORM_WINDOWS

extern Engine::Application* Engine::CreateApplication();

int main(int argc,char** argv) 
{
	Engine::Log::Init();
	auto* app = Engine::CreateApplication();
	app->Run();
	delete app;
}

#endif