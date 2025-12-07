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

        struct Submesh
        {
            std::shared_ptr<Mesh> Mesh;
            std::shared_ptr<Material> Material;
            std::string NodeName;
            glm::mat4 Transform; 
        };

        const std::vector<Submesh>& GetSubmeshes() const { return m_Submeshes; }

    private:
        void LoadModel(const std::string& path);
        void ProcessNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform);
        Submesh ProcessMesh(aiMesh* mesh, const aiScene* scene, const glm::mat4& transform);

        // 辅助：加载纹理
        //std::shared_ptr<Texture2D> LoadMaterialTexture(aiMaterial* mat, aiTextureType type);

    private:
        std::vector<Submesh> m_Submeshes;
        std::string m_Directory; // 模型所在文件夹路径（用于加载纹理）

        std::shared_ptr<Shader> m_BaseShader; // 所有子网格共用的 Shader
    };

}