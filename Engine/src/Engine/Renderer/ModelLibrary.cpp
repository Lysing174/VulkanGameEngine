#include "pch.h"
#include "ModelLibrary.h"
#include "Engine/Renderer/Renderer.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace Engine {

	// 读取模型文件旁的同名 ".meta"，获取持久化 UUID；
	// 若不存在则新生成一个并写入，保证跨会话/改名后仍稳定。
	UUID ModelLibrary::ReadOrCreateMeta(const std::filesystem::path& modelFilePath)
	{
		std::filesystem::path metaPath = modelFilePath;
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
				EG_CORE_WARN("ModelLibrary: failed to write meta '{0}'", metaPath.string());
		}
		return newID;
	}

	std::shared_ptr<Model> ModelLibrary::Load(const UUID& id)
	{
		if ((uint64_t)id == 0)return nullptr;
		
		auto it = m_Models.find(id);
		if (it != m_Models.end())
			return it->second;

		// 由 UUID 反查相对路径，再拼接 models/ 前缀定位磁盘文件
		auto pathIt = m_IDToPath.find(id);
		if (pathIt == m_IDToPath.end())
		{
			EG_CORE_WARN("ModelLibrary::Load: unknown asset UUID {0}", (uint64_t)id);
			return nullptr;
		}
		std::string fullPath = "models/" + pathIt->second;

		// Model 需要 shader 创建 pipeline，使用默认 PBR shader
		auto shader = Renderer::GetShaderLibrary()->Get("Mesh.vert.spv");
		EG_CORE_ASSERT(shader, "ModelLibrary::Load: default Mesh shader not found!");

		auto model = std::make_shared<Model>(fullPath, shader);
		m_Models[id] = model;
		return model;
	}

	std::shared_ptr<Model> ModelLibrary::Get(const UUID& id) const
	{
		auto it = m_Models.find(id);
		if (it != m_Models.end())
			return it->second;
		return nullptr;
	}

	bool ModelLibrary::Exists(const UUID& id) const
	{
		return m_Models.find(id) != m_Models.end();
	}

	UUID ModelLibrary::FindIDByPath(const std::string& relativePath) const
	{
		auto it = m_PathToID.find(relativePath);
		if (it != m_PathToID.end())
			return it->second;
		return UUID(0); // 无效 UUID
	}

	void ModelLibrary::ScanDirectory(const std::string& directory, const std::vector<std::string>& extensions)
	{
		m_Assets.clear();
		m_PathToID.clear();
		m_IDToPath.clear();

		std::error_code ec;
		if (!std::filesystem::exists(directory, ec))
		{
			EG_CORE_WARN("ModelLibrary::ScanDirectory: directory not found: {0}", directory);
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

			m_Assets.push_back({ id, name });
			m_PathToID[key] = id;
			m_IDToPath[id] = key;
		}

		EG_CORE_INFO("ModelLibrary: scanned {0} model(s) in '{1}'", m_Assets.size(), directory);
	}

	void ModelLibrary::LoadAllInDirectory(const std::string& directory, const std::vector<std::string>& extensions)
	{
		std::error_code ec;
		if (!std::filesystem::exists(directory, ec))
		{
			EG_CORE_WARN("ModelLibrary::LoadAllInDirectory: directory not found: {0}", directory);
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
			// 例如 directory="models", 文件="models/a/b.gltf" -> key="a/b.gltf"
			std::string full = entry.path().string();
			std::string key;
			auto pos = full.find(directory);
			if (pos != std::string::npos)
				key = full.substr(pos + directory.size() + 1); // +1 跳过分隔符
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
				EG_CORE_INFO("ModelLibrary: loading {0}", key);
				Load(id);
			}
		}
	}

	std::vector<UUID> ModelLibrary::GetAllIDs() const
	{
		std::vector<UUID> ids;
		ids.reserve(m_Models.size());
		for (const auto& [k, _] : m_Models)
			ids.push_back(k);
		return ids;
	}

}
