# Find HDR10+ library

get_filename_component(_SVT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if (MSVC)
    set(_HDR10PLUS_PLATFORM_DIR "msvc")
else()
    set(_HDR10PLUS_PLATFORM_DIR "gnu")
endif()

set(_HDR10PLUS_HINT_PATHS
    "${_SVT_ROOT}/source/HDR/${_HDR10PLUS_PLATFORM_DIR}"
    "${_SVT_ROOT}/source/HDR/${_HDR10PLUS_PLATFORM_DIR}/lib"
    "${_SVT_ROOT}/source/HDR/lib"
    "${_SVT_ROOT}/source/HDR")

find_path(LIBHDR10PLUS_RS_INCLUDE_DIR NAMES libhdr10plus-rs/hdr10plus.h HINTS ${_HDR10PLUS_HINT_PATHS} PATH_SUFFIXES include)
find_library(LIBHDR10PLUS_RS_LIBRARY NAMES hdr10plus-rs libhdr10plus-rs hdr10plus_static HINTS ${_HDR10PLUS_HINT_PATHS} PATH_SUFFIXES lib lib64)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(hdr10plus REQUIRED_VARS LIBHDR10PLUS_RS_LIBRARY LIBHDR10PLUS_RS_INCLUDE_DIR)

if (hdr10plus_FOUND AND NOT TARGET hdr10plus::hdr10plus)
    add_library(hdr10plus::hdr10plus UNKNOWN IMPORTED)
    set_target_properties(hdr10plus::hdr10plus PROPERTIES
        IMPORTED_LOCATION "${LIBHDR10PLUS_RS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBHDR10PLUS_RS_INCLUDE_DIR}")
endif()

set(LIBHDR10PLUS_RS_FOUND ${hdr10plus_FOUND})
set(LIBHDR10PLUS_RS_LIBRARIES ${LIBHDR10PLUS_RS_LIBRARY})
mark_as_advanced(LIBHDR10PLUS_RS_INCLUDE_DIR LIBHDR10PLUS_RS_LIBRARY)
