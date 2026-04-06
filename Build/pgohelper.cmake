#!/usr/bin/cmake -P
# cmake -P Build/pgohelper.cmake $PWD/Build $PWD/objective-2-fast::$PWD/objective-1-fast $PWD/Bin/Release/SvtAv1EncApp

if(CMAKE_ARGC LESS 6)
    message(
        FATAL_ERROR
            "Usage: cmake -P ${CMAKE_ARGV2} build_dir /path/to/videofiles::additional_paths /path/to/SvtAv1EncApp"
    )
endif()

set(BUILD_DIRECTORY "${CMAKE_ARGV3}")
set(VIDEO_DIRECTORY "${CMAKE_ARGV4}")
set(SvtAv1EncApp "${CMAKE_ARGV5}")

if(NOT EXISTS ${SvtAv1EncApp})
    message(
        FATAL_ERROR
            "Can't run pgo if the binaries don't exist. Looked at ${SvtAv1EncApp}"
    )
endif()

# Delete any existing Clang profiling data.
file(GLOB OLD_FILES
    "${BUILD_DIRECTORY}/*.profraw"
    "${BUILD_DIRECTORY}/*.profdata"
)
if(OLD_FILES)
    file(REMOVE ${OLD_FILES})
endif()

unset(videofiles)
string(REPLACE "::" ";" VIDEO_DIRECTORY "${VIDEO_DIRECTORY}")

foreach(dir IN LISTS VIDEO_DIRECTORY)
    file(GLOB videofile ${dir}/*.y4m)
    list(APPEND videofiles ${videofile})
endforeach()

set(PGO_SETTINGS_LINES
  # PQ-HDR high quality: slow preset, low CRF, strong PQ variance boost and luminance bias
  "--preset 3 --rc 0 --crf 20 --tune 0 --ac-bias 1.8 --variance-boost-curve 3 --variance-boost-strength 3 --luminance-qp-bias 22 --enable-dlf 2"

  # PQ-HDR main profile: balanced preset for typical HDR movie/series content
  "--preset 4 --rc 0 --crf 22 --tune 0 --ac-bias 1.5 --variance-boost-curve 3 --variance-boost-strength 2 --luminance-qp-bias 18 --enable-dlf 2"

  # SDR / neutral psy profile: medium preset without PQ curve, moderate psy strength
  "--preset 5 --rc 0 --crf 26 --tune 0 --ac-bias 1.3 --variance-boost-strength 2 --luminance-qp-bias 12 --enable-dlf 2"

  # SDR / default-like profile: medium preset, higher CRF, default-ish psy, trains non-SCD paths
  "--preset 5 --rc 0 --crf 30 --tune 0 --ac-bias 1.0 --variance-boost-strength 1 --luminance-qp-bias 8 --enable-dlf 2"

  # Faster profile: higher preset for performance-oriented workflows, slightly lower CRF to compensate
  "--preset 7 --rc 0 --crf 28 --tune 0 --ac-bias 1.0 --variance-boost-strength 2 --luminance-qp-bias 10 --enable-dlf 2"

  # Grain-preserving profile: focuses on film grain and texture retention
  "--preset 5 --rc 0 --crf 27 --tune 0 --ac-bias 1.6 --variance-boost-strength 3 --luminance-qp-bias 14 --enable-dlf 2 --film-grain 1"

  # Strong AQ / complex scenes profile: trains AQ and QM-heavy paths on mixed content
  "--preset 6 --rc 0 --crf 26 --tune 0 --ac-bias 1.3 --variance-boost-strength 2 --luminance-qp-bias 12 --enable-dlf 2 --aq-mode 2 --qm-min 4 --qm-max 20"
)

foreach(settings_line IN LISTS PGO_SETTINGS_LINES)
    string(STRIP "${settings_line}" settings_line)
    if(settings_line STREQUAL "")
        continue()
    endif()

    message(STATUS "Running settings: ${settings_line}")

    foreach(video IN LISTS videofiles)
        get_filename_component(videoname "${video}" NAME_WE)

        string(REPLACE " " "_" settings_name "${settings_line}")
        set(output_file "${BUILD_DIRECTORY}/${videoname}_${settings_name}.ivf")

        set(ENCODING_COMMAND
            ${SvtAv1EncApp}
            -i ${video}
            -b ${output_file}
        )
        separate_arguments(extra_args UNIX_COMMAND "${settings_line}")
        list(APPEND ENCODING_COMMAND ${extra_args})

        list(JOIN ENCODING_COMMAND " " ENCODING_COMMAND_STR)
        message(STATUS "Running ${ENCODING_COMMAND_STR}")

        execute_process(COMMAND ${ENCODING_COMMAND})
    endforeach()
endforeach()
