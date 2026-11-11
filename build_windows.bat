@echo off
REM ---------------------------------------------------------------------------
REM Build no Windows (Visual Studio 2022/2026 + CMake + git; vcpkg e baixado sozinho).
REM Pode rodar de qualquer prompt (nao precisa ser o "Native Tools").
REM Opcional: defina VCPKG_ROOT para usar um vcpkg ja existente (padrao: C:\vcpkg).
REM ---------------------------------------------------------------------------
setlocal
if "%VCPKG_ROOT%"=="" set "VCPKG_ROOT=C:\vcpkg"
cd /d "%~dp0"

where cmake >nul 2>nul || (echo cmake nao encontrado no PATH & exit /b 1)
where git   >nul 2>nul || (echo git nao encontrado no PATH & exit /b 1)

REM escolhe o gerador do Visual Studio suportado pelo cmake instalado
set "GEN="
cmake --help | findstr /C:"Visual Studio 18 2026" >nul && set "GEN=Visual Studio 18 2026"
if not defined GEN cmake --help | findstr /C:"Visual Studio 17 2022" >nul && set "GEN=Visual Studio 17 2022"
if not defined GEN (echo Nenhum gerador Visual Studio 2022/2026 no cmake & exit /b 1)
echo Gerador: %GEN%

if not exist fmt      git clone --depth 1 https://github.com/fmtlib/fmt.git            || exit /b 1
if not exist imgui    git clone --depth 1 https://github.com/ocornut/imgui.git         || exit /b 1
if not exist json     git clone --depth 1 https://github.com/nlohmann/json.git         || exit /b 1
if not exist raylib   git clone --depth 1 https://github.com/raysan5/raylib.git        || exit /b 1
if not exist rlImGui  git clone --depth 1 https://github.com/raylib-extras/rlImGui.git || exit /b 1

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
  if not exist "%VCPKG_ROOT%\bootstrap-vcpkg.bat" git clone --depth 1 https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%" || exit /b 1
  call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics || exit /b 1
)

"%VCPKG_ROOT%\vcpkg.exe" install "libavif[dav1d]:x64-windows" libwebp:x64-windows libjxl:x64-windows || exit /b 1

cmake -S . -B build-win -G "%GEN%" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows || exit /b 1
cmake --build build-win --config Release --target work || exit /b 1

echo.
echo Pronto: build-win\work\Release\work.exe
echo (as DLLs do vcpkg sao copiadas junto do .exe automaticamente)
endlocal
