#pragma once

#include "Engine/Renderer/Mesh.h"
#include "Engine/Renderer/Material.h"

#include <vector>
#include <string>

namespace tinygltf { class Model; class Mesh; }

namespace Engine {

    class Model
    {
    public:
        Model(const std::string& path, const std::shared_ptr<Shader>& shader);
        ~Model() = default;

        std::shared_ptr<Mesh> GetMesh() const { return m_Mesh; }
        const std::vector<std::shared_ptr<Material>>& GetMaterials() const { return m_Materials; }
        uint32_t GetMaterialCount() const { return (uint32_t)m_Materials.size(); }
    private:
        void LoadModel(const std::string& path);
        void LoadGLTF(const std::string& path);
        void LoadGLTFMaterials(const tinygltf::Model& model);
        std::shared_ptr<Texture2D> LoadGLTFTexture(const tinygltf::Model& model, int textureIndex, bool sRGB);
        void ProcessGLTFNode(int nodeIndex, const tinygltf::Model& model, const glm::mat4& parentTransform);
        void ProcessGLTFMesh(const tinygltf::Mesh& mesh, const tinygltf::Model& model, const glm::mat4& transform);

    private:
        std::shared_ptr<Mesh> m_Mesh;
        std::vector<std::shared_ptr<Material>> m_Materials;

        std::vector<MeshVertex> m_GlobalVertices;
        std::vector<uint32_t> m_GlobalIndices;
        std::vector<Mesh::Submesh> m_GlobalSubmeshes;

        std::string m_Directory;
        std::shared_ptr<Shader> m_BaseShader;
    };

}