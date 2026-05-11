#include "pch.h"
#include "Model.h"
#include "Engine/Renderer/Texture.h"

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

    // 辅助函数：加载材质纹理
    static std::shared_ptr<Texture2D> LoadMaterialTexture(aiMaterial* mat, aiTextureType type, const std::string& directory)
    {
        if (mat->GetTextureCount(type) > 0)
        {
            aiString path;
            if (mat->GetTexture(type, 0, &path) == AI_SUCCESS)
            {
                std::string texPath = directory + "/" + std::string(path.C_Str());
                // 替换反斜杠为正斜杠
                std::replace(texPath.begin(), texPath.end(), '\\', '/');
                try {
                    return Texture2D::Create(texPath);
                }
                catch (const std::exception& e) {
                    EG_CORE_WARN("Failed to load texture {0}: {1}", texPath, e.what());
                    // 尝试备用目录 textures/
                    std::string fallbackTexPath = "textures/" + std::string(path.C_Str());
                    try {
                        EG_CORE_INFO("Trying fallback path: {0}", fallbackTexPath);
                        return Texture2D::Create(fallbackTexPath);
                    }
                    catch (const std::exception&) {
                        // 备用也失败，返回 nullptr
                    }
                }
            }
        }
        return nullptr;
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

        // 保存目录路径，例如 "models/cottage_obj.obj" -> "models"
        size_t lastSlash = path.find_last_of("/\\");
        m_Directory = (lastSlash != std::string::npos) ? path.substr(0, lastSlash) : "";

        // 预处理材质 (一次性加载所有材质)
        if (scene->HasMaterials())
        {
            m_Materials.resize(scene->mNumMaterials);
            for (uint32_t i = 0; i < scene->mNumMaterials; i++)
            {
                aiMaterial* aiMat = scene->mMaterials[i];
                auto material = std::make_shared<Material>(m_BaseShader);

                // 加载漫反射纹理 ( diffuse map_Kd )
                auto diffuseTexture = LoadMaterialTexture(aiMat, aiTextureType_DIFFUSE, m_Directory);
                if (diffuseTexture)
                {
                    material->SetTexture("u_AlbedoMap", diffuseTexture);
                }

                // 加载法线/凹凸贴图 ( map_Bump → aiTextureType_HEIGHT )
                auto normalTexture = LoadMaterialTexture(aiMat, aiTextureType_HEIGHT, m_Directory);
                if (!normalTexture)
                    normalTexture = LoadMaterialTexture(aiMat, aiTextureType_NORMALS, m_Directory);
                if (normalTexture)
                {
                    material->SetTexture("u_NormalMap", normalTexture);
                }

                // 读取透明度 (MTL 的 d 值或 Tr 值)
                float opacity = 1.0f;
                if (AI_SUCCESS == aiMat->Get(AI_MATKEY_OPACITY, opacity))
                {
                    glm::vec4 color = material->GetColor();
                    color.a = opacity;
                    material->SetAlbedoColor(color);
                }

                m_Materials[i] = material;
            }
        }

        ProcessNode(scene->mRootNode, scene, glm::mat4(1.0f));

        m_Mesh = std::make_shared<Mesh>(m_GlobalVertices, m_GlobalIndices,m_GlobalSubmeshes);
    }

    void Model::ProcessNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform)
    {
        // 计算当前节点的累积变换矩阵
        glm::mat4 transform = parentTransform * AssimpToGLM(node->mTransformation);

        // 处理该节点下的所有 Mesh
        for (uint32_t i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            ProcessMesh(mesh, scene, transform);
        }

        // 递归处理子节点
        for (uint32_t i = 0; i < node->mNumChildren; i++)
        {
            ProcessNode(node->mChildren[i], scene, transform);
        }

    }

    void Model::ProcessMesh(aiMesh* mesh, const aiScene* scene, const glm::mat4& transform)
    {
        uint32_t firstVertex = (uint32_t)m_GlobalVertices.size();
        uint32_t firstIndex = (uint32_t)m_GlobalIndices.size();
        uint32_t indexCount = mesh->mNumFaces * 3;
        
        for (uint32_t i = 0; i < mesh->mNumVertices; i++)
        {
            MeshVertex vertex;
            vertex.pos = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };

            if (mesh->HasNormals())
                vertex.normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };
            else
                vertex.normal = { 0.0f, 0.0f, 0.0f };

            if (mesh->mTextureCoords[0])
                vertex.texCoord = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
            else
                vertex.texCoord = { 0.0f, 0.0f };

            m_GlobalVertices.push_back(vertex);
        }

        for (uint32_t i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            for (uint32_t j = 0; j < face.mNumIndices; j++)
            {
                // 索引不需要加 baseVertex，因为 DrawIndexed 的 baseVertex 参数会处理它
                // 或者在这里加也行，取决于你 Renderer 怎么写。
                // 推荐：存局部索引 (0, 1, 2...)，渲染时利用 vkCmdDrawIndexed 的 vertexOffset 参数。
                m_GlobalIndices.push_back(face.mIndices[j]);
            }
        }

        Mesh::Submesh submesh;
        submesh.FirstVertex = firstVertex;    
        submesh.FirstIndex = firstIndex;      
        submesh.IndexCount = indexCount;    
        submesh.MaterialIndex = mesh->mMaterialIndex; 
        submesh.Transform = transform;       

        m_GlobalSubmeshes.push_back(submesh);
    }
}