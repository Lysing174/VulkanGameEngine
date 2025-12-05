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
echo [1/2] Compiling PosColor...
%GLSLC% PosColorShader.vert -o PosColor.vert.spv
%GLSLC% PosColorShader.frag -o PosColor.frag.spv

rem --- 编译 Mesh Shader ---
echo [2/2] Compiling Mesh...
%GLSLC% MeshShader.vert -o Mesh.vert.spv
%GLSLC% MeshShader.frag -o Mesh.frag.spv

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