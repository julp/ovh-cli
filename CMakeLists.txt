cmake_minimum_required(VERSION 2.8.8)

project(ovh-cli C)

find_package(CURL REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(OpenSSL REQUIRED)

include_directories(
    ${CURL_INCLUDE_DIRS}
    ${LIBXML2_INCLUDE_DIR}
    ${OPENSSL_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(NOT APPLICATION_KEY)
    message(FATAL_ERROR "Expected cmake paramater -DAPPLICATION_KEY= to set your 'application key'")
endif(NOT APPLICATION_KEY)
if(NOT APPLICATION_SECRET)
    message(FATAL_ERROR "Expected cmake paramater -DAPPLICATION_SECRET= to set your 'application secret'")
endif(NOT APPLICATION_SECRET)
if(NOT API_BASE_URL)
    set(API_BASE_URL "https://eu.api.ovh.com/1.0")
endif(NOT API_BASE_URL)

configure_file(
    "config.h.in"
    "config.h"
    @ONLY
)

# add_definitions(-DTEST=1)
add_definitions(-DDEBUG=1)
add_executable(ovh main.c
    linenoise.c
    modules/openssl.c modules/curl.c modules/libxml.c
    modules/api.c modules/conv.c
    modules/base.c
    struct/iterator.c struct/hashtable.c
    commands/account.c commands/domain.c commands/me.c commands/dedicated.c
)
target_link_libraries(ovh ${LIBXML2_LIBRARIES} ${CURL_LIBRARIES} ${OPENSSL_LIBRARIES})
