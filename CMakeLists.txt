cmake_minimum_required(VERSION 3.16)

project(QgBook C)

set(CMAKE_C_STANDARD 17)

# 리소스 XML 경로 및 gresource C 파일 생성 위치
set(RESOURCE_XML ${CMAKE_SOURCE_DIR}/QgBook/resg.xml)
set(GRESOURCE_C ${CMAKE_SOURCE_DIR}/QgBook/resg.c)

# gresource C 파일 생성 명령
add_custom_command(
        OUTPUT ${GRESOURCE_C}
        COMMAND glib-compile-resources
        --target=${GRESOURCE_C}
        --generate-source
        --sourcedir=${CMAKE_SOURCE_DIR}/QgBook
        ${RESOURCE_XML}
        DEPENDS ${RESOURCE_XML}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/QgBook
)

# 소스 파일 목록 (resg.c 포함)
file(GLOB SRC_FILES
        QgBook/*.c
)
list(APPEND SRC_FILES ${GRESOURCE_C})

# 윈도우에서 리소스 파일 추가
if (WIN32)
    list(APPEND SRC_FILES ${CMAKE_SOURCE_DIR}/QgBook/Resource.rc)
endif()

# 패키지 찾기
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(ZLIB REQUIRED zlib)
pkg_check_modules(LIBZIP REQUIRED libzip)

add_executable(QgBook WIN32 ${SRC_FILES})

# 정의 추가
target_compile_definitions(QgBook PRIVATE
        CMAKE_VERSION=1
)

# 미리 컴파일 할 헤더
target_precompile_headers(QgBook PRIVATE "QgBook/pch.h")

# include 디렉토리 추가
target_include_directories(QgBook PRIVATE
        ${GTK4_INCLUDE_DIRS}
        ${GLIB_INCLUDE_DIRS}
        ${SQLITE3_INCLUDE_DIRS}
        ${ZLIB_INCLUDE_DIRS}
        ${LIBZIP_INCLUDE_DIRS}
        QgBook
)

# 라이브러리 링크
if (MSVC)
    target_link_libraries(QgBook
        ${GTK4_LIBRARIES}
        ${GLIB_LIBRARIES}
        ${SQLITE3_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${LIBZIP_LIBRARIES}
    )
else()
    target_link_libraries(QgBook
        m
        ${GTK4_LIBRARIES}
        ${GLIB_LIBRARIES}
        ${SQLITE3_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${LIBZIP_LIBRARIES}
    )
endif()

# 컴파일러 옵션
target_compile_options(QgBook PRIVATE
        $<$<C_COMPILER_ID:MSVC>:/wd4819>
        ${GTK4_CFLAGS_OTHER}
        ${GLIB_CFLAGS_OTHER}
        ${SQLITE3_CFLAGS_OTHER}
        ${ZLIB_CFLAGS_OTHER}
        ${LIBZIP_CFLAGS_OTHER}
)

#  링크 옵션
target_link_directories(QgBook PRIVATE
        ${GTK4_LIBRARY_DIRS}
        ${GLIB_LIBRARY_DIRS}
        ${SQLITE3_LIBRARY_DIRS}
        ${ZLIB_LIBRARY_DIRS}
        ${LIBZIP_LIBRARY_DIRS}
)

# gresource C 파일을 늘 만들게 빌드 의존성 추가
add_custom_target(generate_gresource DEPENDS ${GRESOURCE_C})
add_dependencies(QgBook generate_gresource)

# 아이콘 설치
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    install(
            FILES "${CMAKE_SOURCE_DIR}/QgBook/data/qg-book-48.png"
            DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/48x48/apps/"
            RENAME "qgbook.png"
    )
endif ()
