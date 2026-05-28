#
# Copyright(c) 2019 Intel Corporation
#
# This source code is subject to the terms of the BSD 2 Clause License and
# the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
# was not distributed with this source code in the LICENSE file, you can
# obtain it at https://www.aomedia.org/license/software-license. If the Alliance for Open
# Media Patent License 1.0 was not distributed with this source code in the
# PATENTS file, you can obtain it at https://www.aomedia.org/license/patent-license.
#

cmake_minimum_required(VERSION 3.5...3.28)

# APP Directory CMakeLists.txt

# Include Subdirectories
include_directories(${PROJECT_SOURCE_DIR}/Source/API/)
# ---------------------------------------------------------------
# Optional static link for external libs
# ---------------------------------------------------------------
option(EXT_LIB_STATIC "Force linking with static libraries for external dependencies" OFF)

set(SUFFIX "")
if(EXT_LIB_STATIC)
  set(SUFFIX ${CMAKE_STATIC_LIBRARY_SUFFIX})
else()
  set(SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX})
endif()

# ===============================================================
# Dolby Vision (libdovi)
# ===============================================================
option(LIBDOVI_FOUND "Use the Dolby Vision (dovi) library" OFF)
if(LIBDOVI_FOUND)
    find_package(dovi QUIET)
    if(LIBDOVI_FOUND)
        include_directories(${LIBDOVI_INCLUDE_DIR})
        set(LIBDOVI_LIBRARIES ${LIBDOVI_LIBRARY})
        add_definitions(-DLIBDOVI_FOUND=1)
        message(STATUS "Building with Dolby Vision support – Yes")
    else()
        message(STATUS "Building with Dolby Vision support – No (library not found)")
    endif()
endif()

# ===============================================================
# HDR10+ (hdr10plus-rs)
# ===============================================================
option(LIBHDR10PLUS_RS_FOUND "Use the HDR10+ library" OFF)
if(LIBHDR10PLUS_RS_FOUND)
    find_package(hdr10plus QUIET)
    if(LIBHDR10PLUS_RS_FOUND)
        include_directories(${LIBHDR10PLUS_RS_INCLUDE_DIR})
        set(LIBHDR10PLUS_RS_LIBRARIES ${LIBHDR10PLUS_RS_LIBRARY})
        add_definitions(-DLIBHDR10PLUS_RS_FOUND=1)
        message(STATUS "Building with HDR10+ support – Yes")
    else()
        message(STATUS "Building with HDR10+ support – No (library not found)")
    endif()
endif()

set(all_files
    ../API/EbConfigMacros.h
    ../API/EbDebugMacros.h
    ../API/EbSvtAv1.h
    ../API/EbSvtAv1Enc.h
    ../API/EbSvtAv1ErrorCodes.h
    ../API/EbSvtAv1Formats.h
    ../API/EbSvtAv1Metadata.h
    app_config.c
    app_config.h
    app_context.c
    app_context.h
    app_input_y4m.c
    app_input_y4m.h
    app_main.c
    app_output_ivf.c
    app_output_ivf.h
    app_process_cmd.c
    svt_time.c
    svt_time.h
    )

# App Source Files
add_executable(SvtAv1EncApp
    ${all_files})

if(WIN32)
    target_sources(SvtAv1EncApp PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/svt.rc
    )
endif()
# Expose pthread_setname_np on glibc for the app's main-thread naming.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(SvtAv1EncApp PRIVATE _GNU_SOURCE)
endif()

if(TARGET safestringlib)
    target_sources(SvtAv1EncApp PRIVATE $<TARGET_OBJECTS:safestringlib>)
endif()

# ===============================================================
# Link libraries (Encoder + HDR + DOVI)
# ===============================================================

# Allow duplicate Rust runtime symbols from multiple static libs
if(MSVC)
    target_link_options(SvtAv1EncApp PRIVATE /FORCE:MULTIPLE)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_link_options(SvtAv1EncApp PRIVATE -Wl,--allow-multiple-definition)
endif()

# Link everything through interface libs (safe against symbol overlap)
target_link_libraries(SvtAv1EncApp
    PRIVATE
        SvtAv1Enc
        $<$<BOOL:${LIBDOVI_FOUND}>:dovi::dovi>
        $<$<BOOL:${LIBHDR10PLUS_RS_FOUND}>:hdr10plus::hdr10plus>
        $<$<OR:$<BOOL:${LIBDOVI_FOUND}>,$<BOOL:${LIBHDR10PLUS_RS_FOUND}>>:ws2_32>
        $<$<OR:$<BOOL:${LIBDOVI_FOUND}>,$<BOOL:${LIBHDR10PLUS_RS_FOUND}>>:userenv>
        $<$<OR:$<BOOL:${LIBDOVI_FOUND}>,$<BOOL:${LIBHDR10PLUS_RS_FOUND}>>:ntdll>
)

install(TARGETS SvtAv1EncApp RUNTIME COMPONENT Runtime DESTINATION ${CMAKE_INSTALL_BINDIR})
