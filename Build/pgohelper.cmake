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
    "--preset 2 --rc 0 --crf 18 --tune 0 --variance-boost-curve 3 --variance-boost-strength 3 --luminance-qp-bias 20 --enable-dlf 2"
    "--preset 3 --rc 0 --crf 22 --tune 3 --variance-boost-curve 3 --variance-boost-strength 3 --luminance-qp-bias 25 --enable-dlf 2"
    "--preset 4 --rc 0 --crf 20 --tune 0 --variance-boost-curve 3 --variance-boost-strength 2 --luminance-qp-bias 15 --enable-dlf 2"
    "--preset 4 --rc 0 --crf 24 --tune 3 --variance-boost-curve 3 --variance-boost-strength 3 --luminance-qp-bias 30 --enable-dlf 2"
    "--preset 5 --rc 0 --crf 26 --tune 0 --variance-boost-curve 2 --variance-boost-strength 2 --luminance-qp-bias 15 --enable-dlf 2"
    "--preset 7 --rc 0 --crf 30 --tune 0 --variance-boost-curve 1 --variance-boost-strength 2 --luminance-qp-bias 0 --enable-dlf 2"
    "--preset 3 --rc 1 --tbr 20000 --tune 0 --variance-boost-curve 3 --variance-boost-strength 3 --luminance-qp-bias 20 --enable-dlf 2"
    "--preset 4 --rc 1 --tbr 12000 --tune 0 --variance-boost-curve 3 --variance-boost-strength 2 --luminance-qp-bias 15 --enable-dlf 2"
    "--preset 5 --rc 1 --tbr 8000 --tune 0 --variance-boost-curve 2 --variance-boost-strength 2 --luminance-qp-bias 10 --enable-dlf 2"
    "--preset 4 --rc 1 --tbr 16000 --tune 3 --variance-boost-curve 3 --variance-boost-strength 4 --luminance-qp-bias 30 --enable-dlf 2"
    "--preset 6 --rc 1 --tbr 10000 --tune 0 --variance-boost-curve 2 --variance-boost-strength 2 --luminance-qp-bias 10 --enable-dlf 2"
    "--preset 4 --rc 2 --tbr 20000 --tune 0 --variance-boost-curve 3 --variance-boost-strength 3 --luminance-qp-bias 20 --enable-dlf 2"
    "--preset 5 --rc 2 --tbr 12000 --tune 0 --variance-boost-curve 2 --variance-boost-strength 2 --luminance-qp-bias 10 --enable-dlf 2"
    "--preset 6 --rc 2 --tbr 8000 --tune 0 --variance-boost-curve 1 --variance-boost-strength 2 --luminance-qp-bias 0 --enable-dlf 2"
    "--preset 3 --rc 2 --tbr 16000 --tune 3 --variance-boost-curve 3 --variance-boost-strength 4 --luminance-qp-bias 35 --enable-dlf 2"
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
