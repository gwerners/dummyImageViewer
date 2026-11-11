@echo off
REM ---------------------------------------------------------------------------
REM Remove tudo que e gerado ou baixado, deixando so o codigo-fonte (para nao inflar o git):
REM   - produtos de build: build-win, build, .cache, .vs, *.user, *.ini
REM   - subprojetos clonados: fmt, imgui, json, raylib, rlImGui, stb
REM O vcpkg (VCPKG_ROOT / C:\vcpkg) nunca e tocado.
REM ---------------------------------------------------------------------------
setlocal
cd /d "%~dp0"

for %%D in (build-win build .cache .vs fmt imgui json raylib rlImGui stb) do if exist "%%D" (
  echo removendo %%D
  rmdir /s /q "%%D"
)
del /q /f *.user *.ini 2>nul

echo pronto
endlocal
