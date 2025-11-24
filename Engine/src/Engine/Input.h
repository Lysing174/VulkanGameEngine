#pragma once
#pragma once


#include <glm/glm.hpp>

namespace Engine {

	class Input
	{
	public:
		inline static bool IsKeyPressed(int keycode) { return s_Instance->IsKeyPressedImpl(keycode); }
		inline static bool IsMouseButtonPressed(int button) { return s_Instance->IsMouseButtonPressedImpl(button); }

		inline static glm::vec2 GetMousePosition() { return s_Instance->GetMousePositionImpl(); }
		inline static float GetMouseX() { return s_Instance->GetMousePositionImpl().x; }
		inline static float GetMouseY() { return s_Instance->GetMousePositionImpl().y; }

	protected:
		virtual bool IsKeyPressedImpl(int keycode) = 0;
		virtual bool IsMouseButtonPressedImpl(int button) = 0;
		virtual glm::vec2 GetMousePositionImpl() = 0;
	private:
		static Input* s_Instance;
	};
}