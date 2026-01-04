# Find Dolby Vision library

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

# Try classical CMake search first
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

# pkg-config fallback
find_package(PkgConfig QUIET)
if (PkgConfig_FOUND AND (NOT LIBDOVI_INCLUDE_DIR OR NOT LIBDOVI_LIBRARY))
    foreach(_pc_name libdovi dovi)
        pkg_check_modules(_DOVI_PKG QUIET ${_pc_name})
        if (_DOVI_PKG_FOUND)
            if (NOT LIBDOVI_INCLUDE_DIR AND DEFINED _DOVI_PKG_INCLUDEDIR)
                set(LIBDOVI_INCLUDE_DIR "${_DOVI_PKG_INCLUDEDIR}" CACHE PATH "libdovi include dir" FORCE)
            endif()

            if (NOT LIBDOVI_LIBRARY AND DEFINED _DOVI_PKG_LIBRARIES)
                # If pkg-config provides absolute library paths, prefer the first absolute path
                set(_first_abs "")
                foreach(_lib ${_DOVI_PKG_LIBRARIES})
                    if (IS_ABSOLUTE "${_lib}")
                        set(_first_abs "${_lib}")
                        break()
                    endif()
                endforeach()
                if (_first_abs)
                    set(LIBDOVI_LIBRARY "${_first_abs}" CACHE FILEPATH "libdovi library" FORCE)
                else()
                    # store raw linker flags (may be -ldovi); used later for INTERFACE_LINK_LIBRARIES
                    set(_DOVI_PKG_LIBS_STR "${_DOVI_PKG_LIBRARIES}")
                endif()
            endif()

            # keep pkg info for target creation
            set(_DOVI_PKG_LIBRARIES "${_DOVI_PKG_LIBRARIES}" CACHE INTERNAL "libdovi pkg-config libs")
            set(_DOVI_PKG_INCLUDEDIR "${_DOVI_PKG_INCLUDEDIR}" CACHE INTERNAL "libdovi pkg-config includedir")
            set(_DOVI_PKG_FOUND TRUE)
            break()
        endif()
    endforeach()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(dovi REQUIRED_VARS LIBDOVI_LIBRARY LIBDOVI_INCLUDE_DIR)

if(dovi_FOUND AND NOT TARGET dovi::dovi)
    add_library(dovi::dovi UNKNOWN IMPORTED)
    if (LIBDOVI_LIBRARY AND IS_ABSOLUTE "${LIBDOVI_LIBRARY}")
        set_target_properties(dovi::dovi PROPERTIES
            IMPORTED_LOCATION "${LIBDOVI_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBDOVI_INCLUDE_DIR}"
        )
    else()
        # fallback: use pkg-config-provided linker flags if available
        if (DEFINED _DOVI_PKG_LIBRARIES OR DEFINED _DOVI_PKG_LIBS_STR)
            set(_link_libs "${_DOVI_PKG_LIBRARIES}")
            if (NOT _link_libs AND DEFINED _DOVI_PKG_LIBS_STR)
                set(_link_libs "${_DOVI_PKG_LIBS_STR}")
            endif()
            set_target_properties(dovi::dovi PROPERTIES
                INTERFACE_LINK_LIBRARIES "${_link_libs}"
                INTERFACE_INCLUDE_DIRECTORIES "${LIBDOVI_INCLUDE_DIR}"
            )
        else()
            # As a last resort set include dir only
            set_target_properties(dovi::dovi PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${LIBDOVI_INCLUDE_DIR}"
            )
        endif()
    endif()
endif()

set(LIBDOVI_FOUND ${dovi_FOUND})
set(LIBDOVI_LIBRARIES ${LIBDOVI_LIBRARY})
mark_as_advanced(LIBDOVI_INCLUDE_DIR LIBDOVI_LIBRARY)

if(LIBDOVI_FOUND)
    message(STATUS "libdovi found: ${LIBDOVI_LIBRARY}")
endif()
