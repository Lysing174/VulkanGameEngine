#pragma once

#include "Engine/Renderer/Buffer.h"
#include <happly.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>
#include <string>
#include <limits>

namespace Engine {

    // CPU-side data (variable-length shRest)
    struct GaussianData
    {
        glm::vec3 position  = { 0.0f, 0.0f, 0.0f };
        glm::vec3 normal    = { 0.0f, 0.0f, 0.0f };
        glm::vec3 shDC      = { 0.0f, 0.0f, 0.0f };     // f_dc_0, f_dc_1, f_dc_2
        float     opacity   = 0.0f;                      // raw opacity (needs sigmoid)
        glm::vec3 scale     = { 0.0f, 0.0f, 0.0f };      // scale_0, scale_1, scale_2 (needs exp)
        glm::vec4 rotation  = { 1.0f, 0.0f, 0.0f, 0.0f }; // rot_0=w, rot_1=x, rot_2=y, rot_3=z

        // Higher-order SH coefficients (f_rest_0 ... f_rest_N)
        std::vector<float> shRest;
    };

    // GPU-side data for 3DGS splat rendering (fixed-size, std430 compatible)
    // Precomputed: color (from SH DC + sigmoid opacity), 3D covariance (from scale + rotation)
    struct GaussianDataGPU
    {
        glm::vec4 posAndOpacity;   // xyz = position, w = sigmoid(opacity)
        glm::vec4 color;           // rgb = SH DC color, w = unused
        float cov3d[6];            // symmetric 3x3 upper triangle: M00,M01,M02,M11,M12,M22
        uint32_t modelIndex;       // per-instance transform slot, filled at upload time (NOT asset state)
        float _pad1;               // padding to 64 bytes
    };

    // Gaussian asset: immutable CPU-side splat data loaded from disk (.ply/.splat).
    // Cached by AssetLibrary<GaussianModel> keyed by asset UUID — the heavy parse happens once
    // and the result is shared by every GaussianComponent referencing the same UUID.
    //
    // Per-instance GPU state (global SSBO offset / transform index) is intentionally NOT
    // stored here: it belongs to the instance (component) and the global Gaussian renderer,
    // so a cached GaussianModel can be reused by multiple placed instances without conflict.
    class GaussianModel
    {
    public:
        // 用 Create() 而非直接构造：从磁盘加载并解析 splat 文件。
        static std::shared_ptr<GaussianModel> Create(const std::string& path);

        ~GaussianModel() = default;

        const std::vector<GaussianData>& GetGaussians() const { return m_Gaussians; }
        uint32_t GetGaussianCount() const { return (uint32_t)m_Gaussians.size(); }

        // 生成上传到全局 SSBO 所需的扁平 GPU 数据。
        // modelIndex 为每实例变换在 ModelTransform SSBO 中的下标 (由 Gaussian renderer 分配)，
        // 不存于资产本身，因此同一资产可被多个实例复用。
        std::vector<GaussianDataGPU> GetGPUData(uint32_t modelIndex) const;

    private:
        GaussianModel(const std::string& path);
        void LoadModel(const std::string& path);

    private:
        std::vector<GaussianData> m_Gaussians;
    };

}
