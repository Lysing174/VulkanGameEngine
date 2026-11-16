#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>

namespace Engine {

    enum class RenderStage : uint32_t
    {
        Shadow, Mesh, GaussianPrepare, HiZBuild, PrefixCull, ScanCompact, RadixSort, Gaussian, Count
    };

    constexpr size_t RenderStageCount = static_cast<size_t>(RenderStage::Count);

    struct GpuTimings
    {
        std::array<double, RenderStageCount> Milliseconds = {};
        bool Supported = false;
        bool Valid = false;

        double Get(RenderStage stage) const { return Milliseconds[static_cast<size_t>(stage)]; }
        double TotalRenderMilliseconds() const
        {
            // GaussianPrepare already includes its four sub-stages.
            return Get(RenderStage::Shadow) + Get(RenderStage::Mesh)
                + Get(RenderStage::GaussianPrepare) + Get(RenderStage::Gaussian);
        }
    };

    enum class CpuSyncStage : size_t
    {
        QueueWaitIdle, Acquire, Present, Count
    };

    constexpr size_t CpuSyncStageCount = static_cast<size_t>(CpuSyncStage::Count);
    using CpuSyncTimings = std::array<double, CpuSyncStageCount>;

    struct CpuTimings
    {
        double FrameMilliseconds = 0.0;
        CpuSyncTimings SyncMilliseconds = {};

        double OtherMilliseconds() const
        {
            return FrameMilliseconds - std::accumulate(SyncMilliseconds.begin(), SyncMilliseconds.end(), 0.0);
        }
    };

    struct RendererStatistics
    {
        uint64_t SceneGaussianCount = 0;
        uint64_t FrustumVisibleGaussianCount = 0;
        uint64_t DrawnGaussianCount = 0;
        uint64_t GaussianDataBytes = 0;
        uint64_t SceneVertexCount = 0;
        uint64_t DrawnVertexCount = 0;
        uint64_t PointLightCount = 0;
        uint64_t DirectLightCount = 0;
        CpuTimings CPU;
        GpuTimings GPU;
        bool HasCompletedFrame = false;
    };
}
