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

# Try classic CMake find first
find_path(LIBHDR10PLUS_RS_INCLUDE_DIR
    NAMES libhdr10plus-rs/hdr10plus.h
    HINTS ${_HDR_HINTS}
    PATH_SUFFIXES include
)

find_library(LIBHDR10PLUS_RS_LIBRARY
    NAMES hdr10plus-rs libhdr10plus-rs hdr10plus libhdr10plus
    HINTS ${_HDR_HINTS}
    PATH_SUFFIXES lib lib64
)

# pkg-config fallback
find_package(PkgConfig QUIET)
if (PkgConfig_FOUND AND (NOT LIBHDR10PLUS_RS_INCLUDE_DIR OR NOT LIBHDR10PLUS_RS_LIBRARY))
    foreach(_pc_name libhdr10plus-rs libhdr10plus hdr10plus hdr10plus-rs)
        pkg_check_modules(_HDR10PLUS_PKG QUIET ${_pc_name})
        if (_HDR10PLUS_PKG_FOUND)
            if (NOT LIBHDR10PLUS_RS_INCLUDE_DIR AND DEFINED _HDR10PLUS_PKG_INCLUDEDIR)
                set(LIBHDR10PLUS_RS_INCLUDE_DIR "${_HDR10PLUS_PKG_INCLUDEDIR}" CACHE PATH "hdr10plus include dir" FORCE)
            endif()

            if (NOT LIBHDR10PLUS_RS_LIBRARY AND DEFINED _HDR10PLUS_PKG_LIBRARIES)
                set(_first_abs "")
                foreach(_lib ${_HDR10PLUS_PKG_LIBRARIES})
                    if (IS_ABSOLUTE "${_lib}")
                        set(_first_abs "${_lib}")
                        break()
                    endif()
                endforeach()
                if (_first_abs)
                    set(LIBHDR10PLUS_RS_LIBRARY "${_first_abs}" CACHE FILEPATH "hdr10plus library" FORCE)
                else()
                    set(_HDR10PLUS_PKG_LIBS_STR "${_HDR10PLUS_PKG_LIBRARIES}")
                endif()
            endif()

            set(_HDR10PLUS_PKG_LIBRARIES "${_HDR10PLUS_PKG_LIBRARIES}" CACHE INTERNAL "hdr10plus pkg-config libs")
            set(_HDR10PLUS_PKG_INCLUDEDIR "${_HDR10PLUS_PKG_INCLUDEDIR}" CACHE INTERNAL "hdr10plus pkg-config includedir")
            set(_HDR10PLUS_PKG_FOUND TRUE)
            break()
        endif()
    endforeach()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(hdr10plus REQUIRED_VARS LIBHDR10PLUS_RS_LIBRARY LIBHDR10PLUS_RS_INCLUDE_DIR)

if(hdr10plus_FOUND AND NOT TARGET hdr10plus::hdr10plus)
    add_library(hdr10plus::hdr10plus UNKNOWN IMPORTED)
    if (LIBHDR10PLUS_RS_LIBRARY AND IS_ABSOLUTE "${LIBHDR10PLUS_RS_LIBRARY}")
        set_target_properties(hdr10plus::hdr10plus PROPERTIES
            IMPORTED_LOCATION "${LIBHDR10PLUS_RS_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBHDR10PLUS_RS_INCLUDE_DIR}"
        )
    else()
        if (DEFINED _HDR10PLUS_PKG_LIBRARIES OR DEFINED _HDR10PLUS_PKG_LIBS_STR)
            set(_link_libs "${_HDR10PLUS_PKG_LIBRARIES}")
            if (NOT _link_libs AND DEFINED _HDR10PLUS_PKG_LIBS_STR)
                set(_link_libs "${_HDR10PLUS_PKG_LIBS_STR}")
            endif()
            set_target_properties(hdr10plus::hdr10plus PROPERTIES
                INTERFACE_LINK_LIBRARIES "${_link_libs}"
                INTERFACE_INCLUDE_DIRECTORIES "${LIBHDR10PLUS_RS_INCLUDE_DIR}"
            )
        else()
            set_target_properties(hdr10plus::hdr10plus PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${LIBHDR10PLUS_RS_INCLUDE_DIR}"
            )
        endif()
    endif()
endif()

set(LIBHDR10PLUS_RS_FOUND ${hdr10plus_FOUND})
set(LIBHDR10PLUS_RS_LIBRARIES ${LIBHDR10PLUS_RS_LIBRARY})
mark_as_advanced(LIBHDR10PLUS_RS_INCLUDE_DIR LIBHDR10PLUS_RS_LIBRARY)

if(LIBHDR10PLUS_RS_FOUND)
    message(STATUS "hdr10plus-rs found: ${LIBHDR10PLUS_RS_LIBRARY}")
endif()
