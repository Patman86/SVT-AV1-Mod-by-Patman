#
# Copyright(c) 2019 Netflix, Inc.
#
# This source code is subject to the terms of the BSD 2 Clause License and the
# Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License was
# not distributed with this source code in the LICENSE file, you can obtain it
# at https://www.aomedia.org/license/software-license. If the Alliance for Open
# Media Patent License 1.0 was not distributed with this source code in the
# PATENTS file, you can obtain it at
# https://www.aomedia.org/license/patent-license.
#

# APP Directory CMakeLists.txt

# Include Subdirectories
include_directories(
  ${PROJECT_SOURCE_DIR}/test/
  ${PROJECT_SOURCE_DIR}/test/ref
  ${PROJECT_SOURCE_DIR}/third_party/googletest/include
  third_party/googletest/src
  ${PROJECT_SOURCE_DIR}/Source/API
  ${PROJECT_SOURCE_DIR}/Source/Lib/Common/Codec
  ${PROJECT_SOURCE_DIR}/Source/Lib/Common/C_DEFAULT/
  ${PROJECT_SOURCE_DIR}/Source/Lib/Common/ASM_SSE2/
  ${PROJECT_SOURCE_DIR}/Source/Lib/Common/ASM_SSSE3/
  ${PROJECT_SOURCE_DIR}/Source/Lib/Common/ASM_SSE4_1/
  ${PROJECT_SOURCE_DIR}/Source/Lib/Common/ASM_AVX2/
  ${PROJECT_SOURCE_DIR}/Source/Lib/Common/ASM_AVX512/
  ${PROJECT_SOURCE_DIR}/Source/Lib/Common/ASM_NEON/
  ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/C_DEFAULT/
  ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/ASM_SSE2/
  ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/ASM_SSSE3/
  ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/ASM_SSE4_1/
  ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/ASM_AVX2/
  ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/ASM_AVX512/
  ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/ASM_NEON/
  ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/Codec
  ${PROJECT_SOURCE_DIR}/Source/Lib/Encoder/Globals
  ${PROJECT_SOURCE_DIR}/Source/App/EncApp
  ${PROJECT_SOURCE_DIR}/Source/API
  ${PROJECT_SOURCE_DIR}/third_party/fastfeat/
  ${PROJECT_SOURCE_DIR}/third_party/aom_dsp/inc/)

# Define helper functions and macros used by Google Test.
include(../third_party/googletest/cmake/internal_utils.cmake)

if(MSVC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_ALLOW_KEYWORD_MACROS")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_ALLOW_KEYWORD_MACROS")
endif()

# When other libraries are using a shared version of runtime libraries, Google
# Test also has to use one.
option(
  gtest_force_shared_crt
  "Use shared (DLL) run-time lib even when Google Test is built as static lib."
  OFF)

add_definitions(-D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING=1)

enable_testing()

if(UNIX AND HAVE_X86_PLATFORM)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mavx2")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")
endif()

set(arch_neutral_files
    BitstreamWriterTest.cc
    EbUnitTest.h
    EbUnitTestUtility.c
    EbUnitTestUtility.h
    FilmGrainExpectedResult.h
    FilmGrainTest.cc
    GlobalMotionUtilTest.cc
    IntraBcUtilTest.cc
    ResizeTest.cc
    TestEnv.c
    TxfmCommon.h
    acm_random.h
    random.h
    util.h
    ref/TxfmRef.cc
    ref/TxfmRef.h
    ssim_test.cc
    ../third_party/aom_dsp/src/bitreader.c
    ../third_party/aom_dsp/src/entdec.c)

set(multi_arch_files
    FwdTxfm2dAsmTest.cc
    InvTxfm2dAsmTest.cc
    PictureOperatorTest.cc
    SadTest.cc
    selfguided_filter_test.cc
    CdefTest.cc
    SpatialFullDistortionTest.cc
    TemporalFilterTestPlanewise.cc
    convolve_2d_test.cc
    RestorationPickTest.cc)

