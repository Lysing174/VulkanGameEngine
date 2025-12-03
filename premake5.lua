workspace "VulkanEngine"
	architecture "x64"

	configurations
	{
		"Debug",
		"Release",
		"Dist"
	}
	startproject "SandBox"
	buildoptions { "/utf-8" }


outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

include "vendor/bin/imgui"

project "Engine"
	location "Engine"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	-- staticruntime "On"


	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
	pchheader "pch.h"
	pchsource "Engine/src/pch.cpp"


	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp"
	}

	includedirs
	{
		"ThirdParty/Include",
		"Engine/src",
		"vendor/bin/imgui",
		"D:/program/vulkanSDK1.4.304.1/Include",
		"D:/program/vulkanSDK1.4.304.1/Third-Party/Include"
	}
	libdirs
	{
		"D:/program/vulkanSDK1.4.304.1/Lib",
		"D:/program/vulkanSDK1.4.304.1/Third-Party/Lib"
	}
	links {
        "glfw3.lib",
		"ImGui",
		"vulkan-1.lib"
    }
	filter "system:windows"
		systemversion "latest"

		defines
		{
			"EG_PLATFORM_WINDOWS",
			"GLFW_INCLUDE_NONE"
		}


	filter "configurations:Debug"
		defines "EG_DEBUG"
		buildoptions "/MDd"
		symbols "on"

	filter "configurations:Release"
		defines "EG_RELEASE"
		buildoptions "/MD"
		optimize "on"

	filter "configurations:Dist"
		defines "EG_DIST"
		buildoptions "/MD"
		optimize "on"

project "SandBox"
	location "SandBox"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
	-- staticruntime "on"


	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp"
	}

	includedirs
	{
		"ThirdParty/Include",
		"Engine/src",
		"vendor/bin/imgui",
		"D:/program/vulkanSDK1.4.304.1/Include",
		"D:/program/vulkanSDK1.4.304.1/Third-Party/Include"
	}
	libdirs
	{
		"D:/program/vulkanSDK1.4.304.1/Lib",
		"D:/program/vulkanSDK1.4.304.1/Third-Party/Lib"
	}

	links
	{
		"ImGui",
		"Engine"
	}

	filter "system:windows"
		systemversion "latest"

		defines
		{
			"EG_PLATFORM_WINDOWS"
		}

	filter "configurations:Debug"
		defines "EG_DEBUG"
		buildoptions "/MDd"
		symbols "on"

	filter "configurations:Release"
		defines "EG_RELEASE"
		buildoptions "/MD"
		optimize "on"

	filter "configurations:Dist"
		defines "EG_DIST"
		buildoptions "/MD"
		optimize "on"