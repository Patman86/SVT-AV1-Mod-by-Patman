::
:: Copyright(c) 2019 Intel Corporation
::
:: This source code is subject to the terms of the BSD 2 Clause License and
:: the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
:: was not distributed with this source code in the LICENSE file, you can
:: obtain it at www.aomedia.org/license/software. If the Alliance for Open
:: Media Patent License 1.0 was not distributed with this source code in the
:: PATENTS file, you can obtain it at www.aomedia.org/license/patent.
::
@echo off

setlocal
cd /d "%~dp0"

:: Set defaults to prevent inheriting
set "build=y"
:: Default is debug
set "buildtype=Debug"
:: Default is shared
set "shared=ON"
set "GENERATOR="
:: (cmake -G 2>&1 | Select-String -SimpleMatch '*').Line.Split('=')[0].TrimEnd().Replace('* ','')
:: Default is not building unit tests
set "unittest=OFF"
if NOT -%1-==-- call :args %*
if %errorlevel%==1 exit /b 1
if exist CMakeCache.txt del /f /s /q CMakeCache.txt 1>nul
if exist CMakeFiles rmdir /s /q CMakeFiles 1>nul
if NOT "%GENERATOR%"=="" set GENERATOR=-G"%GENERATOR%"

echo Building in %buildtype% configuration

if NOT "%build%"=="y" echo Generating build files

if "%shared%"=="ON" (
    echo Building shared
) else (
    echo Building static
)

if "%dir%"=="MSVC" (
    if exist MSVC (
        cd MSVC
    ) else (
        mkdir MSVC && cd MSVC
    )
) else if "%dir%"=="GCC" (
    if exist GCC (
        cd GCC
    ) else (
        mkdir GCC && cd GCC
    )
)

set batdir=%~dp0

if "%unittest%"=="ON" echo Building unit tests

if "%vs%"=="2019" (
    cmake ../../.. %GENERATOR% -A x64 -DCMAKE_INSTALL_PREFIX=%SYSTEMDRIVE%\svt-encoders -DBUILD_SHARED_LIBS=%shared% -DBUILD_TESTING=%unittest% %cmake_eflags% || exit /b 1
) else if "%vs%"=="2022" (
    cmake ../../.. %GENERATOR% -A x64 -DCMAKE_INSTALL_PREFIX=%SYSTEMDRIVE%\svt-encoders -DBUILD_SHARED_LIBS=%shared% -DBUILD_TESTING=%unittest% %cmake_eflags% || exit /b 1
) else (
    cmake ../../.. %GENERATOR% -DCMAKE_INSTALL_PREFIX=%SYSTEMDRIVE%\svt-encoders -DBUILD_SHARED_LIBS=%shared% -DBUILD_TESTING=%unittest% %cmake_eflags% || exit /b 1
)

if "%build%"=="y" cmake --build . --config %buildtype%
goto :EOF

:args
if -%1-==-- (
    exit /b
) else if /I "%1"=="/help" (
    call :help
) else if /I "%1"=="help" (
    call :help
) else if /I "%1"=="clean" (
    echo Cleaning build folder
    for %%i in (*) do if not "%%~i" == "build.bat" del "%%~i"
    for /d %%i in (*) do if not "%%~i" == "build.bat" (
        del /f /s /q "%%~i" 1>nul
        rmdir /s /q "%%~i" 1>nul
    )
    exit /b
) else if /I "%1"=="2022" (
    echo Generating Visual Studio 2022 solution
    set "GENERATOR=Visual Studio 17 2022"
    set vs=2022
    set dir=MSVC
    shift
) else if /I "%1"=="2019" (
    echo Generating Visual Studio 2019 solution
    set "GENERATOR=Visual Studio 16 2019"
    set vs=2019
    set dir=MSVC
    shift
) else if /I "%1"=="2017" (
    echo Generating Visual Studio 2017 solution
    set "GENERATOR=Visual Studio 15 2017 Win64"
    set vs=2017
    set dir=MSVC
    shift
) else if /I "%1"=="2015" (
    echo Generating Visual Studio 2015 solution
    set "GENERATOR=Visual Studio 14 2015 Win64"
    set vs=2015
    set dir=MSVC
    shift
) else if /I "%1"=="ninja" (
    echo Generating Ninja files
    set "GENERATOR=Ninja"
    set dir=GCC
    shift
) else if /I "%1"=="msys" (
    echo Generating MSYS Makefiles
    set "GENERATOR=MSYS Makefiles"
    set dir=GCC
    shift
) else if /I "%1"=="mingw" (
    echo Generating MinGW Makefiles
    set "GENERATOR=MinGW Makefiles"
    set dir=GCC
    shift
) else if /I "%1"=="unix" (
    echo Generating Unix Makefiles
    set "GENERATOR=Unix Makefiles"
    set dir=GCC
    shift
) else if /I "%1"=="release" (
    set "buildtype=Release"
    shift
) else if /I "%1"=="debug" (
    set "buildtype=Debug"
    shift
) else if /I "%1"=="RelWithDebInfo" (
    set "buildtype=RelWithDebInfo"
    shift
) else if /I "%1"=="test" (
    set "unittest=ON"
    shift
) else if /I "%1"=="static" (
    set "shared=OFF"
    shift
) else if /I "%1"=="shared" (
    set "shared=ON"
    shift
) else if /I "%1"=="nobuild" (
    set "build=n"
    shift
) else if /I "%1"=="c-only" (
    set "cmake_eflags=%cmake_eflags% -DCOMPILE_C_ONLY=ON"
    shift
) else if /I "%1"=="avx512" (
    set "cmake_eflags=%cmake_eflags% -DENABLE_AVX512=ON"
    shift
) else if /I "%1"=="dovi" (
    set "cmake_eflags=%cmake_eflags% -DLIBDOVI_FOUND=1"
    shift
) else if /I "%1"=="hdr" (
    set "cmake_eflags=%cmake_eflags% -DLIBHDR10PLUS_RS_FOUND=1"
    shift
) else if /I "%1"=="lto" (
    set "cmake_eflags=%cmake_eflags% -DSVT_AV1_LTO=ON"
    shift
) else if /I "%1"=="no-enc" (
    set "cmake_eflags=%cmake_eflags% -DBUILD_ENC=OFF"
    shift
) else if /I "%1"=="no-apps" (
    set "cmake_eflags=%cmake_eflags% -DBUILD_APPS=OFF"
    shift
) else if /I "%1"=="external-cpuinfo" (
    set "cmake_eflags=%cmake_eflags% -DUSE_EXTERNAL_CPUINFO=ON"
    shift
)  else (
    echo Unknown argument "%1"
    call :help
    goto :EOF
)
goto :args

:help
    echo Batch file to build SVT-AV1 on Windows
    echo Usage: build.bat [2022^|2019^|2017^|2015^|clean] [release^|debug] [nobuild] [test] [shared^|static] [c-only] [avx512] [dovi] [hdr] [no-apps] [no-enc]
    exit /b 1
goto :EOF