if(HAVE_X86_PLATFORM)
  set(x86_arch_files
      AdaptiveScanTest.cc
      CompoundUtilTest.cc
      DeblockTest.cc
      EbNoiseModel_test.cc
      EbBlend_a64_mask_1d_test.cc
      EbBlend_a64_mask_test.cc
      EbHighbdIntraPredictionTests.cc
      EbHighbdIntraPredictionTests.h
      EncodeTxbAsmTest.cc
      FFTTest.cc
      FilterIntraPredTest.cc
      ForwardtransformTests.cc
      FwdTxfm1dTest.cc
      InvTxfm1dTest.cc
      FwdTxfm2dTest.cc
      HbdVarianceTest.cc
      MotionEstimationTest.cc
      OBMCSadTest.cc
      OBMCVarianceTest.cc
      PackUnPackTest.cc
      PaletteModeUtilTest.cc
      PsnrTest.cc
      QuantAsmTest.cc
      ResidualTest.cc
      SelfGuidedUtilTest.cc
      VarianceTest.cc
      WedgeUtilTest.cc
      av1_convolve_scale_test.cc
      compute_mean_test.cc
      corner_match_test.cc
      dwt_test.cc
      frame_error_test.cc
      hadamard_test.cc
      intrapred_cfl_test.cc
      intrapred_dr_test.cc
      intrapred_edge_filter_test.cc
      intrapred_test.cc
      quantize_func_test.cc
      subtract_avg_cfl_test.cc
      warp_filter_test.cc
      warp_filter_test_util.cc
      warp_filter_test_util.h
      wiener_convolve_test.cc)
endif()

if(HAVE_ARM_PLATFORM)
  set(arm_arch_files)
endif()

set(arch_neutral_lib_list
    $<TARGET_OBJECTS:COMMON_CODEC>
    $<TARGET_OBJECTS:FASTFEAT>
    $<TARGET_OBJECTS:COMMON_C_DEFAULT>
    $<TARGET_OBJECTS:ENCODER_C_DEFAULT>
    $<TARGET_OBJECTS:ENCODER_GLOBALS>
    $<TARGET_OBJECTS:ENCODER_CODEC>
    gtest_all)

if(HAVE_X86_PLATFORM)
  set(x86_arch_lib_list
      $<TARGET_OBJECTS:COMMON_ASM_SSE2>
      $<TARGET_OBJECTS:COMMON_ASM_SSSE3>
      $<TARGET_OBJECTS:COMMON_ASM_SSE4_1>
      $<TARGET_OBJECTS:COMMON_ASM_AVX2>
      $<TARGET_OBJECTS:COMMON_ASM_AVX512>
      $<TARGET_OBJECTS:ENCODER_ASM_SSE2>
      $<TARGET_OBJECTS:ENCODER_ASM_SSSE3>
      $<TARGET_OBJECTS:ENCODER_ASM_SSE4_1>
      $<TARGET_OBJECTS:ENCODER_ASM_AVX2>
      $<TARGET_OBJECTS:ENCODER_ASM_AVX512>
      $<IF:$<BOOL:${USE_EXTERNAL_CPUINFO}>,cpuinfo::cpuinfo,cpuinfo_public>)
endif()

if(HAVE_ARM_PLATFORM)
  set(arm_arch_lib_list $<TARGET_OBJECTS:COMMON_ASM_NEON>
                        $<TARGET_OBJECTS:ENCODER_ASM_NEON>)
endif()

set(all_files ${arch_neutral_files} ${multi_arch_files} ${x86_arch_files}
              ${arm_arch_files})

set(lib_list ${arch_neutral_lib_list} ${x86_arch_lib_list} ${arm_arch_lib_list})

if(UNIX)
  # App Source Files
  add_executable(SvtAv1UnitTests ${all_files})
  # Link the Encoder App
  target_link_libraries(SvtAv1UnitTests ${lib_list} pthread m)
else()
  cxx_executable_with_flags(SvtAv1UnitTests "${cxx_default}" "${lib_list}"
                            ${all_files})
endif()

add_dependencies(SvtAv1UnitTests SvtAv1Enc)

install(TARGETS SvtAv1UnitTests RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_test(SvtAv1UnitTests ${CMAKE_OUTPUT_DIRECTORY}/SvtAv1UnitTests)

add_subdirectory(api_test)
add_subdirectory(e2e_test)
