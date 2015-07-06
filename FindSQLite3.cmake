#
# This module is designed to find/handle sqlite3 library
#
# Requirements:
# - CMake >= 2.8.3 (for new version of find_package_handle_standard_args)
#
# The following variables will be defined for your use:
#   - SQLITE3_INCLUDE_DIRS  : sqlite3 include directory
#   - SQLITE3_LIBRARIES     : sqlite3 libraries
#   - SQLITE3_VERSION       : complete version of sqlite3 (x.y.z)
#   - SQLITE3_MAJOR_VERSION : major version of sqlite3
#   - SQLITE3_MINOR_VERSION : minor version of sqlite3
#   - SQLITE3_PATCH_VERSION : patch version of sqlite3
#
# How to use:
#   1) Copy this file in the root of your project source directory
#   2) Then, tell CMake to search this non-standard module in your project directory by adding to your CMakeLists.txt:
#        set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})
#   3) Finally call find_package(SQLite3) once
#
# Here is a complete sample to build an executable:
#
#   set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})
#
#   find_package(SQLite3 REQUIRED) # Note: name is case sensitive
#
#   include_directories(${SQLITE3_INCLUDE_DIRS})
#   add_executable(myapp myapp.c)
#   target_link_libraries(myapp ${SQLITE3_LIBRARIES})
#


#=============================================================================
# Copyright (c) 2013, julp
#
# Distributed under the OSI-approved BSD License
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#=============================================================================

cmake_minimum_required(VERSION 2.8.3)

########## Private ##########
if(NOT DEFINED SQLITE3_PUBLIC_VAR_NS)
    set(SQLITE3_PUBLIC_VAR_NS "SQLITE3")
endif(NOT DEFINED SQLITE3_PUBLIC_VAR_NS)
if(NOT DEFINED SQLITE3_PRIVATE_VAR_NS)
    set(SQLITE3_PRIVATE_VAR_NS "_${SQLITE3_PUBLIC_VAR_NS}")
endif(NOT DEFINED SQLITE3_PRIVATE_VAR_NS)

function(sqlite3_debug _VARNAME)
    if(${SQLITE3_PUBLIC_VAR_NS}_DEBUG)
        if(DEFINED ${SQLITE3_PUBLIC_VAR_NS}_${_VARNAME})
            message("${SQLITE3_PUBLIC_VAR_NS}_${_VARNAME} = ${${SQLITE3_PUBLIC_VAR_NS}_${_VARNAME}}")
        else(DEFINED ${SQLITE3_PUBLIC_VAR_NS}_${_VARNAME})
            message("${SQLITE3_PUBLIC_VAR_NS}_${_VARNAME} = <UNDEFINED>")
        endif(DEFINED ${SQLITE3_PUBLIC_VAR_NS}_${_VARNAME})
    endif(${SQLITE3_PUBLIC_VAR_NS}_DEBUG)
endfunction(sqlite3_debug)

# Alias all SQLite3_FIND_X variables to SQLITE3_FIND_X
# Workaround for find_package: no way to force case of variable's names it creates (I don't want to change MY coding standard)
set(${SQLITE3_PRIVATE_VAR_NS}_FIND_PKG_PREFIX "SQLite3")
get_directory_property(${SQLITE3_PRIVATE_VAR_NS}_CURRENT_VARIABLES VARIABLES)
foreach(${SQLITE3_PRIVATE_VAR_NS}_VARNAME ${${SQLITE3_PRIVATE_VAR_NS}_CURRENT_VARIABLES})
    if(${SQLITE3_PRIVATE_VAR_NS}_VARNAME MATCHES "^${${SQLITE3_PRIVATE_VAR_NS}_FIND_PKG_PREFIX}")
        string(REGEX REPLACE "^${${SQLITE3_PRIVATE_VAR_NS}_FIND_PKG_PREFIX}" "${SQLITE3_PUBLIC_VAR_NS}" ${SQLITE3_PRIVATE_VAR_NS}_NORMALIZED_VARNAME ${${SQLITE3_PRIVATE_VAR_NS}_VARNAME})
        set(${${SQLITE3_PRIVATE_VAR_NS}_NORMALIZED_VARNAME} ${${${SQLITE3_PRIVATE_VAR_NS}_VARNAME}})
    endif(${SQLITE3_PRIVATE_VAR_NS}_VARNAME MATCHES "^${${SQLITE3_PRIVATE_VAR_NS}_FIND_PKG_PREFIX}")
endforeach(${SQLITE3_PRIVATE_VAR_NS}_VARNAME)

########## Public ##########
find_path(
    ${SQLITE3_PUBLIC_VAR_NS}_INCLUDE_DIRS
    NAMES sqlite3.h
    PATH_SUFFIXES "include"
)

