@echo off
rem ========================================================
rem  Vulkan Shader 自动编译脚本
rem  注意：确保你的电脑安装了 Vulkan SDK 且配置了环境变量
rem ========================================================

rem 设置编译器路径 (如果 glslc 就在 PATH 里，可以直接写 glslc)
rem 如果不在 PATH 里，请修改为你 SDK 的实际路径，例如：
rem set GLSLC="C:\VulkanSDK\1.3.xxx.x\Bin\glslc.exe"
set GLSLC=glslc

echo Compiling Shaders...

rem --- 编译 PosColor Shader ---
echo [1/7] Compiling PosColor...

rem --- 编译 Mesh Shader ---
echo [2/7] Compiling Mesh...
%GLSLC% MeshShader.vert -o Mesh.vert.spv
%GLSLC% MeshShader.frag -o Mesh.frag.spv

rem --- 编译 Skybox Shader ---
echo [3/7] Compiling Skybox...
%GLSLC% Skybox.vert -o Skybox.vert.spv
%GLSLC% Skybox.frag -o Skybox.frag.spv

rem --- 编译 Shadow Shader (方向光阴影贴图) ---
echo [4/7] Compiling Shadow...
%GLSLC% Shadow.vert -o Shadow.vert.spv
%GLSLC% Shadow.frag -o Shadow.frag.spv

rem --- 编译 GGX Prefilter Compute Shader ---
echo [5/7] Compiling GGXPrefilter...
%GLSLC% GGXPrefilter.comp -o GGXPrefilter.comp.spv

rem --- 编译 GaussianSplat Shader ---
echo [6/7] Compiling GaussianSplat...
%GLSLC% GaussianSplat.vert -o GaussianSplat.vert.spv
%GLSLC% GaussianSplat.frag -o GaussianSplat.frag.spv

rem --- 编译 Gaussian Compute Shaders ---
echo [7/7] Compiling Gaussian Compute...
%GLSLC% GaussianDist.comp -o GaussianDist.comp.spv
%GLSLC% GaussianSortUpsweep.comp -o GaussianSortUpsweep.comp.spv
%GLSLC% GaussianSortSpine.comp -o GaussianSortSpine.comp.spv
%GLSLC% GaussianSortDownsweep.comp -o GaussianSortDownsweep.comp.spv


rem 检查是否有错误 (如果 glslc 返回非0，暂停看报错)
if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Shader compilation failed!
    pause
    exit /b %errorlevel%
)

echo.
echo [SUCCESS] All shaders compiled successfully!
pause