# Test executables are part of the regular build so `ctest` can run a freshly
# built tree without a separate build target.
include(CTest)

function(briefutil_add_test_executable target source_file)
    add_executable(${target} ${source_file})
    target_link_libraries(${target} PRIVATE briefutil_core)
    briefutil_copy_runtime_dlls(${target})
    briefutil_copy_mark2haru_fonts(${target})
endfunction()

briefutil_add_test_executable(test_renderer            test_renderer.cpp)
briefutil_add_test_executable(test_letter_builder      test_letter_builder.cpp)
briefutil_add_test_executable(test_brief_service       test_brief_service.cpp)
briefutil_add_test_executable(test_cli                 test_cli.cpp)
briefutil_add_test_executable(test_cli_output          test_cli_output.cpp)
briefutil_add_test_executable(test_md_to_pdf           test_md_to_pdf.cpp)
briefutil_add_test_executable(test_path_utils          test_path_utils.cpp)
briefutil_add_test_executable(test_sender_profile_schema test_sender_profile_schema.cpp)
briefutil_add_test_executable(test_template_store      test_template_store.cpp)
briefutil_add_test_executable(test_unicode_output_path test_unicode_output_path.cpp)

target_compile_definitions(test_cli PRIVATE
    BRIEFUTIL_CLI_PATH="$<TARGET_FILE:briefutil_cli>"
)
add_dependencies(test_cli briefutil_cli)

target_include_directories(test_cli_output PRIVATE
    ${CMAKE_SOURCE_DIR}/app
)
target_sources(test_cli_output PRIVATE
    ${CMAKE_SOURCE_DIR}/app/cli_output.cpp
)

# Register tests with CTest. Most tests take an optional output path; for
# test_md_to_pdf we pass a bundled sample markdown file.
set(_briefutil_test_data_dir "${CMAKE_CURRENT_SOURCE_DIR}/data")

add_test(NAME test_renderer            COMMAND test_renderer)
add_test(NAME test_letter_builder      COMMAND test_letter_builder)
add_test(NAME test_brief_service       COMMAND test_brief_service)
add_test(NAME test_cli                 COMMAND test_cli)
add_test(NAME test_cli_output          COMMAND test_cli_output)
add_test(NAME test_path_utils          COMMAND test_path_utils)
add_test(NAME test_sender_profile_schema
         COMMAND test_sender_profile_schema)
add_test(NAME test_template_store      COMMAND test_template_store)
add_test(NAME test_unicode_output_path COMMAND test_unicode_output_path)
add_test(NAME test_md_to_pdf
         COMMAND test_md_to_pdf "${_briefutil_test_data_dir}/test_sample.md")

# Convenience target that depends on every test executable.
add_custom_target(check
    DEPENDS
        test_renderer
        test_letter_builder
        test_brief_service
        test_cli
        test_cli_output
        test_md_to_pdf
        test_path_utils
        test_sender_profile_schema
        test_template_store
        test_unicode_output_path
)
