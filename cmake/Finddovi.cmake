# Find Dolby Vision (dovi) library

get_filename_component(_SVT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(MSVC)
    set(_DOVI_COMP_DIR "msvc")
else()
    set(_DOVI_COMP_DIR "gnu")
endif()

set(_DOVI_ROOT_HINTS
    "${_SVT_ROOT}"
    "${_SVT_ROOT}/Source"
    "${_SVT_ROOT}/third_party"
)

foreach(_ROOT ${_DOVI_ROOT_HINTS})
    list(APPEND _DOVI_HINTS
        "${_ROOT}/dovi/${_DOVI_COMP_DIR}"
        "${_ROOT}/dovi/${_DOVI_COMP_DIR}/lib"
        "${_ROOT}/dovi"
        "${_ROOT}/dovi/lib")
endforeach()

find_path(LIBDOVI_INCLUDE_DIR
    NAMES libdovi/rpu_parser.h
    HINTS ${_DOVI_HINTS}
    PATH_SUFFIXES include libdovi
)

find_library(LIBDOVI_LIBRARY
    NAMES dovi libdovi
    HINTS ${_DOVI_HINTS}
    PATH_SUFFIXES lib lib64
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(dovi REQUIRED_VARS LIBDOVI_LIBRARY LIBDOVI_INCLUDE_DIR)

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
