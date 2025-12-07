#include "pch.h"
#include "Model.h"
//#include "Engine/Renderer/Texture.h"

#include <glm/gtc/type_ptr.hpp> 

namespace Engine {

    static glm::mat4 AssimpToGLM(const aiMatrix4x4& from)
    {
        glm::mat4 to;
        // Assimp 是行主序，GLM 是列主序，需要转置
        to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
        to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
        to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
        to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
        return to;
    }

    Model::Model(const std::string& path, const std::shared_ptr<Shader>& shader)
        : m_BaseShader(shader)
    {
        LoadModel(path);
    }


    void Model::LoadModel(const std::string& path)
    {
        Assimp::Importer importer;
        // 关键 Flag：
        // Triangulate: 把所有多边形变成三角形
        // FlipUVs: 翻转 Y 轴纹理坐标 (Vulkan 有时需要，看你的投影矩阵设置，通常 OpenGL 需要)
        // CalcTangentSpace: 计算法线贴图需要的切线
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            EG_CORE_ERROR("Assimp Error: {0}", importer.GetErrorString());
            return;
        }

        // 保存目录路径，例如 "assets/models/backpack/backpack.obj" -> "assets/models/backpack"
        m_Directory = path.substr(0, path.find_last_of('/'));

        // 递归处理节点
        ProcessNode(scene->mRootNode, scene, glm::mat4(1.0f));
    }

    void Model::ProcessNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform)
    {
        // 计算当前节点的累积变换矩阵
        glm::mat4 transform = parentTransform * AssimpToGLM(node->mTransformation);

        // 处理该节点下的所有 Mesh
        for (uint32_t i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            m_Submeshes.push_back(ProcessMesh(mesh, scene, transform));
        }

        // 递归处理子节点
        for (uint32_t i = 0; i < node->mNumChildren; i++)
        {
            ProcessNode(node->mChildren[i], scene, transform);
        }
    }

    Model::Submesh Model::ProcessMesh(aiMesh* mesh, const aiScene* scene, const glm::mat4& transform)
    {
        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;

        for (uint32_t i = 0; i < mesh->mNumVertices; i++)
        {
            MeshVertex vertex;

            vertex.pos.x = mesh->mVertices[i].x;
            vertex.pos.y = mesh->mVertices[i].y;
            vertex.pos.z = mesh->mVertices[i].z;

            if (mesh->HasNormals())
            {
                vertex.normal.x = mesh->mNormals[i].x;
                vertex.normal.y = mesh->mNormals[i].y;
                vertex.normal.z = mesh->mNormals[i].z;
            }

            if (mesh->mTextureCoords[0])
            {
                vertex.texCoord.x = mesh->mTextureCoords[0][i].x;
                vertex.texCoord.y = mesh->mTextureCoords[0][i].y;
            }
            else
            {
                vertex.texCoord = glm::vec2(0.0f, 0.0f);
            }

            vertices.push_back(vertex);
        }

        for (uint32_t i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            for (uint32_t j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);
        }

        auto material = std::make_shared<Material>(m_BaseShader);

        if (mesh->mMaterialIndex >= 0)
        {
            aiMaterial* aiMat = scene->mMaterials[mesh->mMaterialIndex];

            // 这里我们只演示加载漫反射贴图 (Albedo)
            // 真实的 PBR 需要加载 Normal, Roughness, Metallic 等
            // TODO: 实现 LoadMaterialTexture
            // auto texture = LoadMaterialTexture(aiMat, aiTextureType_DIFFUSE);
            // material->SetTexture("u_AlbedoMap", texture);

            // 简单起见，这里先随机个颜色或者白色
            material->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        }

        Submesh submesh;
        submesh.Mesh = std::make_shared<Mesh>(vertices, indices); 
        submesh.Material = material;
        submesh.Transform = transform;
        submesh.NodeName = mesh->mName.C_Str();

        return submesh;
    }
}