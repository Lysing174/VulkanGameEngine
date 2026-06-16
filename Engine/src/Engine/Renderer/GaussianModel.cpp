#include "pch.h"
#include "GaussianModel.h"
#include "Engine/Renderer/Renderer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Engine {

    std::shared_ptr<GaussianModel> GaussianModel::Create(const std::string& path)
    {
        auto model = std::shared_ptr<GaussianModel>(new GaussianModel(path));
        Renderer::RegisterGaussianModel(model);
        return model;
    }

    GaussianModel::GaussianModel(const std::string& path)
    {
        LoadModel(path);
    }

    void GaussianModel::LoadModel(const std::string& path)
    {
        try
        {
            happly::PLYData plyFile(path);

            auto& vertex = plyFile.getElement("vertex");
            size_t count = vertex.count;

            // --- Positions ---
            auto x = vertex.getProperty<float>("x");
            auto y = vertex.getProperty<float>("y");
            auto z = vertex.getProperty<float>("z");

            // --- Normals ---
            bool hasNormals = vertex.hasProperty("nx");
            std::vector<float> nx, ny, nz;
            if (hasNormals)
            {
                nx = vertex.getProperty<float>("nx");
                ny = vertex.getProperty<float>("ny");
                nz = vertex.getProperty<float>("nz");
            }

            // --- SH DC ---
            bool hasSHDC = vertex.hasProperty("f_dc_0");
            std::vector<float> fdc0, fdc1, fdc2;
            if (hasSHDC)
            {
                fdc0 = vertex.getProperty<float>("f_dc_0");
                fdc1 = vertex.getProperty<float>("f_dc_1");
                fdc2 = vertex.getProperty<float>("f_dc_2");
            }

            // --- SH Rest ---
            int shRestCount = 0;
            while (vertex.hasProperty("f_rest_" + std::to_string(shRestCount)))
                shRestCount++;

            std::vector<std::vector<float>> shRestData(shRestCount);
            for (int k = 0; k < shRestCount; k++)
                shRestData[k] = vertex.getProperty<float>("f_rest_" + std::to_string(k));

            // --- Opacity ---
            std::vector<float> opacity;
            if (vertex.hasProperty("opacity"))
                opacity = vertex.getProperty<float>("opacity");

            // --- Scale ---
            bool hasScale = vertex.hasProperty("scale_0");
            std::vector<float> s0, s1, s2;
            if (hasScale)
            {
                s0 = vertex.getProperty<float>("scale_0");
                s1 = vertex.getProperty<float>("scale_1");
                s2 = vertex.getProperty<float>("scale_2");
            }

            // --- Rotation ---
            bool hasRotation = vertex.hasProperty("rot_0");
            std::vector<float> r0, r1, r2, r3;
            if (hasRotation)
            {
                r0 = vertex.getProperty<float>("rot_0");
                r1 = vertex.getProperty<float>("rot_1");
                r2 = vertex.getProperty<float>("rot_2");
                r3 = vertex.getProperty<float>("rot_3");
            }

            // --- Assemble per-gaussian data ---
            m_Gaussians.resize(count);
            for (size_t i = 0; i < count; i++)
            {
                GaussianData& g = m_Gaussians[i];

                g.position = { x[i], y[i], z[i] };

                if (hasNormals)
                    g.normal = { nx[i], ny[i], nz[i] };

                if (hasSHDC)
                    g.shDC = { fdc0[i], fdc1[i], fdc2[i] };

                if (!opacity.empty())
                    g.opacity = opacity[i];

                if (hasScale)
                    g.scale = { s0[i], s1[i], s2[i] };

                if (hasRotation)
                    g.rotation = { r0[i], r1[i], r2[i], r3[i] };

                g.shRest.resize(shRestCount);
                for (int k = 0; k < shRestCount; k++)
                    g.shRest[k] = shRestData[k][i];
            }

            EG_CORE_INFO("Loaded Gaussian model: {0} ({1} gaussians, {2} SH rest coefficients)",
                path, count, shRestCount);
        }
        catch (const std::exception& e)
        {
            EG_CORE_ERROR("Failed to load Gaussian model '{0}': {1}", path, e.what());
        }
    }

    std::vector<GaussianDataGPU> GaussianModel::GetGPUData() const
    {
        uint32_t count = GetGaussianCount();
        std::vector<GaussianDataGPU> gpuData(count);

        const float SH_C0 = 0.28209479177387814f;

        for (uint32_t i = 0; i < count; i++)
        {
            const GaussianData& g = m_Gaussians[i];

            // Position + sigmoid opacity
            gpuData[i].posAndOpacity = glm::vec4(g.position, 1.0f / (1.0f + std::exp(-g.opacity)));

            // SH DC → color: C0 = 0.28209479177, color = 0.5 + C0 * shDC
            gpuData[i].color = glm::vec4(
                glm::clamp(0.5f + SH_C0 * g.shDC, 0.0f, 1.0f),
                0.0f
            );

            // Precompute 3D covariance from scale + rotation
            // scale needs exp(), rotation needs normalize
            glm::vec3 scale = { std::exp(g.scale.x), std::exp(g.scale.y), std::exp(g.scale.z) };
            glm::quat rotation = glm::normalize(glm::quat(g.rotation.x, g.rotation.y, g.rotation.z, g.rotation.w));
            // S = diag(scale), R = mat3(rotation), M = R * S, cov3D = M * M^T
            glm::mat3 S = glm::mat3(glm::scale(glm::mat4(1.0f), scale));
            glm::mat3 R = glm::mat3_cast(rotation);
            glm::mat3 M = R * S;
            glm::mat3 cov = M * glm::transpose(M);

            // Store upper triangle of symmetric 3x3 matrix
            gpuData[i].cov3d[0] = cov[0][0]; // M00
            gpuData[i].cov3d[1] = cov[0][1]; // M01
            gpuData[i].cov3d[2] = cov[0][2]; // M02
            gpuData[i].cov3d[3] = cov[1][1]; // M11
            gpuData[i].cov3d[4] = cov[1][2]; // M12
            gpuData[i].cov3d[5] = cov[2][2]; // M22

            gpuData[i].modelIndex = m_ModelIndex;
            gpuData[i]._pad1 = 0.0f;
        }
        return gpuData;
    }

}
