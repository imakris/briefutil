cmake_minimum_required(VERSION 3.24)

project(briefutil VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
cmake_policy(SET CMP0074 NEW)

# ============================================================================
# libHaru (static, with PNG support via zlib + libpng)
# ============================================================================

include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL  ON  CACHE BOOL "" FORCE)

# -- zlib --
FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.1
)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)

if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB INTERFACE IMPORTED)
    target_link_libraries(ZLIB::ZLIB INTERFACE zlibstatic)
    target_include_directories(ZLIB::ZLIB INTERFACE
        "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
endif()
set(ZLIB_FOUND       TRUE CACHE BOOL "" FORCE)
set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}" CACHE PATH "" FORCE)
set(ZLIB_LIBRARY     "zlibstatic" CACHE STRING "" FORCE)

# -- libpng --
FetchContent_Declare(libpng
    GIT_REPOSITORY https://github.com/pnggroup/libpng.git
    GIT_TAG        v1.6.47
)
set(PNG_SHARED    OFF CACHE BOOL "" FORCE)
set(PNG_STATIC    ON  CACHE BOOL "" FORCE)
set(PNG_TESTS     OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS     OFF CACHE BOOL "" FORCE)
set(PNG_FRAMEWORK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libpng)

if(NOT TARGET PNG::PNG)
    add_library(PNG::PNG INTERFACE IMPORTED)
    target_link_libraries(PNG::PNG INTERFACE png_static)
    target_include_directories(PNG::PNG INTERFACE
        "${libpng_SOURCE_DIR}" "${libpng_BINARY_DIR}")
endif()
set(PNG_FOUND           TRUE CACHE BOOL "" FORCE)
set(PNG_PNG_INCLUDE_DIR "${libpng_SOURCE_DIR}" CACHE PATH "" FORCE)
set(PNG_LIBRARY         "png_static" CACHE STRING "" FORCE)

# -- libHaru --
FetchContent_Declare(libharu
    GIT_REPOSITORY https://github.com/libharu/libharu.git
    GIT_TAG        v2.4.5
)
set(LIBHPDF_EXAMPLES OFF CACHE BOOL "" FORCE)
if(DEFINED CMAKE_SKIP_INSTALL_RULES)
    set(_briefutil_saved_skip_install_rules_defined TRUE)
    set(_briefutil_saved_skip_install_rules "${CMAKE_SKIP_INSTALL_RULES}")
endif()
set(CMAKE_SKIP_INSTALL_RULES ON)
FetchContent_MakeAvailable(libharu)
if(_briefutil_saved_skip_install_rules_defined)
    set(CMAKE_SKIP_INSTALL_RULES "${_briefutil_saved_skip_install_rules}")
else()
    unset(CMAKE_SKIP_INSTALL_RULES)
endif()
unset(_briefutil_saved_skip_install_rules_defined)
unset(_briefutil_saved_skip_install_rules)
file(WRITE "${libharu_BINARY_DIR}/cmake_install.cmake"
    "# libharu install rules are intentionally suppressed for briefutil.\n")

target_include_directories(hpdf PRIVATE "${libpng_BINARY_DIR}")
add_dependencies(hpdf png_static)

# ============================================================================
# Qt 6
# ============================================================================

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickDialogs2)
get_filename_component(BRIEFUTIL_QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)
set(BRIEFUTIL_WINDEPLOYQT "${BRIEFUTIL_QT_BIN_DIR}/windeployqt.exe")
get_filename_component(BRIEFUTIL_CXX_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

# ============================================================================
# Shared helper: copy runtime DLLs for an executable target
# ============================================================================

function(briefutil_copy_runtime_dlls target)
    if(WIN32)
        foreach(_runtime_dll IN ITEMS libc++.dll libunwind.dll)
            if(EXISTS "${BRIEFUTIL_CXX_COMPILER_DIR}/${_runtime_dll}")
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${BRIEFUTIL_CXX_COMPILER_DIR}/${_runtime_dll}"
                        "$<TARGET_FILE_DIR:${target}>"
                    VERBATIM
                )
            endif()
        endforeach()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}>
                $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    endif()
endfunction()

add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(tests)

# ============================================================================
# Packaging
# ============================================================================

set(CPACK_PACKAGE_NAME "briefutil")
set(CPACK_PACKAGE_VENDOR "briefutil")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Utility for generating PDF letters with Qt Quick.")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})

if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "briefutil")
    set(CPACK_NSIS_CONTACT "noreply@example.com")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
endif()

include(CPack)
