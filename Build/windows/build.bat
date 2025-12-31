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

echo %text%

echo Building in %buildtype% configuration

if NOT "%build%"=="y" echo Generating build files

if "%shared%"=="ON" (
    echo Building shared
) else (
    echo Building static
)

if not exist "%dir%" mkdir "%dir%"
cd "%dir%"

set batdir=%~dp0

if "%unittest%"=="ON" echo Building unit tests

set "ARCH_OPTION="
if not "%vs%"=="" set "ARCH_OPTION=-A x64"

cmake --fresh ../../.. %GENERATOR% %ARCH_OPTION% %tool% -DCMAKE_BUILD_TYPE=%buildtype% -DCMAKE_INSTALL_PREFIX=%SYSTEMDRIVE%\svt-encoders -DBUILD_SHARED_LIBS=%shared% -DBUILD_TESTING=%unittest% %cmake_eflags% -DCMAKE_CXX_FLAGS_RELEASE="%flags%" -DCMAKE_C_FLAGS_RELEASE="%flags%"|| exit /b 1

if "%build%"=="y" cmake --build . --config %buildtype% --parallel --clean-first

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
    set "text=Setting environment for Visual Studio 2022"
    set "GENERATOR=Visual Studio 17 2022"
    set vs=2022
    set dir=MSVC
    set "flags=/MD /O2 /Ob3 /Gw /GL /DNDEBUG"
    shift
) else if /I "%1"=="2019" (
    set "text=Setting environment for Visual Studio 2019"
    set "GENERATOR=Visual Studio 16 2019"
    set vs=2019
    set dir=MSVC
    set "flags=/MD /O2 /Ob3 /Gw /GL /DNDEBUG"
    shift
) else if /I "%1"=="2017" (
    set "text=Setting environment for Visual Studio 2017"
    set "GENERATOR=Visual Studio 15 2017 Win64"
    set vs=2017
    set dir=MSVC
    set "flags=/MD /O2 /Ob3 /Gw /GL /DNDEBUG"
    shift
) else if /I "%1"=="2015" (
    set "text=Setting environment for Visual Studio 2015"
    set "GENERATOR=Visual Studio 14 2015 Win64"
    set vs=2015
    set dir=MSVC
    set "flags=/MD /O2 /Ob3 /Gw /GL /DNDEBUG"
    shift
) else if /I "%1"=="ClangVS" (
    set "text=Setting environment for Clang with Visual Studio"
    set dir=ClangVS
    set "tool="-T LLVM_V143""
    set "flags=/MD /MT /O2 /Ot /Gw /GA /DNDEBUG"
    shift
) else if /I "%1"=="Clang" (
    set "text=Setting environment for Clang with Ninja"
    set dir=Clang
    set "GENERATOR=Ninja"
    set "CC=clang"
    set "CXX=clang"
    set "flags=/MD /MT /O2 /Ot /Gw /GA -Wno-unused-command-line-argument"
    shift
) else if /I "%1"=="ninja" (
    set "text=Setting environment for Ninja"
    set "GENERATOR=Ninja"
    set dir=GNU
    shift
) else if /I "%1"=="msys" (
    set "text=Setting environment for MSYS"
    set "GENERATOR=MSYS Makefiles"
    set dir=GNU
    set "flags=-lws2_32 -luserenv -lntdll -s -O3 -DNDEBUG"
    shift
) else if /I "%1"=="mingw" (
    set "text=Setting environment for MinGW"
    set "GENERATOR=MinGW Makefiles"
    set dir=GNU
    shift
) else if /I "%1"=="unix" (
    set "text=Setting environment for Unix"
    set "GENERATOR=Unix Makefiles"
    set dir=UNIX
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
) else if /I "%1"=="no-avx512" (
    set "cmake_eflags=%cmake_eflags% -DENABLE_AVX512=OFF"
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
)  else (
    echo Unknown argument "%1"
    call :help
    goto :EOF
)
goto :args

:help
    echo Batch file to build SVT-AV1 on Windows
    echo Usage: build.bat [2022^|2019^|2017^|2015^|clean] [release^|debug] [nobuild] [test] [shared^|static] [c-only] [no-avx512] [dovi] [hdr] [no-apps] [no-enc]
    exit /b 1
goto :EOF
