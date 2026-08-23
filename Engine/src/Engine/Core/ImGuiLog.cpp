#include "pch.h"
#include "ImGuiLog.h"

#include "imgui.h"

namespace Engine {

	void ImGuiLog::AddLog(const std::string& message, int level)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Lines.push_back({ message, level });
		if (m_Lines.size() > MaxLines)
			m_Lines.erase(m_Lines.begin(), m_Lines.begin() + (m_Lines.size() - MaxLines));
		m_ScrollToBottom = true;
	}

	void ImGuiLog::Draw(const char* title)
	{
		ImGui::Begin(title);

		// 顶部工具栏
		if (ImGui::Button("Clear"))
			Clear();
		ImGui::SameLine();
		ImGui::Text("(%zu lines)", m_Lines.size());
		ImGui::Separator();

		// 子窗口承载日志, 自动滚动到底部
		ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

		std::lock_guard<std::mutex> lock(m_Mutex);
		for (const auto& line : m_Lines)
		{
			// 按 spdlog 级别上色
			ImVec4 color;
			switch (line.Level)
			{
			case 1: color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f); break; // trace (spdlog level 1, 实际为 trace? spdlog: trace=0)
			case 0: color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f); break; // trace
			case 2: color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); break; // debug
			case 3: color = ImVec4(0.7f, 0.95f, 0.7f, 1.0f); break; // info
			case 4: color = ImVec4(0.95f, 0.85f, 0.4f, 1.0f); break; // warn
			case 5: color = ImVec4(1.0f, 0.45f, 0.45f, 1.0f); break; // error
			case 6: color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); break; // critical/fatal
			default: color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); break;
			}
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::TextUnformatted(line.Text.c_str());
			ImGui::PopStyleColor();
		}

		if (m_ScrollToBottom)
		{
			ImGui::SetScrollHereY(1.0f);
			m_ScrollToBottom = false;
		}

		ImGui::EndChild();
		ImGui::End();
	}

}
