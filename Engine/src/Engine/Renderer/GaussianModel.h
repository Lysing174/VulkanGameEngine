#pragma once

#include "Engine/Renderer/Buffer.h"
#include <happly.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>
#include <string>
#include <limits>

namespace Engine {

    // 球谐阶数 (degree). 3 = 三阶, 共 (3+1)^2 = 16 个基函数/通道, RGB 合计 48 个 float.
    constexpr uint32_t SH_DEGREE = 3;
    constexpr uint32_t SH_BASIS  = (SH_DEGREE + 1) * (SH_DEGREE + 1); // 16
    constexpr uint32_t SH_TOTAL  = SH_BASIS * 3;                       // 48

    // CPU-side data
    struct GaussianData
    {
        glm::vec3 position  = { 0.0f, 0.0f, 0.0f };
        glm::vec3 normal    = { 0.0f, 0.0f, 0.0f };
        // 完整球谐系数: 按基函数索引 k (0..SH_BASIS-1), 每基函数 3 通道 (R,G,B) 连续存放.
        // sh[3*k+0..2] = 基函数 k 的 RGB. k=0 对应 f_dc, k>=1 对应 f_rest.
        // PLY channel-major coefficients are reordered on load; f_dc(3) + f_rest(45) = 48.
        float     sh[SH_TOTAL] = {};
        float     opacity   = 0.0f;                      // raw opacity (needs sigmoid)
        glm::vec3 scale     = { 0.0f, 0.0f, 0.0f };      // scale_0, scale_1, scale_2 (needs exp)
        glm::vec4 rotation  = { 1.0f, 0.0f, 0.0f, 0.0f }; // rot_0=w, rot_1=x, rot_2=y, rot_3=z
    };

    // GPU-side data for 3DGS splat rendering (fixed-size, std430 compatible).
    // 颜色不再在 CPU 烘焙: 完整球谐系数上传显存, 由 shader 用视线方向实时求值 (3DGS 原论文做法).
    // 3D 协方差仍预计算 (from scale + rotation).
    struct GaussianDataGPU
    {
        glm::vec4 posAndOpacity;        // 16 B  xyz = position, w = sigmoid(opacity)
        float     sh[SH_TOTAL];         // 192 B 完整三阶球谐系数 (std430, 逐 float 紧密排列)
        float     cov3d[6];             // 24 B  对称 3x3 上三角: M00,M01,M02,M11,M12,M22
        uint32_t  modelIndex;           //  4 B  每实例变换槽位, 上传时填充 (非资产状态)
        float     _pad1;                //  4 B  补齐到 240 B
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
