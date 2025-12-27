# Find HDR10+ library

get_filename_component(_SVT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(MSVC)
    set(_HDR_COMP_DIR "msvc")
else()
    set(_HDR_COMP_DIR "gnu")
endif()

set(_HDR_ROOT_HINTS
    "${_SVT_ROOT}"
    "${_SVT_ROOT}/Source"
    "${_SVT_ROOT}/third_party"
)

foreach(_ROOT ${_HDR_ROOT_HINTS})
    list(APPEND _HDR_HINTS
        "${_ROOT}/hdr/${_HDR_COMP_DIR}"
        "${_ROOT}/hdr/${_HDR_COMP_DIR}/lib"
        "${_ROOT}/hdr"
        "${_ROOT}/hdr/lib")
endforeach()

find_path(LIBHDR10PLUS_RS_INCLUDE_DIR
    NAMES libhdr10plus-rs/hdr10plus.h
    HINTS ${_HDR_HINTS}
    PATH_SUFFIXES include
)

find_library(LIBHDR10PLUS_RS_LIBRARY
    NAMES hdr10plus-rs libhdr10plus-rs
    HINTS ${_HDR_HINTS}
    PATH_SUFFIXES lib lib64
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(hdr10plus REQUIRED_VARS LIBHDR10PLUS_RS_LIBRARY LIBHDR10PLUS_RS_INCLUDE_DIR)

if(hdr10plus_FOUND AND NOT TARGET hdr10plus::hdr10plus)
    add_library(hdr10plus::hdr10plus UNKNOWN IMPORTED)
    set_target_properties(hdr10plus::hdr10plus PROPERTIES
        IMPORTED_LOCATION "${LIBHDR10PLUS_RS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBHDR10PLUS_RS_INCLUDE_DIR}"
    )
endif()

set(LIBHDR10PLUS_RS_FOUND ${hdr10plus_FOUND})
set(LIBHDR10PLUS_RS_LIBRARIES ${LIBHDR10PLUS_RS_LIBRARY})
mark_as_advanced(LIBHDR10PLUS_RS_INCLUDE_DIR LIBHDR10PLUS_RS_LIBRARY)

if(LIBHDR10PLUS_RS_FOUND)
    message(STATUS "hdr10plus-rs found: ${LIBHDR10PLUS_RS_LIBRARY}")
endif()
