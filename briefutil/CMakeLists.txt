cmake_minimum_required(VERSION 3.16)

project(briefutil VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (WIN32)
    option(BRIEFUTIL_BUNDLE_MIKTEX "Download and package a portable MiKTeX runtime." ON)
    if (BRIEFUTIL_BUNDLE_MIKTEX)
        set(MIKTEX_SETUP_VERSION "5.5.0+1763023")
        set(MIKTEX_PACKAGE_SET "basic")
        set(MIKTEX_SETUP_ARCHIVE "miktexsetup-${MIKTEX_SETUP_VERSION}-x64.zip")
        set(MIKTEX_SETUP_URL "https://mirror.ctan.org/systems/win32/miktex/setup/windows-x64/${MIKTEX_SETUP_ARCHIVE}")
        set(MIKTEX_STAGING_DIR "${CMAKE_BINARY_DIR}/miktex")
        set(MIKTEX_SETUP_ZIP "${MIKTEX_STAGING_DIR}/${MIKTEX_SETUP_ARCHIVE}")
        set(MIKTEX_SETUP_EXE "${MIKTEX_STAGING_DIR}/miktexsetup_standalone.exe")
        set(MIKTEX_PORTABLE_DIR "${MIKTEX_STAGING_DIR}/portable")
        set(MIKTEX_PACKAGE_REPOSITORY "${MIKTEX_STAGING_DIR}/package-repo")
        set(MIKTEX_PORTABLE_STAMP "${MIKTEX_STAGING_DIR}/miktex-portable.stamp")
        file(MAKE_DIRECTORY "${MIKTEX_STAGING_DIR}")
        set(_briefutil_need_miktex_download TRUE)
        if (EXISTS "${MIKTEX_SETUP_ZIP}")
            file(SIZE "${MIKTEX_SETUP_ZIP}" _briefutil_miktex_zip_size)
            if (_briefutil_miktex_zip_size GREATER 0)
                set(_briefutil_need_miktex_download FALSE)
            else()
                message(WARNING "Existing MiKTeX archive is empty, forcing re-download.")
            endif()
        endif()
        if (_briefutil_need_miktex_download)
            message(STATUS "Downloading MiKTeX setup from ${MIKTEX_SETUP_URL}")
            file(DOWNLOAD "${MIKTEX_SETUP_URL}" "${MIKTEX_SETUP_ZIP}" SHOW_PROGRESS)
        endif()
        unset(_briefutil_miktex_zip_size)
        unset(_briefutil_need_miktex_download)
        add_custom_command(
            OUTPUT "${MIKTEX_SETUP_EXE}"
            COMMAND powershell -NoLogo -NoProfile -Command "Expand-Archive -LiteralPath \"${MIKTEX_SETUP_ZIP}\" -DestinationPath \"${MIKTEX_STAGING_DIR}\" -Force"
            DEPENDS "${MIKTEX_SETUP_ZIP}"
            COMMENT "Extracting MiKTeX setup"
            VERBATIM
        )
        set(MIKTEX_REMOTE_REPOSITORY "https://ctan.net/systems/win32/miktex/tm/packages/")
        file(TO_NATIVE_PATH "${MIKTEX_SETUP_EXE}" MIKTEX_SETUP_EXE_NATIVE)
        file(TO_NATIVE_PATH "${MIKTEX_PACKAGE_REPOSITORY}" MIKTEX_PACKAGE_REPOSITORY_NATIVE)
        file(TO_NATIVE_PATH "${MIKTEX_PORTABLE_DIR}" MIKTEX_PORTABLE_DIR_NATIVE)
        file(TO_NATIVE_PATH "${MIKTEX_PORTABLE_STAMP}" MIKTEX_PORTABLE_STAMP_NATIVE)
        file(TO_NATIVE_PATH "${CMAKE_COMMAND}" CMAKE_COMMAND_NATIVE)
        set(MIKTEX_BOOTSTRAP_SCRIPT "${CMAKE_BINARY_DIR}/bootstrap_miktex.cmd")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bootstrap_miktex.cmd.in"
            "${MIKTEX_BOOTSTRAP_SCRIPT}"
            @ONLY
        )
        add_custom_command(
            OUTPUT "${MIKTEX_PORTABLE_STAMP}"
            COMMAND cmd /c "${MIKTEX_BOOTSTRAP_SCRIPT}"
            DEPENDS "${MIKTEX_SETUP_EXE}"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Bootstrapping portable MiKTeX distribution (this can take a while)"
            VERBATIM
            USES_TERMINAL
        )
        add_custom_target(portable_miktex DEPENDS "${MIKTEX_PORTABLE_STAMP}")
    endif()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Qml Quick)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Qml Quick)

set(PROJECT_SOURCES
    main.cpp
    proxy.cpp
    qml.qrc
)

if (QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(briefutil
        WIN32
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    add_executable(briefutil
        WIN32
        ${PROJECT_SOURCES}
    )
endif()

target_include_directories(briefutil PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(briefutil
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Qml
        Qt${QT_VERSION_MAJOR}::Quick
)

if (WIN32)
    target_sources(briefutil PRIVATE briefutil.rc)
endif()

if (QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_import_qml_plugins(briefutil)
    qt_finalize_executable(briefutil)
endif()

install(TARGETS briefutil
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)

if (WIN32 AND BRIEFUTIL_BUNDLE_MIKTEX)
    install(DIRECTORY "${MIKTEX_PORTABLE_DIR}/" DESTINATION miktex)
    install(CODE
        "if(NOT EXISTS \"${MIKTEX_PORTABLE_DIR}/texmfs/install/miktex/bin/x64/texify.exe\")
             message(FATAL_ERROR \"Portable MiKTeX was not prepared. Run 'cmake --build ${CMAKE_BINARY_DIR} --target portable_miktex --config \${CMAKE_INSTALL_CONFIG_NAME}' before installing.\")
         endif()")
endif()

if (WIN32)
    if (QT_VERSION_MAJOR EQUAL 6)
        qt_generate_deploy_app_script(
            TARGET briefutil
            OUTPUT_SCRIPT briefutil_deploy_qt
            NO_UNSUPPORTED_PLATFORM_ERROR
        )
        install(SCRIPT ${briefutil_deploy_qt})
    endif()
endif()

set(CPACK_PACKAGE_NAME "briefutil")
set(CPACK_PACKAGE_VENDOR "briefutil")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Utility for generating PDF letters via Qt Quick and MiKTeX.")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})

if (WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "briefutil")
    set(CPACK_NSIS_CONTACT "noreply@example.com")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
endif()

include(CPack)
