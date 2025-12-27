# Find Dolby Vision (dovi) library

get_filename_component(_SVT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(MSVC)
    set(_DOVI_COMP_DIR "msvc")
else()
    set(_DOVI_COMP_DIR "gnu")
endif()

set(_DOVI_PROJECT_HINTS
    "${_SVT_ROOT}/dovi/${_DOVI_COMP_DIR}/lib"
    "${_SVT_ROOT}/dovi/${_DOVI_COMP_DIR}"
    "${_SVT_ROOT}/dovi/lib"
    "${_SVT_ROOT}/dovi"

    "${_SVT_ROOT}/Source/dovi/${_DOVI_COMP_DIR}/lib"
    "${_SVT_ROOT}/Source/dovi/${_DOVI_COMP_DIR}"
    "${_SVT_ROOT}/Source/dovi/lib"
    "${_SVT_ROOT}/Source/dovi"

    "${_SVT_ROOT}/third_party/dovi/${_DOVI_COMP_DIR}/lib"
    "${_SVT_ROOT}/third_party/dovi/${_DOVI_COMP_DIR}"

    "${_SVT_ROOT}/lib/dovi"
    "${_SVT_ROOT}/libs/dovi"
)

set(_DOVI_INCLUDE_HINTS
    "${_SVT_ROOT}/dovi/${_DOVI_COMP_DIR}/include"
    "${_SVT_ROOT}/dovi/include"
    "${_SVT_ROOT}/Source/dovi/${_DOVI_COMP_DIR}/include"
    "${_SVT_ROOT}/Source/dovi/include"
)

find_path(LIBDOVI_INCLUDE_DIR
    NAMES libdovi/rpu_parser.h
    HINTS ${_DOVI_INCLUDE_HINTS}
    PATH_SUFFIXES include libdovi
)

find_library(LIBDOVI_LIBRARY
    NAMES dovi libdovi dovi_static
    HINTS ${_DOVI_PROJECT_HINTS}
    PATH_SUFFIXES lib lib64
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(dovi
    REQUIRED_VARS LIBDOVI_LIBRARY LIBDOVI_INCLUDE_DIR
)

if(dovi_FOUND AND NOT TARGET dovi::dovi)
    add_library(dovi::dovi UNKNOWN IMPORTED)
    set_target_properties(dovi::dovi PROPERTIES
        IMPORTED_LOCATION "${LIBDOVI_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBDOVI_INCLUDE_DIR}"
    )
endif()

set(LIBDOVI_FOUND ${dovi_FOUND})
set(LIBDOVI_LIBRARIES ${LIBDOVI_LIBRARY})
mark_as_advanced(LIBDOVI_INCLUDE_DIR LIBDOVI_LIBRARY)

if(LIBDOVI_FOUND)
    message(STATUS "libdovi found: ${LIBDOVI_LIBRARY}")
endif()
