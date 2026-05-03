qt_add_executable(briefutil
    WIN32
    MANUAL_FINALIZATION
    cli_output.cpp
    main.cpp
    proxy.cpp
    resources/qml.qrc
)

target_include_directories(briefutil PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
)

target_link_libraries(briefutil
    PRIVATE
        briefutil_core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickDialogs2
)

briefutil_copy_mark2haru_fonts(briefutil)

add_custom_target(briefutil_cli_companion
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:briefutil>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:briefutil_cli>"
        "$<TARGET_FILE_DIR:briefutil>/$<TARGET_FILE_NAME:briefutil_cli>"
    DEPENDS briefutil_cli
    VERBATIM
)
add_dependencies(briefutil briefutil_cli_companion)

if(WIN32)
    target_link_libraries(briefutil PRIVATE dwmapi)
    target_sources(briefutil PRIVATE resources/briefutil.rc)
    briefutil_copy_runtime_dlls(briefutil)
    if(EXISTS "${BRIEFUTIL_WINDEPLOYQT}")
        add_custom_command(TARGET briefutil POST_BUILD
            COMMAND "${BRIEFUTIL_WINDEPLOYQT}"
                --dir "$<TARGET_FILE_DIR:briefutil>"
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                --no-translations
                "$<TARGET_FILE:briefutil>"
            VERBATIM
        )
    endif()

    add_executable(briefutil_portable_launcher WIN32 portable_launcher_win.c)
    target_link_libraries(briefutil_portable_launcher PRIVATE shell32)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_options(briefutil_portable_launcher PRIVATE -static -static-libgcc)
    endif()
endif()

qt_import_qml_plugins(briefutil)
qt_finalize_executable(briefutil)

install(TARGETS briefutil
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)

if(WIN32)
    qt_generate_deploy_app_script(
        TARGET briefutil
        OUTPUT_SCRIPT briefutil_deploy_qt
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${briefutil_deploy_qt})
endif()
