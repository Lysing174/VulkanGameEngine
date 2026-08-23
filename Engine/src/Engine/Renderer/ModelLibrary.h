#pragma once

#include "Engine/Renderer/Model.h"
#include "Engine/Scene/UUID.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace Engine {

	// 模型资产库：以 UUID 作为资产身份 (key) 缓存已加载的 Model，
	// 避免同一模型被多个 entity 重复加载 (重复创建 GPU 缓冲 / 材质 DescriptorSet)。
	//
	// 设计参考 Unity 的 AssetDatabase：
	//   - 每个模型磁盘文件旁边有一个同名的 ".meta" 文件，持久化其 UUID，
	//     因此改名/移动文件只要同步 .meta，场景引用 (存的是 UUID) 不会失效。
	//   - 组件 (MeshFilterComponent / MeshRendererComponent) 只存 UUID，
	//     不存路径；运行时按 UUID 从库里取 Model。
	//   - 扫描阶段仅收集资产清单 (UUID + 显示名 + 相对路径)，不加载二进制；
	//     实际 Model 在 SubmitMesh 时按 UUID 懒加载。
	class ModelLibrary
	{
	public:
		// 按 UUID 加载/获取 Model (已加载则直接返回缓存，否则按记录的路径读盘)
		std::shared_ptr<Model> Load(const UUID& id);

		std::shared_ptr<Model> Get(const UUID& id) const;
		bool Exists(const UUID& id) const;

		// 按相对路径反查 UUID (扫描清单内)。找不到返回无效 UUID (0)。
		UUID FindIDByPath(const std::string& relativePath) const;

		// 扫描 models/ 目录下所有指定后缀的模型，全部预加载进库
		// 便于资源视图 / 编辑器里浏览所有可用模型
		// 注意：此函数会实际加载所有模型 (建 GPU buffer / 材质)，启动较慢。
		// 一般情况下请改用 ScanDirectory() + 懒加载。
		void LoadAllInDirectory(const std::string& directory = "models",
			const std::vector<std::string>& extensions = { ".gltf", ".glb" });

		// 仅扫描目录、收集资产清单 (不加载实际二进制)。
		// 用于编辑器下拉框 / 资源浏览，避免启动时把所有模型都读进内存。
		// 每个资产会读取/生成同目录同名 ".meta" 以持久化其 UUID。
		void ScanDirectory(const std::string& directory = "models",
			const std::vector<std::string>& extensions = { ".gltf", ".glb" });

		// 取得扫描到的资产清单 (下拉框用)。
		// 每个元素为 pair<UUID, 显示名>：first=资产稳定身份，second=友好显示名。
		const std::vector<std::pair<UUID, std::string>>& GetAssets() const { return m_Assets; }

		// 取得所有已加载模型的 UUID 列表 (资源视图用)
		std::vector<UUID> GetAllIDs() const;

	private:
		// 读取/生成某模型文件的 .meta，返回其持久化 UUID
		static UUID ReadOrCreateMeta(const std::filesystem::path& modelFilePath);

		std::unordered_map<UUID, std::shared_ptr<Model>> m_Models;
		std::unordered_map<std::string, UUID> m_PathToID; // 相对路径 -> UUID 反查
		std::unordered_map<UUID, std::string> m_IDToPath;  // UUID -> 相对路径 (Load 时定位磁盘文件)
		std::vector<std::pair<UUID, std::string>> m_Assets; // 扫描清单 (ID + 显示名)，可重复扫描会清空重建
	};

}
