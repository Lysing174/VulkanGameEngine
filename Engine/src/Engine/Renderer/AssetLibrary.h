#pragma once

#include "Engine/Scene/UUID.h"
#include "Engine/Core/Log.h"

#include "Engine/Renderer/Model.h"
#include "Engine/Renderer/GaussianModel.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace Engine {

	// 资产创建 / 类型名：以模板特化方式按资产类型提供实现 (特化声明见下方，定义在 AssetLibrary.cpp)。
	// 这样无需为每种资源写派生类，直接用 AssetLibrary<Model> / AssetLibrary<GaussianModel> 即可。
	template<typename T>
	std::shared_ptr<T> AssetCreate(const std::string& fullPath);

	template<typename T>
	const char* AssetTypeName();

	template<>
	std::shared_ptr<Model> AssetCreate<Model>(const std::string& fullPath);
	template<>
	std::shared_ptr<GaussianModel> AssetCreate<GaussianModel>(const std::string& fullPath);
	template<>
	const char* AssetTypeName<Model>();
	template<>
	const char* AssetTypeName<GaussianModel>();

	// 每个资产类型的默认扫描目录与扩展名。
	template<typename T>
	struct AssetDirTraits;

	template<>
	struct AssetDirTraits<Model>
	{
		static const char* Directory() { return "models"; }
		static std::vector<std::string> Extensions() { return { ".gltf", ".glb" }; }
	};

	template<>
	struct AssetDirTraits<GaussianModel>
	{
		static const char* Directory() { return "gaussians"; }
		static std::vector<std::string> Extensions() { return { ".ply", ".splat" }; }
	};

	// 资产库模板：以 UUID 作为资产身份 (key) 缓存已加载的资产 (shared_ptr<T>)，
	// 避免同一资产被多个 entity 重复加载 (重复解析 / 重建 GPU 资源)。
	//
	// 通用逻辑 (UUID 缓存 / .meta 持久化 / 路径反查 / 扫描清单 / 懒加载) 全部在本模板实现；
	// 资产如何构造由 AssetCreate<T> 特化决定，默认目录/扩展名由 AssetDirTraits<T> 决定。
	//
	// 设计参考 Unity 的 AssetDatabase：
	//   - 每个资产磁盘文件旁有同名 ".meta" 持久化其 UUID，改名/移动不影响引用。
	//   - 组件只存 UUID，不存路径；运行时按 UUID 从库里取资产。
	//   - 扫描阶段仅收集清单 (UUID + 显示名 + 相对路径)，实际资产在 Load 时懒加载。
	template<typename T>
	class AssetLibrary
	{
	public:
		// 按 UUID 加载/获取资产：已加载直接返回缓存，否则按记录的路径读盘构造并缓存。
		std::shared_ptr<T> Load(const UUID& id)
		{
			if ((uint64_t)id == 0) return nullptr;

			auto it = m_Assets.find(id);
			if (it != m_Assets.end()) return it->second;

			auto pathIt = m_IDToPath.find(id);
			if (pathIt == m_IDToPath.end())
			{
				EG_CORE_WARN("{0}: Load unknown asset UUID {1}", TypeName(), (uint64_t)id);
				return nullptr;
			}
			std::string fullPath = m_BaseDirectory + "/" + pathIt->second;

			auto asset = AssetCreate<T>(fullPath);
			if (asset) m_Assets[id] = asset;
			return asset;
		}

		std::shared_ptr<T> Get(const UUID& id) const
		{
			auto it = m_Assets.find(id);
			if (it != m_Assets.end()) return it->second;
			return nullptr;
		}

		bool Exists(const UUID& id) const
		{
			return m_Assets.find(id) != m_Assets.end();
		}

		// 按相对路径反查 UUID。找不到返回无效 UUID (0)。
		UUID FindIDByPath(const std::string& relativePath) const
		{
			auto it = m_PathToID.find(relativePath);
			if (it != m_PathToID.end()) return it->second;
			return UUID(0);
		}

		// 仅扫描目录、收集资产清单 (不加载二进制)。每个资产读取/生成同名 ".meta" 持久化 UUID。
		void ScanDirectory(const std::string& directory = AssetDirTraits<T>::Directory(),
			const std::vector<std::string>& extensions = AssetDirTraits<T>::Extensions())
		{
			m_AssetsList.clear();
			m_PathToID.clear();
			m_IDToPath.clear();
			m_BaseDirectory = directory;

			std::error_code ec;
			if (!std::filesystem::exists(directory, ec))
			{
				EG_CORE_WARN("{0}: ScanDirectory directory not found: {1}", TypeName(), directory);
				return;
			}

			for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, ec))
			{
				if (!entry.is_regular_file()) continue;

				std::string ext = entry.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

				bool match = false;
				for (const auto& e : extensions)
				{
					std::string le = e;
					std::transform(le.begin(), le.end(), le.begin(), ::tolower);
					if (ext == le) { match = true; break; }
				}
				if (!match) continue;

				// key = directory 之后的相对路径 (不含 directory 前缀)
				std::string full = entry.path().string();
				std::string key;
				auto pos = full.find(directory);
				if (pos != std::string::npos)
					key = full.substr(pos + directory.size() + 1); // +1 跳过分隔符
				else
					key = full;
				std::replace(key.begin(), key.end(), '\\', '/');

				// 友好名：去掉扩展名 (保留子目录结构便于区分同名文件)
				std::string name = key;
				if (name.size() >= ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
					name.erase(name.size() - ext.size());

				// 读取或生成 UUID (持久化在 .meta)
				UUID id = ReadOrCreateMeta(entry.path());

				m_AssetsList.push_back({ id, name });
				m_PathToID[key] = id;
				m_IDToPath[id] = key;
			}

			EG_CORE_INFO("{0}: scanned {1} asset(s) in '{2}'", TypeName(), m_AssetsList.size(), directory);
		}

		// 扫描并实际加载目录下所有资产 (建 CPU 数据)。一般请改用 ScanDirectory() + 懒加载。
		void LoadAllInDirectory(const std::string& directory = AssetDirTraits<T>::Directory(),
			const std::vector<std::string>& extensions = AssetDirTraits<T>::Extensions())
		{
			std::error_code ec;
			if (!std::filesystem::exists(directory, ec))
			{
				EG_CORE_WARN("{0}: LoadAllInDirectory directory not found: {1}", TypeName(), directory);
				return;
			}

			for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, ec))
			{
				if (!entry.is_regular_file()) continue;

				std::string ext = entry.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

				bool match = false;
				for (const auto& e : extensions)
				{
					std::string le = e;
					std::transform(le.begin(), le.end(), le.begin(), ::tolower);
					if (ext == le) { match = true; break; }
				}
				if (!match) continue;

				std::string full = entry.path().string();
				std::string key;
				auto pos = full.find(directory);
				if (pos != std::string::npos)
					key = full.substr(pos + directory.size() + 1);
				else
					key = full;
				std::replace(key.begin(), key.end(), '\\', '/');

				// 由相对路径取得/生成 UUID 并加载
				UUID id = ReadOrCreateMeta(entry.path());
				if (!m_PathToID.count(key))
				{
					m_PathToID[key] = id;
					m_IDToPath[id] = key;
				}
				if (!Exists(id))
				{
					EG_CORE_INFO("{0}: loading {1}", TypeName(), key);
					Load(id);
				}
			}
		}

		// 取得扫描到的资产清单 (下拉框用)：first=资产稳定身份，second=友好显示名。
		const std::vector<std::pair<UUID, std::string>>& GetAssets() const { return m_AssetsList; }

		// 取得所有已加载资产的 UUID 列表
		std::vector<UUID> GetAllIDs() const
		{
			std::vector<UUID> ids;
			ids.reserve(m_Assets.size());
			for (const auto& [k, _] : m_Assets)
				ids.push_back(k);
			return ids;
		}

		const char* TypeName() const { return AssetTypeName<T>(); }

	private:
		// 读取/生成某资产文件的 .meta，返回其持久化 UUID
		static UUID ReadOrCreateMeta(const std::filesystem::path& assetFilePath)
		{
			std::filesystem::path metaPath = assetFilePath;
			metaPath += ".meta";

			std::error_code ec;
			if (std::filesystem::exists(metaPath, ec))
			{
				std::ifstream in(metaPath);
				if (in)
				{
					std::string line;
					if (std::getline(in, line))
					{
						// 格式: "uuid=<number>"
						const std::string prefix = "uuid=";
						if (line.rfind(prefix, 0) == 0)
						{
							try
							{
								uint64_t v = std::stoull(line.substr(prefix.size()));
								return UUID(v);
							}
							catch (...) { /* 解析失败则重新生成 */ }
						}
					}
				}
			}

			// 生成新 UUID 并写回 .meta
			UUID newID;
			{
				std::ofstream out(metaPath, std::ios::trunc);
				if (out)
					out << "uuid=" << (uint64_t)newID << "\n";
				else
					EG_CORE_WARN("{0}: failed to write meta '{1}'", "AssetLibrary", metaPath.string());
			}
			return newID;
		}

		std::unordered_map<UUID, std::shared_ptr<T>> m_Assets;        // UUID -> 已加载资产
		std::unordered_map<std::string, UUID> m_PathToID;            // 相对路径 -> UUID 反查
		std::unordered_map<UUID, std::string> m_IDToPath;            // UUID -> 相对路径 (Load 时定位磁盘文件)
		std::vector<std::pair<UUID, std::string>> m_AssetsList;      // 扫描清单 (ID + 显示名)
		std::string m_BaseDirectory = "";                           // ScanDirectory 使用的根目录 (Load 时拼接路径)
	};

}
