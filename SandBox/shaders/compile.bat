@echo off
setlocal
rem Pass --no-pause when invoking from a terminal or build tool.
pushd "%~dp0"
if errorlevel 1 exit /b 1

if not exist "spv" mkdir "spv"
if errorlevel 1 goto :error

set "GLSLC=glslc"
if defined VULKAN_SDK if exist "%VULKAN_SDK%\Bin\glslc.exe" set "GLSLC=%VULKAN_SDK%\Bin\glslc.exe"

echo Compiling shaders...

echo [1/6] Mesh
call :Compile "MeshShader.vert" "MeshShader.vert.spv" || goto :error
call :Compile "MeshShader.frag" "MeshShader.frag.spv" || goto :error
call :Compile "UnlitShader.vert" "UnlitShader.vert.spv" || goto :error
call :Compile "UnlitShader.frag" "UnlitShader.frag.spv" || goto :error

echo [2/6] Skybox
call :Compile "Skybox.vert" "Skybox.vert.spv" || goto :error
call :Compile "Skybox.frag" "Skybox.frag.spv" || goto :error

echo [3/6] Shadow
call :Compile "Shadow.vert" "Shadow.vert.spv" || goto :error
call :Compile "Shadow.frag" "Shadow.frag.spv" || goto :error

echo [4/6] GGX Prefilter
call :Compile "GGXPrefilter.comp" "GGXPrefilter.comp.spv" || goto :error

echo [5/6] Gaussian Splat
call :Compile "GaussianSplat.vert" "GaussianSplat.vert.spv" || goto :error
call :Compile "GaussianSplat.frag" "GaussianSplat.frag.spv" || goto :error

echo [6/6] Gaussian Compute
call :Compile "PrefixCull.comp" "PrefixCull.comp.spv" || goto :error
call :Compile "PrefixScan.comp" "PrefixScan.comp.spv" || goto :error
call :Compile "PrefixCompact.comp" "PrefixCompact.comp.spv" || goto :error
call :Compile "GaussianSortUpsweep.comp" "GaussianSortUpsweep.comp.spv" || goto :error
call :Compile "GaussianSortSpine.comp" "GaussianSortSpine.comp.spv" || goto :error
call :Compile "GaussianSortDownsweep.comp" "GaussianSortDownsweep.comp.spv" || goto :error

echo.
echo [SUCCESS] All shaders compiled successfully!
popd
if /I not "%~1"=="--no-pause" pause
exit /b 0

:Compile
echo   %~1
"%GLSLC%" "%~1" -o "spv\%~2"
exit /b %errorlevel%

:error
set "SHADER_EXIT_CODE=%errorlevel%"
echo.
echo [ERROR] Shader compilation failed. Check glslc output and VULKAN_SDK.
popd
if /I not "%~1"=="--no-pause" pause
exit /b %SHADER_EXIT_CODE%
