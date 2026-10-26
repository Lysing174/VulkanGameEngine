#include "pch.h"
#include "Engine/Renderer/DatasetCamera.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Engine {

    DatasetCamera::DatasetCamera(const Intrinsics& intrinsics,
                                 const glm::quat& qvec, const glm::vec3& tvec,
                                 float nearClip, float farClip)
        : m_Intrinsics(intrinsics), m_NearClip(nearClip), m_FarClip(farClip)
    {
        SetPose(qvec, tvec);   // 内部同时重算投影与视图
    }

    void DatasetCamera::SetIntrinsics(const Intrinsics& intrinsics)
    {
        m_Intrinsics = intrinsics;
        RecalculateProjection();
    }

    void DatasetCamera::SetPose(const glm::quat& qvec, const glm::vec3& tvec)
    {
        m_Qvec = qvec;
        m_Tvec = tvec;
        RecalculateProjection();
        RecalculateView();
    }

    void DatasetCamera::SetClipPlanes(float nearClip, float farClip)
    {
        m_NearClip = nearClip;
        m_FarClip  = farClip;
        RecalculateProjection();
    }

    void DatasetCamera::ApplyScale(float scale)
    {
        if (scale <= 0.0f) return;

        m_Intrinsics.fx *= scale;
        m_Intrinsics.fy *= scale;
        m_Intrinsics.cx *= scale;
        m_Intrinsics.cy *= scale;
        m_Intrinsics.Width  = std::max(1u, (uint32_t)std::round(m_Intrinsics.Width  * scale));
        m_Intrinsics.Height = std::max(1u, (uint32_t)std::round(m_Intrinsics.Height * scale));

        RecalculateProjection();
    }

    glm::vec3 DatasetCamera::GetForwardDirection() const
    {
        // 世界空间前向 = R^T * (0,0,1) = R 的第三行
        const glm::mat3 R = glm::mat3_cast(glm::normalize(m_Qvec));
        return glm::normalize(glm::vec3(R[0][2], R[1][2], R[2][2]));
    }

    void DatasetCamera::RecalculateProjection()
    {
        const float W = (float)m_Intrinsics.Width;
        const float H = (float)m_Intrinsics.Height;
        if (W <= 0.0f || H <= 0.0f || m_Intrinsics.fx <= 0.0f || m_Intrinsics.fy <= 0.0f)
            return;

        // 由内参直接构造 (glm::perspective 不支持主点偏移)。
        // 推导 (OpenCV: u = fx*X/Z + cx, v = fy*Y/Z + cy; GL NDC y 向上):
        //   ndc.x = 2u/W - 1  →  clip.x = (2fx/W)x + (1 - 2cx/W)z
        //   ndc.y = 1 - 2v/H  →  clip.y = (2fy/H)y + (2cy/H - 1)z
        // 深度 [0,1] (GLM_FORCE_DEPTH_ZERO_TO_ONE), clip.w = -z。
        glm::mat4 p = glm::mat4(0.0f);
        p[0][0] =  2.0f * m_Intrinsics.fx / W;
        p[1][1] =  2.0f * m_Intrinsics.fy / H;
        p[2][0] =  1.0f - 2.0f * m_Intrinsics.cx / W;
        p[2][1] =  2.0f * m_Intrinsics.cy / H - 1.0f;
        p[2][2] =  m_FarClip / (m_NearClip - m_FarClip);
        p[2][3] = -1.0f;
        p[3][2] = (m_FarClip * m_NearClip) / (m_NearClip - m_FarClip);

        m_Projection = p;
    }

    void DatasetCamera::RecalculateView()
    {
        // COLMAP: X_cam = R * X_world + T  (x 右, y 下, z 前)
        const glm::mat3 R = glm::mat3_cast(glm::normalize(m_Qvec));

        glm::mat4 viewCV = glm::mat4(1.0f);
        viewCV[0] = glm::vec4(R[0], 0.0f);
        viewCV[1] = glm::vec4(R[1], 0.0f);
        viewCV[2] = glm::vec4(R[2], 0.0f);
        viewCV[3] = glm::vec4(m_Tvec, 1.0f);

        // OpenCV (y 下, z 前) → OpenGL (y 上, -z 前)
        const glm::mat4 cvToGL = glm::mat4(
             1.0f,  0.0f,  0.0f, 0.0f,
             0.0f, -1.0f,  0.0f, 0.0f,
             0.0f,  0.0f, -1.0f, 0.0f,
             0.0f,  0.0f,  0.0f, 1.0f);

        m_ViewMatrix = cvToGL * viewCV;

        // 相机世界坐标: R * C + T = 0 → C = -R^T * T
        m_Position = -glm::transpose(R) * m_Tvec;
    }

    std::vector<DatasetCamera> DatasetCamera::LoadFromColmapTxt(const std::string& camerasTxtPath,
                                                                const std::string& imagesTxtPath)
    {
        std::vector<DatasetCamera> cameras;

        // ---------------- cameras.txt ----------------
        std::unordered_map<uint32_t, Intrinsics> intrinsicsMap;
        {
            std::ifstream in(camerasTxtPath);
            if (!in)
            {
                EG_CORE_ERROR("DatasetCamera: cannot open cameras file '{0}'", camerasTxtPath);
                return cameras;
            }

            std::string line;
            while (std::getline(in, line))
            {
                if (line.empty() || line[0] == '#') continue;

                std::istringstream iss(line);
                uint32_t cameraId = 0, width = 0, height = 0;
                std::string model;
                if (!(iss >> cameraId >> model >> width >> height)) continue;

                std::vector<double> params;
                double value = 0.0;
                while (iss >> value) params.push_back(value);

                Intrinsics intr;
                intr.Width  = width;
                intr.Height = height;

                if (model == "SIMPLE_PINHOLE" && params.size() >= 3)
                {
                    intr.fx = intr.fy = (float)params[0];
                    intr.cx = (float)params[1];
                    intr.cy = (float)params[2];
                }
                else if (model == "PINHOLE" && params.size() >= 4)
                {
                    intr.fx = (float)params[0];
                    intr.fy = (float)params[1];
                    intr.cx = (float)params[2];
                    intr.cy = (float)params[3];
                }
                else if (params.size() >= 4)
                {
                    // OPENCV / RADIAL / SIMPLE_RADIAL ...: 取前 4 个参数, 畸变暂忽略
                    EG_CORE_WARN("DatasetCamera: camera model '{0}' has distortion, "
                                 "using fx/fy/cx/cy only (distortion ignored)", model);
                    intr.fx = (float)params[0];
                    intr.fy = (float)params[1];
                    intr.cx = (float)params[2];
                    intr.cy = (float)params[3];
                }
                else
                {
                    EG_CORE_WARN("DatasetCamera: unsupported camera model '{0}' (id={1}), skipped",
                                 model, cameraId);
                    continue;
                }

                intrinsicsMap[cameraId] = intr;
            }
        }

        // ---------------- images.txt ----------------
        {
            std::ifstream in(imagesTxtPath);
            if (!in)
            {
                EG_CORE_ERROR("DatasetCamera: cannot open images file '{0}'", imagesTxtPath);
                return cameras;
            }

            std::string line;
            bool expectImageLine = true;
            while (std::getline(in, line))
            {
                if (line.empty() || line[0] == '#') continue;

                if (!expectImageLine)
                {
                    expectImageLine = true;   // 跳过 POINTS2D 行
                    continue;
                }

                std::istringstream iss(line);
                uint32_t imageId = 0, cameraId = 0;
                double qw = 1.0, qx = 0.0, qy = 0.0, qz = 0.0;
                double tx = 0.0, ty = 0.0, tz = 0.0;
                std::string name;
                if (!(iss >> imageId >> qw >> qx >> qy >> qz >> tx >> ty >> tz >> cameraId >> name))
                {
                    expectImageLine = false;
                    continue;
                }
                expectImageLine = false;

                auto it = intrinsicsMap.find(cameraId);
                if (it == intrinsicsMap.end())
                {
                    EG_CORE_WARN("DatasetCamera: image '{0}' references unknown camera id {1}, skipped",
                                 name, cameraId);
                    continue;
                }

                DatasetCamera camera(it->second,
                                     glm::quat((float)qw, (float)qx, (float)qy, (float)qz),
                                     glm::vec3((float)tx, (float)ty, (float)tz));
                camera.SetName(name);
                cameras.push_back(std::move(camera));
            }
        }

        // 以文件名排序, 保证多次运行结果稳定
        std::sort(cameras.begin(), cameras.end(),
                  [](const DatasetCamera& a, const DatasetCamera& b) { return a.GetName() < b.GetName(); });

        EG_CORE_INFO("DatasetCamera: loaded {0} camera(s) from '{1}' + '{2}'",
                     cameras.size(), camerasTxtPath, imagesTxtPath);
        return cameras;
    }

    // 按 COLMAP 相机模型 id 返回参数个数 (与 COLMAP 源码 camera_models.h 一致)
    static size_t ColmapModelParamCount(int32_t modelId)
    {
        switch (modelId)
        {
            case 0:  return 3;   // SIMPLE_PINHOLE
            case 1:  return 4;   // PINHOLE
            case 2:  return 4;   // SIMPLE_RADIAL
            case 3:  return 5;   // RADIAL
            case 4:  return 8;   // OPENCV
            case 5:  return 8;   // OPENCV_FISHEYE
            case 6:  return 12;  // FULL_OPENCV
            case 7:  return 5;   // FOV
            case 8:  return 4;   // SIMPLE_RADIAL_FISHEYE
            case 9:  return 6;   // RADIAL_FISHEYE
            case 10: return 12;  // THIN_PRISM_FISHEYE
            default: return 4;
        }
    }

    // 统一分发: 按 cameras 文件扩展名选 txt / bin
    std::vector<DatasetCamera> DatasetCamera::LoadFromColmap(const std::string& camerasPath,
                                                            const std::string& imagesPath)
    {
        const size_t n = camerasPath.size();
        if (n >= 4 && camerasPath.compare(n - 4, 4, ".bin") == 0)
            return LoadFromColmapBin(camerasPath, imagesPath);
        return LoadFromColmapTxt(camerasPath, imagesPath);
    }

    std::vector<DatasetCamera> DatasetCamera::LoadFromColmapBin(const std::string& camerasBinPath,
                                                                const std::string& imagesBinPath)
    {
        std::vector<DatasetCamera> cameras;

        // ---------------- cameras.bin (little-endian) ----------------
        // uint64 num_cameras
        //   loop: int32 id, int32 model_id, int64 w, int64 h, double params[N]
        std::unordered_map<uint32_t, Intrinsics> intrinsicsMap;
        {
            std::ifstream in(camerasBinPath, std::ios::binary);
            if (!in)
            {
                EG_CORE_ERROR("DatasetCamera: cannot open cameras file '{0}'", camerasBinPath);
                return cameras;
            }

            uint64_t numCameras = 0;
            in.read(reinterpret_cast<char*>(&numCameras), sizeof(uint64_t));

            for (uint64_t i = 0; i < numCameras; ++i)
            {
                int32_t cameraId = 0, modelId = 0;
                int64_t width = 0, height = 0;
                in.read(reinterpret_cast<char*>(&cameraId), sizeof(int32_t));
                in.read(reinterpret_cast<char*>(&modelId),  sizeof(int32_t));
                in.read(reinterpret_cast<char*>(&width),    sizeof(int64_t));
                in.read(reinterpret_cast<char*>(&height),   sizeof(int64_t));

                const size_t nParams = ColmapModelParamCount(modelId);
                std::vector<double> params(nParams);
                in.read(reinterpret_cast<char*>(params.data()),
                        (std::streamsize)(nParams * sizeof(double)));

                Intrinsics intr;
                intr.Width  = (uint32_t)width;
                intr.Height = (uint32_t)height;

                if (modelId == 0 && params.size() >= 3)          // SIMPLE_PINHOLE
                {
                    intr.fx = intr.fy = (float)params[0];
                    intr.cx = (float)params[1];
                    intr.cy = (float)params[2];
                }
                else if (modelId == 1 && params.size() >= 4)     // PINHOLE
                {
                    intr.fx = (float)params[0];
                    intr.fy = (float)params[1];
                    intr.cx = (float)params[2];
                    intr.cy = (float)params[3];
                }
                else if (params.size() >= 4)                     // 其它 (含 OPENCV 等): 取前 4, 畸变忽略
                {
                    EG_CORE_WARN("DatasetCamera: camera model id {0} has distortion, "
                                 "using fx/fy/cx/cy only (distortion ignored)", modelId);
                    intr.fx = (float)params[0];
                    intr.fy = (float)params[1];
                    intr.cx = (float)params[2];
                    intr.cy = (float)params[3];
                }
                else
                {
                    EG_CORE_WARN("DatasetCamera: unsupported camera model id {0} (id={1}), skipped",
                                 modelId, cameraId);
                    continue;
                }

                intrinsicsMap[(uint32_t)cameraId] = intr;
            }
        }

        // ---------------- images.bin (little-endian) ----------------
        // uint64 num_images
        //   loop: int32 id, double qw qx qy qz, double tx ty tz, int32 camera_id,
        //         char name[] (NUL-terminated),
        //         uint64 num_points2D, (double x, double y, int64 point3D_id) * num_points2D
        {
            std::ifstream in(imagesBinPath, std::ios::binary | std::ios::ate);
            if (!in)
            {
                EG_CORE_ERROR("DatasetCamera: cannot open images file '{0}'", imagesBinPath);
                return {};
            }

            const std::streamoff fileSize = in.tellg();
            in.seekg(0, std::ios::beg);
            uint64_t numImages = 0;
            in.read(reinterpret_cast<char*>(&numImages), sizeof(numImages));
            constexpr uint64_t minImageBytes = 2 * sizeof(int32_t) + 7 * sizeof(double) + 1 + sizeof(uint64_t);
            if (!in || numImages > (uint64_t)(fileSize - (std::streamoff)sizeof(numImages)) / minImageBytes)
            {
                EG_CORE_ERROR("DatasetCamera: invalid images.bin header ('{0}')", imagesBinPath);
                return {};
            }

            for (uint64_t imageIndex = 0; imageIndex < numImages; ++imageIndex)
            {
                const std::streamoff recordOffset = in.tellg();
                auto fail = [&](const char* reason) -> std::vector<DatasetCamera>
                {
                    EG_CORE_ERROR("DatasetCamera: invalid image record {0} at byte {1} in '{2}': {3}",
                                  imageIndex + 1, recordOffset, imagesBinPath, reason);
                    return {};
                };

                int32_t imageId = 0, cameraId = 0;
                double qw = 1.0, qx = 0.0, qy = 0.0, qz = 0.0;
                double tx = 0.0, ty = 0.0, tz = 0.0;
                in.read(reinterpret_cast<char*>(&imageId), sizeof(imageId));
                in.read(reinterpret_cast<char*>(&qw), sizeof(qw));
                in.read(reinterpret_cast<char*>(&qx), sizeof(qx));
                in.read(reinterpret_cast<char*>(&qy), sizeof(qy));
                in.read(reinterpret_cast<char*>(&qz), sizeof(qz));
                in.read(reinterpret_cast<char*>(&tx), sizeof(tx));
                in.read(reinterpret_cast<char*>(&ty), sizeof(ty));
                in.read(reinterpret_cast<char*>(&tz), sizeof(tz));
                in.read(reinterpret_cast<char*>(&cameraId), sizeof(cameraId));
                if (!in) return fail("truncated pose");

                std::string name;
                std::getline(in, name, '\0');
                if (!in || in.eof()) return fail("missing NUL terminator in image name");

                uint64_t numPoints2D = 0;
                in.read(reinterpret_cast<char*>(&numPoints2D), sizeof(numPoints2D));
                if (!in) return fail("missing point count");

                constexpr uint64_t pointBytes = 2 * sizeof(double) + sizeof(int64_t);
                const std::streamoff remainingBytes = fileSize - (std::streamoff)in.tellg();
                if (remainingBytes < 0 || numPoints2D > (uint64_t)remainingBytes / pointBytes)
                    return fail("point count exceeds remaining file size");
                in.seekg((std::streamoff)(numPoints2D * pointBytes), std::ios::cur);
                if (!in) return fail("cannot skip image observations");

                auto it = intrinsicsMap.find((uint32_t)cameraId);
                if (it == intrinsicsMap.end())
                {
                    EG_CORE_WARN("DatasetCamera: image '{0}' references unknown camera id {1}, skipped",
                                 name, cameraId);
                    continue;
                }

                DatasetCamera camera(it->second,
                                     glm::quat((float)qw, (float)qx, (float)qy, (float)qz),
                                     glm::vec3((float)tx, (float)ty, (float)tz));
                camera.SetName(name);
                cameras.push_back(std::move(camera));
            }

            if ((std::streamoff)in.tellg() != fileSize)
            {
                EG_CORE_ERROR("DatasetCamera: unexpected trailing bytes in images file '{0}'", imagesBinPath);
                return {};
            }
        }

        std::sort(cameras.begin(), cameras.end(),
                  [](const DatasetCamera& a, const DatasetCamera& b) { return a.GetName() < b.GetName(); });

        EG_CORE_INFO("DatasetCamera: loaded {0} camera(s) from '{1}' + '{2}'",
                     cameras.size(), camerasBinPath, imagesBinPath);
        return cameras;
    }

}
