@echo off
setlocal
set "api=%PERLIN_API%"
if "%api%"=="" set "api=%~dp0..\build\_deps\perlin_api-src"
if not exist "%api%\build.cmd" (
    echo perlin-api not found at %api%: set PERLIN_API to a checkout of https://github.com/theperlinproject/perlin-api 1>&2
    exit /b 1
)
set "flag="
if /i "%~1"=="--wasm32" (set "flag=--wasm32" & shift)
if /i "%~1"=="--wasm64" (set "flag=--wasm64" & shift)
set "src=%~1"
if "%src%"=="" (
    echo usage: plugins\build.cmd [--wasm32^|--wasm64] ^<plugin.cpp^> [outdir]
    exit /b 2
)
set "out=%~2"
if "%out%"=="" set "out=%~dp0prebuilt"
call "%api%\build.cmd" %flag% "%src%" "%out%"
exit /b %errorlevel%
