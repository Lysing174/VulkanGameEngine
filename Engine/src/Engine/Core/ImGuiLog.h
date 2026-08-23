#pragma once

#include <string>
#include <vector>
#include <mutex>

namespace Engine {

	// 收集 spdlog 日志并在 ImGui 中显示的单例
	class ImGuiLog
	{
	public:
		static ImGuiLog& Get()
		{
			static ImGuiLog instance;
			return instance;
		}

		// 由 spdlog sink 调用, 线程安全
		void AddLog(const std::string& message, int level);

		// 在 ImGui 中绘制日志窗口 (每帧调用)
		void Draw(const char* title = "Log");

		void Clear() { std::lock_guard<std::mutex> lock(m_Mutex); m_Lines.clear(); m_ScrollToBottom = true; }

	private:
		ImGuiLog() = default;
		ImGuiLog(const ImGuiLog&) = delete;
		ImGuiLog& operator=(const ImGuiLog&) = delete;

		struct Line
		{
			std::string Text;
			int Level; // spdlog level
		};

		std::vector<Line> m_Lines;
		std::mutex m_Mutex;
		bool m_ScrollToBottom = true;
		static const size_t MaxLines = 2000; // 上限, 防止无限增长
	};

}
