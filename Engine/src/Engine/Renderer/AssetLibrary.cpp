#include "pch.h"
#include "Engine/Renderer/AssetLibrary.h"
#include "Engine/Renderer/Renderer.h"

#include <filesystem>

namespace Engine {

	// 从磁盘路径构造 Model：Model 需要 shader 创建 pipeline，使用默认 PBR shader
	template<>
	std::shared_ptr<Model> AssetCreate<Model>(const std::string& fullPath)
	{
		auto shader = Renderer::GetShaderLibrary()->Get("Mesh.vert.spv");
		EG_CORE_ASSERT(shader, "AssetCreate<Model>: default Mesh shader not found!");

		return std::make_shared<Model>(fullPath, shader);
	}

	// 从磁盘路径构造 GaussianModel (解析 .ply/.splat，填充 CPU 端高斯数据)
	template<>
	std::shared_ptr<GaussianModel> AssetCreate<GaussianModel>(const std::string& fullPath)
	{
		return GaussianModel::Create(fullPath);
	}

	template<>
	const char* AssetTypeName<Model>() { return "Model"; }

	template<>
	const char* AssetTypeName<GaussianModel>() { return "GaussianModel"; }

}
