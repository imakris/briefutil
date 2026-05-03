cmake_minimum_required(VERSION 3.24)

project(briefutil VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
cmake_policy(SET CMP0074 NEW)

option(BRIEFUTIL_BUILD_GUI "Build the Qt Quick GUI frontend" ON)

include(FetchContent)

# ============================================================================
# mark2haru
# ============================================================================

set(_briefutil_default_mark2haru_source_dir "")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../mark2haru/CMakeLists.txt")
    get_filename_component(
        _briefutil_default_mark2haru_source_dir
        "${CMAKE_CURRENT_LIST_DIR}/../../mark2haru"
        ABSOLUTE
    )
endif()

set(BRIEFUTIL_MARK2HARU_SOURCE_DIR "${_briefutil_default_mark2haru_source_dir}"
    CACHE PATH "Path to a local mark2haru checkout")
unset(_briefutil_default_mark2haru_source_dir)

set(MARK2HARU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(MARK2HARU_BUILD_TESTS OFF CACHE BOOL "" FORCE)

if(BRIEFUTIL_MARK2HARU_SOURCE_DIR)
    if(NOT EXISTS "${BRIEFUTIL_MARK2HARU_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "BRIEFUTIL_MARK2HARU_SOURCE_DIR does not contain a mark2haru CMake project.")
    endif()
    FetchContent_Declare(mark2haru
        SOURCE_DIR "${BRIEFUTIL_MARK2HARU_SOURCE_DIR}"
    )
else()
    FetchContent_Declare(mark2haru
        GIT_REPOSITORY https://github.com/imakris/mark2haru.git
        GIT_TAG        master
    )
endif()

set(_briefutil_saved_cxx_standard "${CMAKE_CXX_STANDARD}")
set(_briefutil_saved_cxx_standard_required "${CMAKE_CXX_STANDARD_REQUIRED}")
set(_briefutil_saved_cxx_extensions "${CMAKE_CXX_EXTENSIONS}")
FetchContent_MakeAvailable(mark2haru)
set(CMAKE_CXX_STANDARD "${_briefutil_saved_cxx_standard}")
set(CMAKE_CXX_STANDARD_REQUIRED "${_briefutil_saved_cxx_standard_required}")
set(CMAKE_CXX_EXTENSIONS "${_briefutil_saved_cxx_extensions}")
unset(_briefutil_saved_cxx_standard)
unset(_briefutil_saved_cxx_standard_required)
unset(_briefutil_saved_cxx_extensions)

if(NOT TARGET mark2haru::mark2haru)
    message(FATAL_ERROR "mark2haru::mark2haru was not provided by the mark2haru dependency.")
endif()

function(briefutil_copy_mark2haru_fonts target)
    mark2haru_copy_bundled_fonts(${target} "$<TARGET_FILE_DIR:${target}>/fonts")
endfunction()

# ============================================================================
# Qt 6
# ============================================================================

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core)
if(BRIEFUTIL_BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Gui Qml Quick QuickControls2 QuickDialogs2)
endif()
get_filename_component(BRIEFUTIL_QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)
set(BRIEFUTIL_WINDEPLOYQT "${BRIEFUTIL_QT_BIN_DIR}/windeployqt.exe")
get_filename_component(BRIEFUTIL_CXX_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
set(BRIEFUTIL_COMPILER_RUNTIME_DLLS
    libc++.dll
    libunwind.dll
    libgcc_s_seh-1.dll
    libstdc++-6.dll
    libwinpthread-1.dll
)

# ============================================================================
# Shared helper: copy runtime DLLs for an executable target
# ============================================================================

function(briefutil_copy_compiler_runtime_dlls target)
    if(WIN32)
        foreach(_runtime_dll IN LISTS BRIEFUTIL_COMPILER_RUNTIME_DLLS)
            if(EXISTS "${BRIEFUTIL_CXX_COMPILER_DIR}/${_runtime_dll}")
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${BRIEFUTIL_CXX_COMPILER_DIR}/${_runtime_dll}"
                        "$<TARGET_FILE_DIR:${target}>"
                    VERBATIM
                )
            endif()
        endforeach()
    endif()
endfunction()

function(briefutil_copy_runtime_dlls target)
    if(WIN32)
        briefutil_copy_compiler_runtime_dlls(${target})
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}>
                $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    endif()
endfunction()

function(briefutil_install_compiler_runtime_dlls)
    if(WIN32)
        foreach(_runtime_dll IN LISTS BRIEFUTIL_COMPILER_RUNTIME_DLLS)
            if(EXISTS "${BRIEFUTIL_CXX_COMPILER_DIR}/${_runtime_dll}")
                install(FILES "${BRIEFUTIL_CXX_COMPILER_DIR}/${_runtime_dll}"
                    DESTINATION bin
                )
            endif()
        endforeach()
    endif()
endfunction()

enable_testing()

add_subdirectory(core)
add_subdirectory(cli)
if(BRIEFUTIL_BUILD_GUI)
    add_subdirectory(app)
endif()
add_subdirectory(tests)
briefutil_install_compiler_runtime_dlls()
install(DIRECTORY "${MARK2HARU_BUNDLED_FONT_DIR}/"
    DESTINATION bin/fonts
    FILES_MATCHING PATTERN "*.ttf"
)

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
