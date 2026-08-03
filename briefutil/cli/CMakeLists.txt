add_executable(briefutil_cli
    main.cpp
)

target_link_libraries(briefutil_cli
    PRIVATE
        briefutil_core
        Qt6::Core
)

briefutil_stage_runtime_dlls(briefutil_cli)
mark2haru_stage_bundled_fonts(briefutil_cli)

install(TARGETS briefutil_cli
    RUNTIME DESTINATION bin
)

if(WIN32)
    qt_generate_deploy_app_script(
        TARGET briefutil_cli
        OUTPUT_SCRIPT briefutil_cli_deploy_qt
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${briefutil_cli_deploy_qt})
endif()
