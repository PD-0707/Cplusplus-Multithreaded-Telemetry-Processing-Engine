@echo off
setlocal

echo ========================================================
echo   Building C++ Multithreaded Telemetry Processing Engine
echo ========================================================

rem Locate Visual Studio Build Tools environment
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
) else (
    echo Note: vcvars64.bat not found in default path. Assuming compiler environment is already in PATH.
)

echo Compiling src\main.cpp with C++17 and /O2 optimizations...
cl.exe /std:c++17 /O2 /EHsc /W3 /Iinclude src\main.cpp /Fe:TelemetryEngine.exe

if %ERRORLEVEL% equ 0 (
    echo.
    echo Build SUCCESSFUL: TelemetryEngine.exe created!
    echo.
    echo To run simulation:  .\TelemetryEngine.exe
    echo To run benchmark:   .\TelemetryEngine.exe --benchmark
) else (
    echo.
    echo Build FAILED with exit code %ERRORLEVEL%.
)

endlocal

