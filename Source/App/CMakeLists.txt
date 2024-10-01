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
find_package(PkgConfig)
if(NOT PkgConfig_FOUND)
  message(WARNING "PkgConfig not found; disabled building with dovi & hdr10plus.")
endif()

# libdovi detection & preprocessor macro
option(LIBDOVI_FOUND "Use the dovi library" OFF)
if(LIBDOVI_FOUND)
  if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    find_path(PKG_CONFIG_FOUND NAMES dovi HINTS "${PROJECT_SOURCE_DIR}/Source/dovi/msvc/lib")
    find_library(LIBDOVI_LIBRARY NAMES dovi libdovi HINTS "${PROJECT_SOURCE_DIR}/Source/dovi/msvc/lib")
    find_path(LIBDOVI_INCLUDE_DIR NAMES libdovi/rpu_parser.h HINTS "${PROJECT_SOURCE_DIR}/Source/dovi/msvc/include")
  elseif (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    find_path(PKG_CONFIG_FOUND NAMES dovi HINTS "${PROJECT_SOURCE_DIR}/Source/dovi/gcc/lib")
    find_library(LIBDOVI_LIBRARY NAMES dovi libdovi HINTS "${PROJECT_SOURCE_DIR}/Source/dovi/gcc/lib")
    find_path(LIBDOVI_INCLUDE_DIR NAMES libdovi/rpu_parser.h HINTS "${PROJECT_SOURCE_DIR}/Source/dovi/gcc/include")
  endif()
  if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_LIBDOVI dovi QUIET)
  endif()
endif()

  if(NOT LIBDOVI_LIBRARY)
  message(FATAL_ERROR "dovi library not found!")
  endif()

  set(LIBDOVI_INCLUDE_DIRS ${LIBDOVI_INCLUDE_DIR})
  set(LIBDOVI_LIBRARIES ${LIBDOVI_LIBRARY})
  mark_as_advanced(LIBDOVI_INCLUDE_DIR LIBDOVI_LIBRARY)
  add_definitions(-DLIBDOVI_FOUND=1)
  execute_process(COMMAND ${CMAKE_COMMAND} -E echo_append "-- Building with dovi support - Yes\n")
else()
  execute_process(COMMAND ${CMAKE_COMMAND} -E echo_append "-- Building with dovi support - No\n")
endif()

# libhdr10plus detection & preprocessor macro
option(LIBHDR10PLUS_RS_FOUND "Use the hdr10plus library" OFF)
if(LIBHDR10PLUS_RS_FOUND)
  if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    find_path(PKG_CONFIG_FOUND NAMES hdr10plus-rs HINTS "${PROJECT_SOURCE_DIR}/Source/hdr/msvc/lib")
    find_library(LIBHDR10PLUS_RS_LIBRARY NAMES hdr10plus-rs libhdr10plus-rs HINTS "${PROJECT_SOURCE_DIR}/Source/hdr/msvc/lib")
    find_path(LIBHDR10PLUS_RS_INCLUDE_DIR NAMES libhdr10plus-rs/hdr10plus.h HINTS "${PROJECT_SOURCE_DIR}/Source/hdr/msvc/include")
  elseif (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    find_path(PKG_CONFIG_FOUND NAMES hdr10plus-rs HINTS "${PROJECT_SOURCE_DIR}/Source/hdr/gcc/lib")
    find_library(LIBHDR10PLUS_RS_LIBRARY NAMES hdr10plus-rs libhdr10plus-rs HINTS "${PROJECT_SOURCE_DIR}/Source/hdr/gcc/lib")
    find_path(LIBHDR10PLUS_RS_INCLUDE_DIR NAMES libhdr10plus-rs/hdr10plus.h HINTS "${PROJECT_SOURCE_DIR}/Source/hdr/gcc/include")
  endif()
  if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_LIBHDR10PLUS_RS hdr10plus-rs QUIET)
  endif()
endif()

  if(NOT LIBHDR10PLUS_RS_LIBRARY)
  message(FATAL_ERROR "hdr10plus library not found!")
  endif()

  set(LIBHDR10PLUS_RS_INCLUDE_DIRS ${LIBHDR10PLUS_RS_INCLUDE_DIR})
  set(LIBHDR10PLUS_RS_LIBRARIES ${LIBHDR10PLUS_RS_LIBRARY})
  mark_as_advanced(LIBHDR10PLUS_RS_INCLUDE_DIR LIBHDR10PLUS_RS_LIBRARY)
  add_definitions(-DLIBHDR10PLUS_RS_FOUND=1)
  execute_process(COMMAND ${CMAKE_COMMAND} -E echo_append "-- Building with hdr10plus support - Yes\n")
else()
  execute_process(COMMAND ${CMAKE_COMMAND} -E echo_append "-- Building with hdr10plus support - No\n")
endif()

set(all_files
    ../API/EbDebugMacros.h
    ../API/EbSvtAv1.h
    ../API/EbSvtAv1Enc.h
    ../API/EbSvtAv1ErrorCodes.h
    ../API/EbSvtAv1ExtFrameBuf.h
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

if(TARGET safestringlib)
    target_sources(SvtAv1EncApp PRIVATE $<TARGET_OBJECTS:safestringlib>)
endif()

#********** SET COMPILE FLAGS************
# Link the Encoder App
target_link_libraries(SvtAv1EncApp
        SvtAv1Enc
        ${LIBDOVI_LIBRARY}
        ${LIBHDR10PLUS_RS_LIBRARY})
if(${CMAKE_SYSTEM_NAME} STREQUAL "Android")
    target_link_libraries(SvtAv1EncApp ${PLATFORM_LIBS})
elseif(UNIX)
    target_link_libraries(SvtAv1EncApp
        pthread
        m)
endif()

install(TARGETS SvtAv1EncApp RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})
