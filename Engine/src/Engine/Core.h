#pragma once
#define FMT_HEADER_ONLY

#ifdef EG_PLATFORM_WINDOWS
	#define ENGINE_API
#else
	#error Vulkan Engine only support Windows!
#endif

#ifdef EG_DEBUG
	#define EG_ENABLE_ASSERTS
#endif

#ifdef EG_ENABLE_ASSERTS
	#define EG_ASSERT(x, ... ) {if(!(x)) {EG_ERROR("Assertion Faild: {0}", __VA_ARGS__); __debugbreak();}}
	#define EG_CORE_ASSERT(x, ... ) {if(!(x)) {EG_ERROR("Assertion Faild: {0}", __VA_ARGS__); __debugbreak();}}
#else
	#define EG_ASSERT(x, ...)
	#define EG_CORE_ASSERT(x, ...)
#endif

#define BIT(x) (1 << x)

#define EG_BIND_EVENT_FN(fn) std::bind(&fn,this,std::placeholders::_1)

//vkQueueWaitIdle改成fence


//shader类实际对应vulkan的graphicpipeline