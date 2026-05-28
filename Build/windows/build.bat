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

if not defined MSYS_ROOT (
    for /f "delims=" %%I in ('where bash 2^>nul ^| findstr /I "msys64"') do (
        for %%J in ("%%~dpI\..\..") do set "MSYS_ROOT=%%~fJ"
        goto :MSYS_ROOT_found
    )
    if exist "H:\mabs\msys64\usr\bin\bash.exe" (
        set "MSYS_ROOT=H:\mabs\msys64"
    ) else if exist "C:\msys64\usr\bin\bash.exe" (
        set "MSYS_ROOT=C:\msys64"
    )
)
:MSYS_ROOT_found

if not defined LLVM_ROOT (
    for /f "delims=" %%I in ('where clang 2^>nul ^| findstr /I "\\LLVM\\bin\\clang.exe"') do (
        for %%J in ("%%~dpI\..") do set "LLVM_ROOT=%%~fJ"
        goto :LLVM_ROOT_found
    )
    if exist "C:\Program Files\LLVM\bin\clang.exe" (
        set "LLVM_ROOT=C:\Program Files\LLVM"
    ) else if exist "C:\Program Files (x86)\LLVM\bin\clang.exe" (
        set "LLVM_ROOT=C:\Program Files (x86)\LLVM"
    )
)
:LLVM_ROOT_found

set "CFLAGS="
set "CXXFLAGS="
set "CPPFLAGS="
set "LDFLAGS="
:: Set defaults to prevent inheriting
set "build=y"
:: Default is Release
set "buildtype=Release"
:: Default is static
set "shared=OFF"
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

cmake --fresh ../../.. %GENERATOR% %ARCH_OPTION% %tool% %cmake_eflags% -DCMAKE_BUILD_TYPE=%buildtype% -DCMAKE_INSTALL_PREFIX=%SYSTEMDRIVE%\svt-encoders -DBUILD_SHARED_LIBS=%shared% -DBUILD_TESTING=%unittest% -DCMAKE_CXX_FLAGS_RELEASE="%flags%" -DCMAKE_C_FLAGS_RELEASE="%flags%"|| exit /b 1

if "%build%"=="y" cmake --build . --clean-first --parallel --config %buildtype% %pgo%

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
) else if /I "%1"=="2026" (
    set "text=Setting environment for Visual Studio 2026"
    set "GENERATOR=Visual Studio 18 2026"
    set vs=2026
    set dir=MSVC
    set "flags=/MD /O2 /Ob3 /Gw /GL /DNDEBUG /W0"
    shift
) else if /I "%1"=="2022" (
    set "text=Setting environment for Visual Studio 2022"
    set "GENERATOR=Visual Studio 17 2022"
    set vs=2022
    set dir=MSVC
    set "flags=/MD /O2 /Ob3 /Gw /GL /DNDEBUG /W0"
    shift
) else if /I "%1"=="2019" (
    set "text=Setting environment for Visual Studio 2019"
    set "GENERATOR=Visual Studio 16 2019"
    set vs=2019
    set dir=MSVC
    set "flags=/MD /O2 /Ob3 /Gw /GL /DNDEBUG /W0"
    shift
) else if /I "%1"=="2017" (
    set "text=Setting environment for Visual Studio 2017"
    set "GENERATOR=Visual Studio 15 2017 Win64"
    set vs=2017
    set dir=MSVC
    set "flags=/MD /O2 /Ob3 /Gw /GL /DNDEBUG /W0"
    shift
) else if /I "%1"=="2015" (
    set "text=Setting environment for Visual Studio 2015"
    set "GENERATOR=Visual Studio 14 2015 Win64"
    set vs=2015
    set dir=MSVC
    set "flags=/MD /O2 /Ob3 /Gw /GL /DNDEBUG /W0"
    shift
) else if /I "%1"=="ClangVS" (
    set "text=Setting environment for Clang with Visual Studio"
    set dir=ClangVS
    set "tool="-T LLVM_V143""
    set "PATH=%LLVM_ROOT%\bin;%PATH%"
    set "flags=/MD /MT /O2 /Ot /Gw /GA /DNDEBUG /W0"
    shift
) else if /I "%1"=="Clang" (
    set "text=Setting environment for Clang with Ninja"
    set dir=Clang
    set "GENERATOR=Ninja"
    if defined MSYSTEM (
        if /I "%MSYSTEM%"=="CLANG64" (
            echo Detected MSYS CLANG64 environment
            set "PATH=%MSYS_ROOT%\clang64\bin;%PATH%"
            set "CC=%MSYS_ROOT%\clang64\bin\clang.exe"
            set "CXX=%MSYS_ROOT%\clang64\bin\clang++.exe"
            set "flags=-s -O3 -DNDEBUG -w -march=x86-64-v4 -ffast-math"
        ) else (
            echo Detected MSYS/MINGW environment
            set "PATH=%MSYS_ROOT%\mingw64\bin;%PATH%"
            set "CC=%MSYS_ROOT%\mingw64\bin\clang.exe"
            set "CXX=%MSYS_ROOT%\mingw64\bin\clang++.exe"
            set "flags=-s -O3 -DNDEBUG -w -march=x86-64-v4 -ffast-math"
        )
    ) else (
        echo Detected pure Windows environment, using LLVM
        set "PATH=%LLVM_ROOT%\bin;%PATH%"
        set "CC=clang-cl"
        set "CXX=clang-cl"
        set "flags=/MD /MT /O2 /Ot /Gw /GA /DNDEBUG /W0"
    )
    shift
) else if /I "%1"=="msys" (
    set "text=Setting environment for MSYS"
    set "GENERATOR=MSYS Makefiles"
    set dir=GNU
    set "PATH=%MSYS_ROOT%\ucrt64\bin;%PATH%"
    set "CC=%MSYS_ROOT%\ucrt64\bin\gcc.exe"
    set "CXX=%MSYS_ROOT%\ucrt64\bin\g++.exe"
    set "flags=-s -O3 -DNDEBUG -w"
    shift
) else if /I "%1"=="ninja" (
    set "text=Setting environment for Ninja"
    set "GENERATOR=Ninja"
    set dir=GNU
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
) else if /I "%1"=="pgo" (
    set "cmake_eflags=%cmake_eflags% -DSVT_AV1_PGO=ON"
    set "pgo=--target RunPGO"
    shift
) else if /I "%1"=="pgogen" (
    set "cmake_eflags=%cmake_eflags% -DSVT_AV1_PGO=ON"
    set "pgogen=--target PGOCompileGen"
    shift
) else if /I "%1"=="pgouse" (
    set "cmake_eflags=%cmake_eflags% -DSVT_AV1_PGO=ON"
    set "pgouse=--target PGOCompileUse"
    shift
) else if /I "%1"=="pgopath" (
    if "%~2"=="" (
        echo Fehler: pgopath erwartet einen Pfad
        exit /b 1
    )
    set "cmake_eflags=%cmake_eflags% -DSVT_AV1_PGO_CUSTOM_VIDEOS=%~2"
    shift
    shift
) else if /I "%1"=="ext-lib-static" (
    set "cmake_eflags=%cmake_eflags% -DEXT_LIB_STATIC=ON"
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
    echo Usage: build.bat [2022^|2019^|2017^|2015^|clean] [release^|debug] [nobuild] [test] [shared^|static] [c-only] [no-avx512] [dovi] [hdr] [ext-lib-static] [no-apps] [no-enc] [pgo] [pgopath] [path_for_pgopath]
    exit /b 1
goto :EOF
