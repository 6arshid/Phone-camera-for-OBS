# FindLibObs.cmake
#
# Locates the libobs headers and an import library for the installed OBS
# Studio build so the plugin can be compiled and linked without building
# OBS Studio from source.
#
# Inputs (cache/env):
#   OBS_ROOT         - OBS installation root (default: C:\Program Files\obs-studio)
#   OBS_HEADERS_DIR  - directory containing libobs/obs.h
#                      (default: <repo>/deps/obs-headers/obs-studio)
#   OBS_IMPORT_LIB   - path to obs.lib / libobs import library
#                      (default: <repo>/deps/obs-import/obs.lib on Windows)
#
# Outputs:
#   LibObs_FOUND, LibObs_INCLUDE_DIRS, LibObs_LIBRARY, obsconfig.h generated
#   in the build tree and added to the include path.

set(OBS_ROOT "" CACHE PATH "OBS Studio installation root")
set(OBS_HEADERS_DIR "" CACHE PATH "Path to obs-studio sources (headers)")
set(OBS_IMPORT_LIB "" CACHE PATH "Path to obs import library")

set(_ocp_repo_root "${CMAKE_CURRENT_LIST_DIR}/..")

if(NOT OBS_ROOT)
    if(DEFINED ENV{OBS_ROOT})
        set(OBS_ROOT "$ENV{OBS_ROOT}" CACHE PATH "OBS Studio installation root" FORCE)
    elseif(EXISTS "C:/Program Files/obs-studio")
        set(OBS_ROOT "C:/Program Files/obs-studio" CACHE PATH "OBS Studio installation root" FORCE)
    endif()
endif()

if(NOT OBS_HEADERS_DIR)
    if(DEFINED ENV{OBS_HEADERS_DIR})
        set(OBS_HEADERS_DIR "$ENV{OBS_HEADERS_DIR}" CACHE PATH "obs-studio sources" FORCE)
    else()
        set(OBS_HEADERS_DIR "${_ocp_repo_root}/deps/obs-headers/obs-studio" CACHE PATH "obs-studio sources" FORCE)
    endif()
endif()

find_path(LIBOBS_INCLUDE_DIR
    NAMES obs.h
    HINTS "${OBS_HEADERS_DIR}/libobs"
    PATHS "${OBS_HEADERS_DIR}/libobs"
    NO_DEFAULT_PATH)

if(WIN32)
    if(NOT OBS_IMPORT_LIB)
        if(DEFINED ENV{OBS_IMPORT_LIB})
            set(OBS_IMPORT_LIB "$ENV{OBS_IMPORT_LIB}" CACHE FILEPATH "obs import lib" FORCE)
        else()
            set(OBS_IMPORT_LIB "${_ocp_repo_root}/deps/obs-import/obs.lib" CACHE FILEPATH "obs import lib" FORCE)
        endif()
    endif()
    if(EXISTS "${OBS_IMPORT_LIB}")
        set(LIBOBS_LIBRARY "${OBS_IMPORT_LIB}" CACHE FILEPATH "obs import library" FORCE)
    else()
        find_library(LIBOBS_LIBRARY
            NAMES obs libobs
            HINTS "${OBS_ROOT}/lib"
            PATHS "${OBS_ROOT}/lib")
    endif()
else()
    find_library(LIBOBS_LIBRARY
        NAMES obs libobs
        HINTS "${OBS_ROOT}/lib" "${OBS_ROOT}/libobs"
        PATHS /usr/local/lib /usr/lib)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibObs
    REQUIRED_VARS LIBOBS_INCLUDE_DIR LIBOBS_LIBRARY)

if(LibObs_FOUND)
    set(LibObs_INCLUDE_DIRS "${LIBOBS_INCLUDE_DIR}")

    set(_obsconfig_in "${LIBOBS_INCLUDE_DIR}/obsconfig.h.in")
    set(_obsconfig_out "${CMAKE_BINARY_DIR}/generated/obsconfig.h")
    if(EXISTS "${_obsconfig_in}" AND NOT EXISTS "${_obsconfig_out}")
        file(READ "${_obsconfig_in}" _obsconfig_content)
        string(REPLACE "#cmakedefine" "// #cmakedefine" _obsconfig_content "${_obsconfig_content}")
        string(REPLACE "@OBS_DATA_PATH@" "data" _obsconfig_content "${_obsconfig_content}")
        string(REPLACE "@OBS_PLUGIN_PATH@" "obs-plugins" _obsconfig_content "${_obsconfig_content}")
        string(REPLACE "@OBS_PLUGIN_DESTINATION@" "obs-plugins/64bit" _obsconfig_content "${_obsconfig_content}")
        string(REPLACE "@OBS_RELEASE_CANDIDATE@" "0" _obsconfig_content "${_obsconfig_content}")
        string(REPLACE "@OBS_BETA@" "0" _obsconfig_content "${_obsconfig_content}")
        file(WRITE "${_obsconfig_out}" "${_obsconfig_content}")
    endif()

    set(LibObs_INCLUDE_DIRS "${LIBOBS_INCLUDE_DIR};${CMAKE_BINARY_DIR}/generated")

    add_library(LibObs::libobs UNKNOWN IMPORTED)
    set_target_properties(LibObs::libobs PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${LibObs_INCLUDE_DIRS}"
        IMPORTED_LOCATION "${LIBOBS_LIBRARY}")

    if(WIN32)
        set_property(TARGET LibObs::libobs APPEND PROPERTY INTERFACE_LINK_LIBRARIES "ws2_32;secur32;winmm")
    endif()
endif()

mark_as_advanced(LIBOBS_INCLUDE_DIR LIBOBS_LIBRARY)
