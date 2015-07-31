cmake_minimum_required(VERSION 2.8.8)

project(ovh-cli C)

set(OVH_CLI_VERSION_MAJOR 0)
set(OVH_CLI_VERSION_MINOR 0)
set(OVH_CLI_VERSION_PATCH 1)

math(EXPR OVH_CLI_VERSION_NUMBER "${OVH_CLI_VERSION_MAJOR} * 1000 + ${OVH_CLI_VERSION_MINOR} * 100 + ${OVH_CLI_VERSION_PATCH}")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall -Wextra -std=c99 -Wwrite-strings -Wstrict-prototypes -Wuninitialized -Wunreachable-code -Wno-comment -Wnonnull -Wunreachable-code")
add_definitions(-D_GNU_SOURCE)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})

set(MISC_SOURCES )
set(LIBRARIES )

find_package(CURL REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Intl QUIET)
find_package(Gettext QUIET)
# set(EDITLINE_DEBUG TRUE)
find_package(EditLine REQUIRED)

find_path(ICONV_INCLUDE_DIR iconv.h)
find_library(ICONV_LIBRARIES NAMES iconv libiconv c)
if(NOT ICONV_LIBRARIES)
    message(FATAL_ERROR "iconv not found")
endif(NOT ICONV_LIBRARIES)
include(CheckCSourceCompiles)

set(CMAKE_REQUIRED_FLAGS "-Werror")
check_c_source_compiles("
#include <iconv.h>
int main(void) {
    iconv_t conv = 0;
    const char *in = 0;
    size_t ilen = 0;
    char *out = 0;
    size_t olen = 0;
    iconv(conv, &in, &ilen, &out, &olen);
    return 0;
}
" ICONV_SECOND_ARGUMENT_IS_CONST)
if(ICONV_SECOND_ARGUMENT_IS_CONST)
    set(ICONV_CONST "const")
else(ICONV_SECOND_ARGUMENT_IS_CONST)
    set(ICONV_CONST "")
endif(ICONV_SECOND_ARGUMENT_IS_CONST)

include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckFunctionExists)
include(CheckLibraryExists)

check_include_file("stdint.h" HAVE_STDINT_H)
check_include_file("inttypes.h" HAVE_INTTYPES_H)
check_function_exists("strcasecmp" HAVE_STRCASECMP)
# if(NOT HAVE_STRCASECMP)
#     check_symbol_exists("strcasecmp" "strings.h" HAVE_STRINGS_H)
# endif(NOT HAVE_STRCASECMP)
check_function_exists("strncasecmp" HAVE_STRNCASECMP)
check_function_exists("strtod_l" HAVE_STRTOD_L)
check_library_exists("iconv" "iconvctl" ${ICONV_LIBRARIES} HAVE_ICONVCTL)
check_function_exists("strlcpy" HAVE_STRLCPY)
if(NOT HAVE_STRLCPY)
    check_library_exists("bsd" "strlcpy" "lib" HAVE_LIBBSD_STRLCPY)
    if(HAVE_LIBBSD_STRLCPY)
        list(APPEND LIBRARIES "bsd")
#     else(HAVE_LIBBSD_STRLCPY)
#         list(APPEND SOURCES "../missing/strlcpy.c")
    endif(HAVE_LIBBSD_STRLCPY)
endif(NOT HAVE_STRLCPY)

option(DEBUG "Enable/disable debugging" OFF)
option(PRINT_OVH_ID "Enable/disable display of internal OVH identifiers" OFF)

include(FeatureSummary)

set(EXTRA_MODULES )
macro(declare_module _NAME _DESC)
    string(TOUPPER ${_NAME} UC_NAME)
    option(WITH_${UC_NAME}_MODULE "Build ${_NAME} module" ON)
    add_feature_info(${_NAME} WITH_${UC_NAME}_MODULE ${_DESC})
    list(APPEND EXTRA_MODULES ${_NAME})
endmacro(declare_module)

declare_module("cloud" "public dedicated cloud")
declare_module("dedicated" "dedicated servers")
declare_module("domain" "DNS zone")
declare_module("hosting" "shared hosting")
declare_module("key" "global SSH keys")
declare_module("me" "personnal informations, account and credentials")
declare_module("support" "read, write, close and reopen your tickets")
declare_module("vps" "virtual servers")

set_package_properties(CURL PROPERTIES TYPE REQUIRED PURPOSE "HTTP(S) client to interact with OVH API")
set_package_properties(LibXml2 PROPERTIES TYPE REQUIRED PURPOSE "For configuration file management and auto-generation of consumer key")
set_package_properties(OpenSSL PROPERTIES TYPE REQUIRED PURPOSE "To sign (SHA1) HTTP request")
set_package_properties(EditLine PROPERTIES TYPE REQUIRED PURPOSE "For command line completion")
set_package_properties(Gettext PROPERTIES TYPE OPTIONAL PURPOSE "The tools to manage files translation")
set_package_properties(Intl PROPERTIES TYPE OPTIONAL PURPOSE "The library to translate internal strings")
set_package_properties(SQLite3 PROPERTIES TYPE OPTIONAL PURPOSE "Permanent storage for some datas")

feature_summary(INCLUDE_QUIET_PACKAGES WHAT ALL)

include_directories(
    ${Intl_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${ICONV_INCLUDE_DIR}
    ${LIBXML2_INCLUDE_DIR}
    ${OPENSSL_INCLUDE_DIR}
    ${SQLITE3_INCLUDE_DIRS}
    ${EDITLINE_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(INTL_FOUND)
    set(WITH_NLS true)
    set(LOCALEDIR "${CMAKE_INSTALL_PREFIX}/share/locale/")
    add_subdirectory(i18n)
    list(APPEND MISC_SOURCES "modules/nls.c")
endif(INTL_FOUND)

set(
    BUILTIN_MODULES
    modules/home.c modules/sqlite.c modules/openssl.c modules/curl.c modules/libxml.c modules/api.c modules/conv.c modules/table.c modules/base.c
    commands/account.c
)

# set(EXTRA_MODULES cloud dedicated domain me hosting vps key support)

set(EXTRA_MODULES_ENABLED )
foreach(MODULE ${EXTRA_MODULES})
    string(TOUPPER ${MODULE} UC_NAME)
    if(WITH_${UC_NAME}_MODULE)
        list(APPEND EXTRA_MODULES_ENABLED "commands/${MODULE}.c")
    endif(WITH_${UC_NAME}_MODULE)
endforeach(MODULE)

configure_file(
    "config.h.in"
    "config.h"
    @ONLY
)

add_executable(ovh main.c graph.c json.c error.c util.c model.c date.c graphic.c
    struct/iterator.c struct/hashtable.c struct/dptrarray.c struct/xtring.c
    ${BUILTIN_MODULES} ${EXTRA_MODULES_ENABLED} ${MISC_SOURCES}
)
target_link_libraries(ovh ${LIBRARIES} ${Intl_LIBRARIES} ${CURL_LIBRARIES} ${ICONV_LIBRARIES} ${LIBXML2_LIBRARIES} ${OPENSSL_LIBRARIES} ${SQLITE3_LIBRARIES} ${EDITLINE_LIBRARIES})

install(TARGETS ovh DESTINATION bin)

execute_process(COMMAND ${CMAKE_CURRENT_BINARY_DIR}/ovh help OUTPUT_VARIABLE HELP)
configure_file(
    "README.md.in"
    "README.md"
    @ONLY
)
