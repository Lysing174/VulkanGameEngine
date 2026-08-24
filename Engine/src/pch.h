#pragma once

// Vulkan 的 NDC/裁剪深度范围是 [0,1]，必须在使用 GLM 投影/正交矩阵前定义，
// 否则 glm::perspective/glm::ortho 生成 OpenGL 风格 [-1,1] 深度，与 Vulkan
// viewport 的 [0,1] 约定冲突（近半深度被 clamp 到 0）。
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <iostream>
#include <memory>
#include <utility>
#include <algorithm>
#include <functional>

#include <string>
#include <sstream>
#include <array>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "Engine/Core/Log.h"

#ifdef HZ_PLATFORM_WINDOWS
#include <Windows.h>
#endif