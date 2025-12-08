#pragma once

#include "Engine/Renderer/Mesh.h"
#include "Engine/Renderer/Material.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <vector>
#include <string>

namespace Engine {

    class Model
    {
    public:
        Model(const std::string& path, const std::shared_ptr<Shader>& shader);
        ~Model() = default;


        std::shared_ptr<Mesh> GetMesh() const { return m_Mesh; }
        const std::vector<std::shared_ptr<Material>>& GetMaterials() const { return m_Materials; }
    private:
        void LoadModel(const std::string& path);
        void ProcessNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform);
        void ProcessMesh(aiMesh* mesh, const aiScene* scene, const glm::mat4& transform);

        // 辅助：加载纹理
        //std::shared_ptr<Texture2D> LoadMaterialTexture(aiMaterial* mat, aiTextureType type);

    private:
        std::shared_ptr<Mesh> m_Mesh;
        std::vector<std::shared_ptr<Material>> m_Materials;

        // 临时加载数据 (Load 完成后由于 Model 析构或栈释放，这些 vector 会被清理，但这做成员变量方便递归)
        std::vector<MeshVertex> m_GlobalVertices;
        std::vector<uint32_t> m_GlobalIndices;
        std::vector<Mesh::Submesh> m_GlobalSubmeshes;

        std::string m_Directory;
        std::shared_ptr<Shader> m_BaseShader;
    };

}