cmake_minimum_required(VERSION 3.24)

project(briefutil VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
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

# Expose zlibstatic as ZLIB::ZLIB imported target so that find_package(ZLIB)
# in downstream deps (libpng, libHaru) finds it without searching the system.
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

# Expose png_static as PNG::PNG for libHaru
if(NOT TARGET PNG::PNG)
    add_library(PNG::PNG INTERFACE IMPORTED)
    target_link_libraries(PNG::PNG INTERFACE png_static)
    target_include_directories(PNG::PNG INTERFACE
        "${libpng_SOURCE_DIR}" "${libpng_BINARY_DIR}")
endif()
set(PNG_FOUND          TRUE CACHE BOOL "" FORCE)
set(PNG_PNG_INCLUDE_DIR "${libpng_SOURCE_DIR}" CACHE PATH "" FORCE)
set(PNG_LIBRARY        "png_static" CACHE STRING "" FORCE)

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
# CMake still emits a parent include() for the fetched subproject install
# script, so provide an explicit no-op file to keep app installs self-contained.
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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
set(_windeployqt "${_qt_bin_dir}/windeployqt.exe")

set(PROJECT_SOURCES
    main.cpp
    proxy.cpp
    pdf_renderer_haru.cpp
    sender_profile.cpp
    letter_builder.cpp
    qml.qrc
)

qt_add_executable(briefutil
    WIN32
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

target_include_directories(briefutil PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${libharu_SOURCE_DIR}/include
    ${libharu_BINARY_DIR}/include
)

target_link_libraries(briefutil
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        hpdf
)

if (WIN32)
    target_link_libraries(briefutil PRIVATE dwmapi)
    target_sources(briefutil PRIVATE briefutil.rc)
    get_filename_component(_cxx_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_runtime_dll IN ITEMS libc++.dll libunwind.dll)
        if(EXISTS "${_cxx_compiler_dir}/${_runtime_dll}")
            add_custom_command(TARGET briefutil POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_cxx_compiler_dir}/${_runtime_dll}"
                    "$<TARGET_FILE_DIR:briefutil>"
                VERBATIM
            )
        endif()
    endforeach()
    add_custom_command(TARGET briefutil POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:briefutil>
            $<TARGET_FILE_DIR:briefutil>
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
    if(EXISTS "${_windeployqt}")
        add_custom_command(TARGET briefutil POST_BUILD
            COMMAND "${_windeployqt}"
                --dir "$<TARGET_FILE_DIR:briefutil>"
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}"
                --no-translations
                "$<TARGET_FILE:briefutil>"
            VERBATIM
        )
    endif()
endif()

qt_import_qml_plugins(briefutil)
qt_finalize_executable(briefutil)

# ============================================================================
# Renderer smoke test (not installed, development only)
# ============================================================================

add_executable(test_renderer EXCLUDE_FROM_ALL
    test_renderer.cpp
    pdf_renderer_haru.cpp
)

target_include_directories(test_renderer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${libharu_SOURCE_DIR}/include
    ${libharu_BINARY_DIR}/include
)

target_link_libraries(test_renderer PRIVATE hpdf)

if(WIN32)
    foreach(_runtime_dll IN ITEMS libc++.dll libunwind.dll)
        if(EXISTS "${_cxx_compiler_dir}/${_runtime_dll}")
            add_custom_command(TARGET test_renderer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_cxx_compiler_dir}/${_runtime_dll}"
                    "$<TARGET_FILE_DIR:test_renderer>"
                VERBATIM
            )
        endif()
    endforeach()
endif()

# ============================================================================
# Letter builder test (not installed, development only)
# ============================================================================

add_executable(test_letter_builder EXCLUDE_FROM_ALL
    test_letter_builder.cpp
    pdf_renderer_haru.cpp
    sender_profile.cpp
    letter_builder.cpp
)

target_include_directories(test_letter_builder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${libharu_SOURCE_DIR}/include
    ${libharu_BINARY_DIR}/include
)

target_link_libraries(test_letter_builder PRIVATE
    hpdf
    Qt6::Core
)

if(WIN32)
    foreach(_runtime_dll IN ITEMS libc++.dll libunwind.dll)
        if(EXISTS "${_cxx_compiler_dir}/${_runtime_dll}")
            add_custom_command(TARGET test_letter_builder POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_cxx_compiler_dir}/${_runtime_dll}"
                    "$<TARGET_FILE_DIR:test_letter_builder>"
                VERBATIM
            )
        endif()
    endforeach()
    add_custom_command(TARGET test_letter_builder POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:test_letter_builder>
            $<TARGET_FILE_DIR:test_letter_builder>
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endif()

# ============================================================================
# Install
# ============================================================================

install(TARGETS briefutil
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)

if (WIN32)
    qt_generate_deploy_app_script(
        TARGET briefutil
        OUTPUT_SCRIPT briefutil_deploy_qt
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${briefutil_deploy_qt})
endif()

set(CPACK_PACKAGE_NAME "briefutil")
set(CPACK_PACKAGE_VENDOR "briefutil")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Utility for generating PDF letters with Qt Quick.")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})

if (WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "briefutil")
    set(CPACK_NSIS_CONTACT "noreply@example.com")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
endif()

include(CPack)
