#pragma once

#include "Engine/Renderer/Camera.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>

namespace Engine {

    // 数据集相机: 由数据集内参 (fx, fy, cx, cy) + 外参 (COLMAP qvec/tvec) 直接构造,
    // 不含任何交互逻辑, 供离线测试/评测流程驱动 Renderer::BeginScene 使用。
    //
    // 约定:
    //   - 外参为 COLMAP / OpenCV 约定: X_cam = R * X_world + T (相机看向 +Z, 像平面 y 向下)。
    //   - 内参单位为像素; 主点 (cx, cy) 允许偏离图像中心 (由投影矩阵 y 行的偏移项表达)。
    //   - 生成的是 OpenGL 约定投影矩阵 (y 向上, 深度 [0,1]);
    //     Vulkan 的 y 翻转由 Renderer::BeginScene 统一处理 (整行翻转, 保证主点偏移方向正确)。
    class DatasetCamera : public Camera
    {
    public:
        struct Intrinsics
        {
            float    fx = 0.0f, fy = 0.0f;   // 焦距 (像素)
            float    cx = 0.0f, cy = 0.0f;   // 主点 (像素)
            uint32_t Width = 0, Height = 0;  // 该相机对应的图像分辨率
        };

        DatasetCamera() = default;

        DatasetCamera(const Intrinsics& intrinsics,
                      const glm::quat& qvec, const glm::vec3& tvec,
                      float nearClip = 0.01f, float farClip = 1000.0f);

        void SetIntrinsics(const Intrinsics& intrinsics);
        void SetPose(const glm::quat& qvec, const glm::vec3& tvec);
        void SetClipPlanes(float nearClip, float farClip);
        void SetName(const std::string& name) { m_Name = name; }

        const Intrinsics& GetIntrinsics() const { return m_Intrinsics; }
        const std::string& GetName() const { return m_Name; }

        // 分辨率缩放 (低分辨率快速验证): 内参与图像尺寸同步缩放, 视场保持不变
        void ApplyScale(float scale);

        // ---- Camera 接口 ----
        const glm::mat4& GetViewMatrix() const override { return m_ViewMatrix; }
        const glm::vec3& GetPosition() const override { return m_Position; }
        glm::vec3 GetForwardDirection() const override;

        // ---- COLMAP 加载 (按扩展名自动选 txt / bin) ----
        // 文本:  cameras.txt "<id> <model> <w> <h> <params...>"
        //        images.txt  "<id> <qw> <qx> <qy> <qz> <tx> <ty> <tz> <camera_id> <name>" + POINTS2D 行
        // 二进制: cameras.bin / images.bin (little-endian), 字段与文本一致
        // 支持 PINHOLE / SIMPLE_PINHOLE; 带畸变模型 (OPENCV/RADIAL...) 取前 4 个参数
        // 作为 fx fy cx cy 并给出警告 (畸变本身暂不处理)。
        static std::vector<DatasetCamera> LoadFromColmap(const std::string& camerasPath,
                                                          const std::string& imagesPath);

        static std::vector<DatasetCamera> LoadFromColmapTxt(const std::string& camerasTxtPath,
                                                            const std::string& imagesTxtPath);

        static std::vector<DatasetCamera> LoadFromColmapBin(const std::string& camerasBinPath,
                                                            const std::string& imagesBinPath);

    private:
        void RecalculateProjection();
        void RecalculateView();

    private:
        Intrinsics   m_Intrinsics;
        float        m_NearClip = 0.01f, m_FarClip = 1000.0f;

        // 外参 (COLMAP: world -> camera)
        glm::quat    m_Qvec = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3    m_Tvec = glm::vec3(0.0f);

        glm::mat4    m_ViewMatrix = glm::mat4(1.0f);
        glm::vec3    m_Position   = glm::vec3(0.0f);
        std::string  m_Name;
    };

}
