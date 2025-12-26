# Find Dolby Vision (dovi) library

get_filename_component(_SVT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if (MSVC)
    set(_DOVI_PLATFORM_DIR "msvc")
else()
    set(_DOVI_PLATFORM_DIR "gnu")
endif()

set(_DOVI_HINT_PATHS
    "${_SVT_ROOT}/source/dovi/${_DOVI_PLATFORM_DIR}"
    "${_SVT_ROOT}/source/dovi/${_DOVI_PLATFORM_DIR}/lib"
    "${_SVT_ROOT}/source/dovi/lib"
    "${_SVT_ROOT}/source/dovi")

find_path(LIBDOVI_INCLUDE_DIR NAMES libdovi/rpu_parser.h HINTS ${_DOVI_HINT_PATHS} PATH_SUFFIXES include)
find_library(LIBDOVI_LIBRARY NAMES dovi libdovi dovi_static HINTS ${_DOVI_HINT_PATHS} PATH_SUFFIXES lib lib64)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(dovi REQUIRED_VARS LIBDOVI_LIBRARY LIBDOVI_INCLUDE_DIR)

if (dovi_FOUND AND NOT TARGET dovi::dovi)
    add_library(dovi::dovi UNKNOWN IMPORTED)
    set_target_properties(dovi::dovi PROPERTIES
        IMPORTED_LOCATION "${LIBDOVI_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBDOVI_INCLUDE_DIR}")
endif()

set(LIBDOVI_FOUND ${dovi_FOUND})
set(LIBDOVI_LIBRARIES ${LIBDOVI_LIBRARY})
mark_as_advanced(LIBDOVI_INCLUDE_DIR LIBDOVI_LIBRARY)
