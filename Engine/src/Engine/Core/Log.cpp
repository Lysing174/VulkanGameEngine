#include "pch.h"
#include "Log.h"
#include "ImGuiLog.h"

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/base_sink.h"

#include <mutex>

namespace Engine {

	// 自定义 spdlog sink: 把每条日志转发到 ImGuiLog 单例
	template<typename Mutex>
	class ImGuiLogSink : public spdlog::sinks::base_sink<Mutex>
	{
	protected:
		void sink_it_(const spdlog::details::log_msg& msg) override
		{
			// 格式化消息 (使用 logger 的 pattern 之外, 这里简单取 payload)
			spdlog::memory_buf_t formatted;
			spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
			std::string text = fmt::to_string(formatted);
			ImGuiLog::Get().AddLog(text, static_cast<int>(msg.level));
		}

		void flush_() override {}
	};

	using ImGuiLogSink_mt = ImGuiLogSink<std::mutex>;
	using ImGuiLogSink_st = ImGuiLogSink<spdlog::details::null_mutex>;

	std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

	void Log::Init()
	{
		spdlog::set_pattern("%^[%T] %n: %v%$");
		s_CoreLogger = spdlog::stdout_color_mt("ENGINE");
		s_CoreLogger->set_level(spdlog::level::trace);

		s_ClientLogger = spdlog::stdout_color_mt("APP");
		s_ClientLogger->set_level(spdlog::level::trace);

		// 把日志同时转发到 ImGui Log 面板
		auto imguiSink = std::make_shared<ImGuiLogSink_mt>();
		imguiSink->set_pattern("%^[%T] %n: %v%$");
		s_CoreLogger->sinks().push_back(imguiSink);
		s_ClientLogger->sinks().push_back(imguiSink);
	}
}
