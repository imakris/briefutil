qt_add_executable(briefutil
    WIN32
    MANUAL_FINALIZATION
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
)

if(WIN32)
    target_link_libraries(briefutil PRIVATE dwmapi)
    target_sources(briefutil PRIVATE resources/briefutil.rc)
    foreach(_runtime_dll IN ITEMS libc++.dll libunwind.dll)
        if(EXISTS "${BRIEFUTIL_CXX_COMPILER_DIR}/${_runtime_dll}")
            add_custom_command(TARGET briefutil POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${BRIEFUTIL_CXX_COMPILER_DIR}/${_runtime_dll}"
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