if(${SQLITE3_PUBLIC_VAR_NS}_INCLUDE_DIRS)

    find_library(
        ${SQLITE3_PUBLIC_VAR_NS}_LIBRARIES
        NAMES sqlite3
    )

    file(STRINGS "${${SQLITE3_PUBLIC_VAR_NS}_INCLUDE_DIRS}/sqlite3.h" ${SQLITE3_PRIVATE_VAR_NS}_VERSION_NUMBER_DEFINITION LIMIT_COUNT 1 REGEX ".*# *define *SQLITE_VERSION_NUMBER *([0-9]+).*")
    string(REGEX REPLACE ".*# *define +SQLITE_VERSION_NUMBER +([0-9]+).*" "\\1" ${SQLITE3_PRIVATE_VAR_NS}_VERSION_NUMBER ${${SQLITE3_PRIVATE_VAR_NS}_VERSION_NUMBER_DEFINITION})

    math(EXPR ${SQLITE3_PUBLIC_VAR_NS}_MAJOR_VERSION "${${SQLITE3_PRIVATE_VAR_NS}_VERSION_NUMBER} / 1000000")
    math(EXPR ${SQLITE3_PUBLIC_VAR_NS}_MINOR_VERSION "(${${SQLITE3_PRIVATE_VAR_NS}_VERSION_NUMBER} - ${${SQLITE3_PUBLIC_VAR_NS}_MAJOR_VERSION} * 1000000) / 1000")
    math(EXPR ${SQLITE3_PUBLIC_VAR_NS}_PATCH_VERSION "${${SQLITE3_PRIVATE_VAR_NS}_VERSION_NUMBER} - ${${SQLITE3_PUBLIC_VAR_NS}_MAJOR_VERSION} * 1000000 - ${${SQLITE3_PUBLIC_VAR_NS}_MINOR_VERSION} * 1000")
    set(${SQLITE3_PUBLIC_VAR_NS}_VERSION "${${SQLITE3_PUBLIC_VAR_NS}_MAJOR_VERSION}.${${SQLITE3_PUBLIC_VAR_NS}_MINOR_VERSION}.${${SQLITE3_PUBLIC_VAR_NS}_PATCH_VERSION}")

    include(FindPackageHandleStandardArgs)
    if(${SQLITE3_PUBLIC_VAR_NS}_FIND_REQUIRED AND NOT ${SQLITE3_PUBLIC_VAR_NS}_FIND_QUIETLY)
        find_package_handle_standard_args(
            ${SQLITE3_PUBLIC_VAR_NS}
            REQUIRED_VARS ${SQLITE3_PUBLIC_VAR_NS}_LIBRARIES ${SQLITE3_PUBLIC_VAR_NS}_INCLUDE_DIRS
            VERSION_VAR ${SQLITE3_PUBLIC_VAR_NS}_VERSION
        )
    else(${SQLITE3_PUBLIC_VAR_NS}_FIND_REQUIRED AND NOT ${SQLITE3_PUBLIC_VAR_NS}_FIND_QUIETLY)
        find_package_handle_standard_args(${SQLITE3_PUBLIC_VAR_NS} "sqlite3 not found" ${SQLITE3_PUBLIC_VAR_NS}_LIBRARIES ${SQLITE3_PUBLIC_VAR_NS}_INCLUDE_DIRS)
    endif(${SQLITE3_PUBLIC_VAR_NS}_FIND_REQUIRED AND NOT ${SQLITE3_PUBLIC_VAR_NS}_FIND_QUIETLY)

else(${SQLITE3_PUBLIC_VAR_NS}_INCLUDE_DIRS)

    if(${SQLITE3_PUBLIC_VAR_NS}_FIND_REQUIRED AND NOT ${SQLITE3_PUBLIC_VAR_NS}_FIND_QUIETLY)
        message(FATAL_ERROR "Could not find sqlite3 include directory")
    endif(${SQLITE3_PUBLIC_VAR_NS}_FIND_REQUIRED AND NOT ${SQLITE3_PUBLIC_VAR_NS}_FIND_QUIETLY)

endif(${SQLITE3_PUBLIC_VAR_NS}_INCLUDE_DIRS)

mark_as_advanced(
    ${SQLITE3_PUBLIC_VAR_NS}_INCLUDE_DIRS
    ${SQLITE3_PUBLIC_VAR_NS}_LIBRARIES
)

# IN (args)
sqlite3_debug("FIND_REQUIRED")
sqlite3_debug("FIND_QUIETLY")
sqlite3_debug("FIND_VERSION")
# OUT
# Linking
sqlite3_debug("INCLUDE_DIRS")
sqlite3_debug("LIBRARIES")
# Version
sqlite3_debug("MAJOR_VERSION")
sqlite3_debug("MINOR_VERSION")
sqlite3_debug("PATCH_VERSION")
sqlite3_debug("VERSION")
