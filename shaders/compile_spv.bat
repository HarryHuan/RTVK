@echo off
REM RTVK SPIR-V Shader Compilation Script
REM Requires: glslc (from Vulkan SDK) on PATH

setlocal
set SHADER_DIR=%~dp0
set OUTPUT_DIR=%SHADER_DIR%

echo ==================== Compiling Shaders ====================

REM ?? Common ????????????????????????????????????
echo [common]
glslc -fshader-stage=vert   "%SHADER_DIR%\common\fullscreen.vert" -o "%OUTPUT_DIR%\common\fullscreen.vert.spv" 2>&1
glslc -fshader-stage=frag   "%SHADER_DIR%\common\fullscreen.frag" -o "%OUTPUT_DIR%\common\fullscreen.frag.spv" 2>&1

REM ?? Simulation ????????????????????????????????
echo [sim]
glslc -fshader-stage=comp   "%SHADER_DIR%\sim\spatial_hash.comp"   -o "%OUTPUT_DIR%\sim\spatial_hash.comp.spv" 2>&1
glslc -fshader-stage=comp   "%SHADER_DIR%\sim\constraint.comp"    -o "%OUTPUT_DIR%\sim\constraint.comp.spv" 2>&1
glslc -fshader-stage=comp   "%SHADER_DIR%\sim\compact.comp"     -o "%OUTPUT_DIR%\sim\compact.comp.spv" 2>&1
glslc -fshader-stage=comp   "%SHADER_DIR%\sim\integrate.comp"     -o "%OUTPUT_DIR%\sim\integrate.comp.spv" 2>&1

REM ?? Render ????????????????????????????????????
echo [render]
glslc -fshader-stage=vert   "%SHADER_DIR%\particle\particle.vert"   -o "%OUTPUT_DIR%\particle\particle.vert.spv" 2>&1
glslc -fshader-stage=frag   "%SHADER_DIR%\particle\particle.frag"   -o "%OUTPUT_DIR%\particle\particle.frag.spv" 2>&1   -o "%OUTPUT_DIR%\render\material.frag.spv" 2>&1
REM glslc -fshader-stage=frag   "%SHADER_DIR%\render\post.frag"       -o "%OUTPUT_DIR%\render\post.frag.spv" 2>&1
REM glslc -fshader-stage=frag   "%SHADER_DIR%\render\skybox.frag"     -o "%OUTPUT_DIR%\render\skybox.frag.spv" 2>&1

echo ======================= Done ==============================
endlocal


