#include "pch.h"
#include "Engine/Renderer/AssetLibrary.h"
#include "Engine/Renderer/Renderer.h"

#include <filesystem>

namespace Engine {

	// OBJ uses baked textures; glTF keeps the default PBR shader.
	template<>
	std::shared_ptr<Model> AssetCreate<Model>(const std::string& fullPath)
	{
		std::string extension = std::filesystem::path(fullPath).extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		const char* shaderName = extension == ".obj" ? "UnlitShader.vert.spv" : "MeshShader.vert.spv";
		auto shader = Renderer::GetShaderLibrary()->Get(shaderName);
		EG_CORE_ASSERT(shader, "AssetCreate<Model>: model shader not found!");

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
