cmake_minimum_required(VERSION 3.24)

project(briefutil VERSION 0.2.0 LANGUAGES C CXX)

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
# Shared helper: stage the runtime DLLs an executable target loads
# ============================================================================

# Windows resolves an executable's Qt and compiler runtime DLLs from the
# directory the executable sits in, so one output directory is one staging
# destination. A per-target POST_BUILD copy gives that destination as many
# concurrent writers as there are targets landing there, and copy_if_different
# is not atomic against a concurrent writer: a parallel build can fail with
# "Error copying file (if different)", as mark2haru's did when its bundled
# fonts were staged in this same shape. So a destination gets exactly one owner
# target that stages it, and every consumer landing there takes a dependency
# edge on that owner instead of copying anything itself.
#
# The owner stages the union of its consumers' DLLs rather than any one
# consumer's. Unlike a fixed asset list, $<TARGET_RUNTIME_DLLS:> is
# target-specific - the one test that links Qt6::Gui needs a DLL the others do
# not - so an owner that staged the first consumer's closure and called the
# destination done would leave the others failing to load at run time. Each
# consumer therefore contributes its own copy command to the owner. Those
# commands belong to a single target and so run in sequence, which is what
# removes the race; spreading them over separate targets is what created it.
function(briefutil_stage_runtime_dlls target)
    if(NOT WIN32)
        return()
    endif()

    get_target_property(_briefutil_output_dir ${target} RUNTIME_OUTPUT_DIRECTORY)
    if(NOT _briefutil_output_dir)
        set(_briefutil_output_dir "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    # The directory is used verbatim as the key: any lossy transform of it
    # (a C identifier, say) would map two destinations differing only in
    # punctuation onto one owner, and the loser would never be staged.
    set(_briefutil_owner_property
        "BRIEFUTIL_RUNTIME_DLL_OWNER_${_briefutil_output_dir}")
    set(_briefutil_scope_property
        "BRIEFUTIL_RUNTIME_DLL_OWNER_SCOPE_${_briefutil_output_dir}")

    get_property(_briefutil_owner GLOBAL PROPERTY "${_briefutil_owner_property}")
    if(_briefutil_owner)
        # Contributing to an existing owner means add_custom_command(TARGET),
        # which may only extend a target created in the current directory. Name
        # the conflict here rather than let CMake report the generated owner as
        # a missing target and leave the real cause - two directories writing
        # one destination - to be guessed.
        get_property(_briefutil_owner_scope GLOBAL
            PROPERTY "${_briefutil_scope_property}")
        if(NOT "${_briefutil_owner_scope}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
            message(FATAL_ERROR
                "${target} stages runtime DLLs into ${_briefutil_output_dir}, "
                "which is already staged by ${_briefutil_owner} in "
                "${_briefutil_owner_scope}. Declare both targets in one "
                "directory or give this one its own output directory.")
        endif()
    else()
        set(_briefutil_owner "briefutil_runtime_dlls_for_${target}")
        # The owner runs ahead of every consumer that depends on it, so the
        # output directory need not exist yet. Without this the copy of a lone
        # DLL would create the destination as a file rather than fill it.
        add_custom_target(${_briefutil_owner}
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Staging briefutil runtime DLLs"
            VERBATIM
        )
        # The compiler runtime is the same set for every consumer, so it is
        # staged once when the owner is created rather than per consumer.
        foreach(_briefutil_runtime_dll IN LISTS BRIEFUTIL_COMPILER_RUNTIME_DLLS)
            if(EXISTS "${BRIEFUTIL_CXX_COMPILER_DIR}/${_briefutil_runtime_dll}")
                add_custom_command(TARGET ${_briefutil_owner} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${BRIEFUTIL_CXX_COMPILER_DIR}/${_briefutil_runtime_dll}"
                        "$<TARGET_FILE_DIR:${target}>"
                    VERBATIM
                )
            endif()
        endforeach()
        set_property(GLOBAL PROPERTY "${_briefutil_owner_property}"
            "${_briefutil_owner}")
        set_property(GLOBAL PROPERTY "${_briefutil_scope_property}"
            "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    # The destination is spelled as a generator expression rather than as the
    # key above so that a multi-config generator picks up its per-configuration
    # subdirectory without this function knowing about it.
    add_custom_command(TARGET ${_briefutil_owner} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${target}>
            "$<TARGET_FILE_DIR:${target}>"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
    add_dependencies(${target} ${_briefutil_owner})
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
