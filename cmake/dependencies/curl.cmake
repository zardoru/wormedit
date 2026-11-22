# Static libcurl 8.11.1 (MIT-style curl license, COPYING in the archive) for the update check in
# packages that cannot rely on a system libcurl: the AppImage (OpenSSL, static) and Windows
# (Schannel). Everything but HTTP(S) is off; CA certificates come from the OS.
set(WORMEDIT_CURL_SOURCE_DIR "" CACHE PATH "Path to curl 8.11.1 sources")

if(TARGET CURL::libcurl)
    return()
endif()

if(WORMEDIT_CURL_SOURCE_DIR)
    FetchContent_Declare(wormedit_curl SOURCE_DIR "${WORMEDIT_CURL_SOURCE_DIR}")
else()
    FetchContent_Declare(
        wormedit_curl
        URL https://github.com/curl/curl/releases/download/curl-8_11_1/curl-8.11.1.tar.xz
        URL_HASH SHA256=c7ca7db48b0909743eaef34250da02c19bc61d4f1dcedd6603f109409536ab56
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()

set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
if(WIN32)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
else()
    set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
    set(OPENSSL_USE_STATIC_LIBS ON)
endif()
FetchContent_MakeAvailable(wormedit_curl)
if(NOT TARGET CURL::libcurl)
    add_library(CURL::libcurl ALIAS libcurl_static)
endif()
set(CURL_FOUND TRUE)
