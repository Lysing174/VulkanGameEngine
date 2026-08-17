#pragma once

#include "Engine/Events/Event.h"

#include <sstream>

namespace Engine {

	struct WindowProps
	{
		std::string Title;
		uint32_t Width;
		uint32_t Height;
		
		bool Fullscreen = false;

		WindowProps(const std::string& title = "Vulkan Engine",
			uint32_t width = 1920,
			uint32_t height = 1080,
			bool fullscreen = false)
			: Title(title), Width(width), Height(height), Fullscreen(fullscreen)
		{
		}
	};

	// Interface representing a desktop system based Window
	class Window
	{
	public:
		using EventCallbackFn = std::function<void(Event&)>;

		virtual ~Window() = default;

		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;

		// Window attributes
		virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;

		virtual void* GetNativeWindow() const = 0;
		virtual class GraphicsContext* GetContext() const { return m_Context; }

		static Window* Create(const WindowProps& props = WindowProps());

	protected:
		GraphicsContext* m_Context;

	};

}