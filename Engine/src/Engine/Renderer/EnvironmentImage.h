#pragma once

#include <string>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "Platform/Vulkan/VulkanCubeTexture.h"

namespace Engine {
	
class MipmapGenerator;

// ============================================================
// SHCoefficients — 3-band Spherical Harmonics (9 coeffs × RGB)
//
// Standard real SH basis (normalized).  Stored as vec3 for
// convenience; GPU-friendly packing via GetSHData() returns
// an array of 9 × vec4 (w unused) for direct UBO/SSBO upload.
// ============================================================
struct SHCoefficients
{
	glm::vec3 L00;		// Band 0  m=0   (ambient — Y00)
	glm::vec3 L1m1;		// Band 1  m=-1  (Y1,-1)
	glm::vec3 L10;		// Band 1  m=0   (Y1,0)
	glm::vec3 L1p1;		// Band 1  m=+1  (Y1,1)
	glm::vec3 L2m2;		// Band 2  m=-2  (Y2,-2)
	glm::vec3 L2m1;		// Band 2  m=-1  (Y2,-1)
	glm::vec3 L20;		// Band 2  m=0   (Y2,0)
	glm::vec3 L2p1;		// Band 2  m=+1  (Y2,1)
	glm::vec3 L2p2;		// Band 2  m=+2  (Y2,2)
};

// ============================================================
// EnvironmentImage
//
// Reads an HDR equirectangular panorama, converts it to a
// cubemap with a full GGX pre-filtered mip-chain (via compute
// shader), and computes 3-band spherical harmonics for
// low-frequency irradiance reconstruction.
//
// Usage:
//   auto env = std::make_shared<EnvironmentImage>("path/to/sky.hdr");
//   env->Initialize();   // one-time setup (OnAttach)
// ============================================================
class EnvironmentImage
{
public:
	explicit EnvironmentImage(const std::string& hdrPath);
	~EnvironmentImage();

	// ---- Accessors ----

	const std::string& GetPath()						const { return m_HdrPath; }
	const std::shared_ptr<VulkanCubeTexture>& GetCubeMap() const { return m_CubeMap; }
	const SHCoefficients& GetSH()						const { return m_SH; }
	bool IsLoaded()										const { return m_Loaded; }

	// GPU-friendly upload: 9 × vec4 (L00, L1m1, L10, L1p1, L2m2, L2m1, L20, L2p1, L2p2)
	std::vector<glm::vec4> GetSHData() const;

	// ---- Lifecycle ----

	// One-time initialization: load HDR → cubemap → GGX mipmap → SH
	// Must be called after VulkanContext has been fully initialized.
	void Initialize();

private:
	// ====== Internal helpers ======

	void LoadHDR();
	void EquirectToCubemap();
	void UploadCubemapToGPU();
	void ComputeSH();

	// Bilinear sample on an HDR equirectangular map (RGB data, 3 ch, pitch)
	static glm::vec3 SampleEquirect(const float* data, int w, int h, glm::vec2 uv);

	// Direction vector → cubemap UV on a single face
	static glm::vec2 DirToUV(int face, const glm::vec3& dir);

	// ====== Internal data ======

	std::string m_HdrPath;

	// Raw HDR pixels in RGBA (loaded by stbi_loadf, 4 ch)
	int m_HdrWidth  = 0;
	int m_HdrHeight = 0;
	std::vector<float> m_HdrPixels;

	// Cubemap face resolution (configurable, default 512)
	int m_FaceSize  = 512;
	int m_MipLevels = 0;

	// Per-face base-level data only: faceData[face * faceSize² * 4]
	// (Mipmap chain is generated on GPU via MipmapGenerator)
	std::vector<float> m_FaceBaseData;

	// GPU resources
	std::shared_ptr<VulkanCubeTexture> m_CubeMap;

	// GGX pre-filter mipmap generator (GPU compute shader)
	std::unique_ptr<MipmapGenerator> m_MipmapGenerator;

	// Spherical harmonics
	SHCoefficients m_SH{};

	bool m_Loaded = false;
};

} // namespace Engine
