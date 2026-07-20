# Find FFMS2 library

get_filename_component(_SVT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(MSVC)
    set(_FFMS2_COMP_DIR "msvc")
else()
    set(_FFMS2_COMP_DIR "gnu")
endif()

set(_FFMS2_ROOT_HINTS
    "${_SVT_ROOT}"
    "${_SVT_ROOT}/Source"
    "${_SVT_ROOT}/third_party"
)

foreach(_ROOT ${_FFMS2_ROOT_HINTS})
    list(APPEND _FFMS2_HINTS
        "${_ROOT}/ffms2/${_FFMS2_COMP_DIR}"
        "${_ROOT}/ffms2/${_FFMS2_COMP_DIR}/lib"
        "${_ROOT}/ffms2"
        "${_ROOT}/ffms2/lib")
endforeach()

# Try classic CMake find first
find_path(FFMS2_INCLUDE_DIR
    NAMES ffms.h ffms2/ffms.h
    HINTS ${_FFMS2_HINTS}
    PATH_SUFFIXES include include/ffms2 ffms2
)

find_library(FFMS2_LIBRARY
    NAMES ffms2 libffms2
    HINTS ${_FFMS2_HINTS}
    PATH_SUFFIXES lib lib64
)

# pkg-config fallback
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND (NOT FFMS2_INCLUDE_DIR OR NOT FFMS2_LIBRARY))
    foreach(_pc_name ffms2 libffms2)
        pkg_check_modules(_FFMS2_PKG QUIET ${_pc_name})
        if(_FFMS2_PKG_FOUND)
            if(NOT FFMS2_INCLUDE_DIR AND DEFINED _FFMS2_PKG_INCLUDEDIR)
                set(FFMS2_INCLUDE_DIR "${_FFMS2_PKG_INCLUDEDIR}" CACHE PATH "FFMS2 include dir" FORCE)
            endif()

            if(NOT FFMS2_LIBRARY AND DEFINED _FFMS2_PKG_LIBRARIES)
                set(_first_abs "")
                foreach(_lib ${_FFMS2_PKG_LIBRARIES})
                    if(IS_ABSOLUTE "${_lib}")
                        set(_first_abs "${_lib}")
                        break()
                    endif()
                endforeach()

                if(_first_abs)
                    set(FFMS2_LIBRARY "${_first_abs}" CACHE FILEPATH "FFMS2 library" FORCE)
                else()
                    set(_FFMS2_PKG_LIBS_STR "${_FFMS2_PKG_LIBRARIES}")
                endif()
            endif()

            set(_FFMS2_PKG_LIBRARIES "${_FFMS2_PKG_LIBRARIES}" CACHE INTERNAL "FFMS2 pkg-config libs")
            set(_FFMS2_PKG_INCLUDEDIR "${_FFMS2_PKG_INCLUDEDIR}" CACHE INTERNAL "FFMS2 pkg-config includedir")
            set(_FFMS2_PKG_FOUND TRUE)
            break()
        endif()
    endforeach()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFMS2 REQUIRED_VARS FFMS2_LIBRARY FFMS2_INCLUDE_DIR)

if(FFMS2_FOUND AND NOT TARGET FFMS2::FFMS2)
    add_library(FFMS2::FFMS2 UNKNOWN IMPORTED)
    if(FFMS2_LIBRARY AND IS_ABSOLUTE "${FFMS2_LIBRARY}")
        set_target_properties(FFMS2::FFMS2 PROPERTIES
            IMPORTED_LOCATION "${FFMS2_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${FFMS2_INCLUDE_DIR}"
        )
    else()
        if(DEFINED _FFMS2_PKG_LIBRARIES OR DEFINED _FFMS2_PKG_LIBS_STR)
            set(_link_libs "${_FFMS2_PKG_LIBRARIES}")
            if(NOT _link_libs AND DEFINED _FFMS2_PKG_LIBS_STR)
                set(_link_libs "${_FFMS2_PKG_LIBS_STR}")
            endif()
            set_target_properties(FFMS2::FFMS2 PROPERTIES
                INTERFACE_LINK_LIBRARIES "${_link_libs}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFMS2_INCLUDE_DIR}"
            )
        else()
            set_target_properties(FFMS2::FFMS2 PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${FFMS2_INCLUDE_DIR}"
            )
        endif()
    endif()
endif()

set(FFMS2_LIBRARIES ${FFMS2_LIBRARY})
mark_as_advanced(FFMS2_INCLUDE_DIR FFMS2_LIBRARY)

if(FFMS2_FOUND)
    message(STATUS "FFMS2 found: ${FFMS2_LIBRARY}")
endif()