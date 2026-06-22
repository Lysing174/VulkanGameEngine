#include "pch.h"
#include "Model.h"
#include "Engine/Renderer/Texture.h"
#include "Platform/Vulkan/VulkanTexture.h"

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

    // 辅助函数：加载材质纹理（支持多类型fallback + 嵌入纹理）
    // sRGB: true 用于颜色贴图 (albedo/emissive), false 用于数据贴图 (normal/ORM)
    static std::shared_ptr<Texture2D> LoadMaterialTexture(
        aiMaterial* mat, aiTextureType type,
        const std::string& directory, const aiScene* scene, bool sRGB)
    {
        if (mat->GetTextureCount(type) > 0)
        {
            aiString path;
            if (mat->GetTexture(type, 0, &path) == AI_SUCCESS)
            {
                std::string pathStr = path.C_Str();

                // 嵌入纹理 (glTF Embedded): Assimp 以 "*0", "*1" 等命名
                if (!pathStr.empty() && pathStr[0] == '*')
                {
                    int index = std::atoi(pathStr.c_str() + 1);
                    if (scene && index >= 0 && index < (int)scene->mNumTextures)
                    {
                        aiTexture* aiTex = scene->mTextures[index];
                        VkFormat vkFormat = sRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
                        // mHeight == 0 表示压缩格式 (PNG/JPG), mWidth 是数据大小
                        if (aiTex->mHeight == 0)
                        {
                            try {
                                return Texture2D::CreateFromMemory(
                                    aiTex->pcData, (size_t)aiTex->mWidth, sRGB);
                            }
                            catch (const std::exception& e) {
                                EG_CORE_WARN("Failed to load embedded texture {0}: {1}", pathStr, e.what());
                            }
                        }
                        else
                        {
                            // 未压缩的原始 RGBA 像素
                            try {
                                return std::make_shared<VulkanTexture2D>(
                                    aiTex->mWidth, aiTex->mHeight, aiTex->pcData, vkFormat);
                            }
                            catch (const std::exception& e) {
                                EG_CORE_WARN("Failed to load embedded RGBA texture {0}: {1}", pathStr, e.what());
                            }
                        }
                    }
                    return nullptr;
                }

                // 外部文件纹理
                std::string texPath = directory + "/" + pathStr;
                std::replace(texPath.begin(), texPath.end(), '\\', '/');
                try {
                    return Texture2D::Create(texPath, sRGB);
                }
                catch (const std::exception& e) {
                    EG_CORE_WARN("Failed to load texture {0}: {1}", texPath, e.what());
                }
            }
        }
        return nullptr;
    }

    // 按优先级尝试多种纹理类型
    static std::shared_ptr<Texture2D> LoadMaterialTexture(
        aiMaterial* mat,
        const std::vector<aiTextureType>& types,
        const std::string& directory,
        const aiScene* scene, bool sRGB)
    {
        for (aiTextureType type : types)
        {
            auto tex = LoadMaterialTexture(mat, type, directory, scene, sRGB);
            if (tex) return tex;
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

        // 预处理材质 (一次性加载所有材质, 支持 OBJ/MTL 和 glTF PBR)
        if (scene->HasMaterials())
        {
            m_Materials.resize(scene->mNumMaterials);
            for (uint32_t i = 0; i < scene->mNumMaterials; i++)
            {
                aiMaterial* aiMat = scene->mMaterials[i];
                auto material = std::make_shared<Material>(m_BaseShader);

                material->BeginBatchUpdate();

                // ----- Albedo / BaseColor -----
                // glTF: aiTextureType_BASE_COLOR;  OBJ: aiTextureType_DIFFUSE
                // sRGB=true: 颜色贴图需要硬件 sRGB→线性 转换
                auto albedoTexture = LoadMaterialTexture(aiMat,
                    { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }, m_Directory, scene, true);
                if (albedoTexture)
                {
                    material->SetTexture("u_AlbedoMap", albedoTexture);
                    material->SetHasAlbedoMap(true);
                }

                // ----- Normal Map -----
                // glTF: aiTextureType_NORMAL_CAMERA;  OBJ: aiTextureType_NORMALS / HEIGHT
                // sRGB=false: 法线贴图是向量数据，必须保持线性空间
                auto normalTexture = LoadMaterialTexture(aiMat,
                    { aiTextureType_NORMAL_CAMERA, aiTextureType_NORMALS, aiTextureType_HEIGHT }, m_Directory, scene, false);
                if (normalTexture)
                {
                    material->SetTexture("u_NormalMap", normalTexture);
                    material->SetUseNormalMap(true);
                }

                // ----- Metallic-Roughness (ORM packed: R=AO, G=Roughness, B=Metallic) -----
                // glTF: metallicRoughnessTexture maps to aiTextureType_METALNESS or UNKNOWN
                // sRGB=false: ORM 贴图存的是标量数据，必须保持线性空间
                auto ormTexture = LoadMaterialTexture(aiMat,
                    { aiTextureType_METALNESS, aiTextureType_DIFFUSE_ROUGHNESS }, m_Directory, scene, false);
                if (ormTexture)
                {
                    material->SetTexture("u_MetalRoughAO", ormTexture);
                    material->SetHasMetalRoughnessMap(true);
                    material->SetAOStrength(1.0f); 
                }
                
                // 独立 AO 图（glTF occlusionTexture）
                // sRGB=false: AO 贴图是标量数据
                auto aoTexture = LoadMaterialTexture(aiMat,
                    { aiTextureType_LIGHTMAP }, m_Directory, scene, false);
                if (aoTexture && !ormTexture)  // ORM 已包含 AO 时不重复设置
                {
                    material->SetTexture("u_AOMap", aoTexture);
                    material->SetAOStrength(1.0f);
                }

                // Emissive texture
                // sRGB=true: 自发光颜色贴图需要硬件 sRGB→线性 转换
                auto emissiveTexture = LoadMaterialTexture(aiMat,
                    { aiTextureType_EMISSIVE }, m_Directory, scene, true);
                if (emissiveTexture)
                {
                    material->SetTexture("u_EmissiveMap", emissiveTexture);
                    material->SetHasEmissiveMap(true);
                }

                // ----- PBR Material Factors (from glTF or MTL) -----

                // Base Color Factor
                aiColor4D baseColor;
                if (AI_SUCCESS == aiMat->Get(AI_MATKEY_BASE_COLOR, baseColor))
                {
                    material->SetAlbedoColor(glm::vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a));
                }
                else if (AI_SUCCESS == aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor))
                {
                    // OBJ/MTL fallback
                    material->SetAlbedoColor(glm::vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a));
                }

                // Metallic Factor
                float metallic = 0.0f;
                if (AI_SUCCESS == aiMat->Get(AI_MATKEY_METALLIC_FACTOR, metallic))
                {
                    material->SetMetalness(metallic);
                }

                // Roughness Factor
                float roughness = 0.5f;
                if (AI_SUCCESS == aiMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness))
                {
                    material->SetRoughness(roughness);
                }

                // Emissive Factor (glTF)
                aiColor3D emissiveColor(0.0f, 0.0f, 0.0f);
                if (AI_SUCCESS == aiMat->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor))
                {
                    float emissiveIntensity = 1.0f;
                    aiMat->Get(AI_MATKEY_EMISSIVE_INTENSITY, emissiveIntensity);
                    material->SetEmissive(emissiveIntensity, glm::vec3(emissiveColor.r, emissiveColor.g, emissiveColor.b));
                }

                // Opacity
                float opacity = 1.0f;
                if (AI_SUCCESS == aiMat->Get(AI_MATKEY_OPACITY, opacity))
                {
                    glm::vec4 col = material->GetColor();
                    col.a = opacity;
                    material->SetAlbedoColor(col);
                }

                material->EndBatchUpdate();

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

            // Read tangent from Assimp (aiProcess_CalcTangentSpace already computed it)
            // Assimp stores handedness in mBitangents, not tangent.w; we cross-product reconstruct B later
            if (mesh->HasTangentsAndBitangents())
                vertex.tangent = { mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z };
            else
                vertex.tangent = { 1.0f, 0.0f, 0.0f };

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