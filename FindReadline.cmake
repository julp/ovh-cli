#
# This module is designed to find/handle readline library
#
# Requirements:
# - CMake >= 2.8.3 (for new version of find_package_handle_standard_args)
#
# The following variables will be defined for your use:
#   - READLINE_INCLUDE_DIRS  : readline include directory
#   - READLINE_LIBRARIES     : readline libraries
#   - READLINE_VERSION       : complete version of readline (x.y)
#   - READLINE_MAJOR_VERSION : major version of readline
#   - READLINE_MINOR_VERSION : minor version of readline
#
# How to use:
#   1) Copy this file in the root of your project source directory
#   2) Then, tell CMake to search this non-standard module in your project directory by adding to your CMakeLists.txt:
#        set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})
#   3) Finally call find_package(Readline) once
#
# Here is a complete sample to build an executable:
#
#   set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})
#
#   find_package(Readline REQUIRED) # Note: name is case sensitive
#
#   include_directories(${READLINE_INCLUDE_DIRS})
#   add_executable(myapp myapp.c)
#   target_link_libraries(myapp ${READLINE_LIBRARIES})
#


#=============================================================================
# Copyright (c) 2014, julp
#
# Distributed under the OSI-approved BSD License
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#=============================================================================

cmake_minimum_required(VERSION 2.8.3)

########## Private ##########
if(NOT DEFINED READLINE_PUBLIC_VAR_NS)
    set(READLINE_PUBLIC_VAR_NS "READLINE")
endif(NOT DEFINED READLINE_PUBLIC_VAR_NS)
if(NOT DEFINED READLINE_PRIVATE_VAR_NS)
    set(READLINE_PRIVATE_VAR_NS "_${READLINE_PUBLIC_VAR_NS}")
endif(NOT DEFINED READLINE_PRIVATE_VAR_NS)

function(readline_debug _VARNAME)
    if(${READLINE_PUBLIC_VAR_NS}_DEBUG)
        if(DEFINED ${READLINE_PUBLIC_VAR_NS}_${_VARNAME})
            message("${READLINE_PUBLIC_VAR_NS}_${_VARNAME} = ${${READLINE_PUBLIC_VAR_NS}_${_VARNAME}}")
        else(DEFINED ${READLINE_PUBLIC_VAR_NS}_${_VARNAME})
            message("${READLINE_PUBLIC_VAR_NS}_${_VARNAME} = <UNDEFINED>")
        endif(DEFINED ${READLINE_PUBLIC_VAR_NS}_${_VARNAME})
    endif(${READLINE_PUBLIC_VAR_NS}_DEBUG)
endfunction(readline_debug)

# Alias all Readline_FIND_X variables to READLINE_FIND_X
# Workaround for find_package: no way to force case of variable's names it creates (I don't want to change MY coding standard)
# ---
# NOTE: only prefix is considered, not full name of the variables to minimize conflicts with string(TOUPPER) for example
# Readline_foo becomes READLINE_foo not Readline_FOO as this is two different variables
set(${READLINE_PRIVATE_VAR_NS}_FIND_PKG_PREFIX "Readline")
get_directory_property(${READLINE_PRIVATE_VAR_NS}_CURRENT_VARIABLES VARIABLES)
foreach(${READLINE_PRIVATE_VAR_NS}_VARNAME ${${READLINE_PRIVATE_VAR_NS}_CURRENT_VARIABLES})
    if(${READLINE_PRIVATE_VAR_NS}_VARNAME MATCHES "^${${READLINE_PRIVATE_VAR_NS}_FIND_PKG_PREFIX}")
        string(REGEX REPLACE "^${${READLINE_PRIVATE_VAR_NS}_FIND_PKG_PREFIX}" "${READLINE_PUBLIC_VAR_NS}" ${READLINE_PRIVATE_VAR_NS}_NORMALIZED_VARNAME ${${READLINE_PRIVATE_VAR_NS}_VARNAME})
        set(${${READLINE_PRIVATE_VAR_NS}_NORMALIZED_VARNAME} ${${${READLINE_PRIVATE_VAR_NS}_VARNAME}})
    endif(${READLINE_PRIVATE_VAR_NS}_VARNAME MATCHES "^${${READLINE_PRIVATE_VAR_NS}_FIND_PKG_PREFIX}")
endforeach(${READLINE_PRIVATE_VAR_NS}_VARNAME)

########## Public ##########
find_path(
    ${READLINE_PUBLIC_VAR_NS}_INCLUDE_DIRS
    NAMES readline.h
    PATH_SUFFIXES readline
)

if(${READLINE_PUBLIC_VAR_NS}_INCLUDE_DIRS)

    find_library(
        ${READLINE_PUBLIC_VAR_NS}_LIBRARIES
        NAMES readline
    )

    file(READ "${${READLINE_PUBLIC_VAR_NS}_INCLUDE_DIRS}/readline.h" ${READLINE_PRIVATE_VAR_NS}_H_CONTENT)
    string(REGEX REPLACE ".*# *define +RL_VERSION_MAJOR[ \t]+([0-9]+).*" "\\1" ${READLINE_PUBLIC_VAR_NS}_MAJOR_VERSION ${${READLINE_PRIVATE_VAR_NS}_H_CONTENT})
    string(REGEX REPLACE ".*# *define +RL_VERSION_MINOR[ \t]+([0-9]+).*" "\\1" ${READLINE_PUBLIC_VAR_NS}_MINOR_VERSION ${${READLINE_PRIVATE_VAR_NS}_H_CONTENT})
    set(${READLINE_PUBLIC_VAR_NS}_VERSION "${${READLINE_PUBLIC_VAR_NS}_MAJOR_VERSION}.${${READLINE_PUBLIC_VAR_NS}_MINOR_VERSION}")

    include(FindPackageHandleStandardArgs)
    if(${READLINE_PUBLIC_VAR_NS}_FIND_REQUIRED AND NOT ${READLINE_PUBLIC_VAR_NS}_FIND_QUIETLY)
        find_package_handle_standard_args(
            ${READLINE_PUBLIC_VAR_NS}
            REQUIRED_VARS ${READLINE_PUBLIC_VAR_NS}_LIBRARIES ${READLINE_PUBLIC_VAR_NS}_INCLUDE_DIRS
            VERSION_VAR ${READLINE_PUBLIC_VAR_NS}_VERSION
        )
    else(${READLINE_PUBLIC_VAR_NS}_FIND_REQUIRED AND NOT ${READLINE_PUBLIC_VAR_NS}_FIND_QUIETLY)
        find_package_handle_standard_args(${READLINE_PUBLIC_VAR_NS} "readline not found" ${READLINE_PUBLIC_VAR_NS}_LIBRARIES ${READLINE_PUBLIC_VAR_NS}_INCLUDE_DIRS)
    endif(${READLINE_PUBLIC_VAR_NS}_FIND_REQUIRED AND NOT ${READLINE_PUBLIC_VAR_NS}_FIND_QUIETLY)

else(${READLINE_PUBLIC_VAR_NS}_INCLUDE_DIRS)

    if(${READLINE_PUBLIC_VAR_NS}_FIND_REQUIRED AND NOT ${READLINE_PUBLIC_VAR_NS}_FIND_QUIETLY)
        message(FATAL_ERROR "Could not find readline include directory")
    endif(${READLINE_PUBLIC_VAR_NS}_FIND_REQUIRED AND NOT ${READLINE_PUBLIC_VAR_NS}_FIND_QUIETLY)

endif(${READLINE_PUBLIC_VAR_NS}_INCLUDE_DIRS)

mark_as_advanced(
    ${READLINE_PUBLIC_VAR_NS}_INCLUDE_DIRS
    ${READLINE_PUBLIC_VAR_NS}_LIBRARIES
)

# IN (args)
readline_debug("FIND_REQUIRED")
readline_debug("FIND_QUIETLY")
readline_debug("FIND_VERSION")
# OUT
# Linking
readline_debug("INCLUDE_DIRS")
readline_debug("LIBRARIES")
# Version
readline_debug("MAJOR_VERSION")
readline_debug("MINOR_VERSION")
readline_debug("VERSION")
