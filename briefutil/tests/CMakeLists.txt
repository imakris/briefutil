function(briefutil_add_test_executable target source_file)
    add_executable(${target} EXCLUDE_FROM_ALL ${source_file})
    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/app/resources
    )
    target_link_libraries(${target} PRIVATE briefutil_core)
    briefutil_copy_runtime_dlls(${target})
endfunction()

briefutil_add_test_executable(test_renderer         test_renderer.cpp)
briefutil_add_test_executable(test_letter_builder   test_letter_builder.cpp)
briefutil_add_test_executable(test_md_to_pdf        test_md_to_pdf.cpp)
briefutil_add_test_executable(test_markdown_parser  test_markdown_parser.cpp)
briefutil_add_test_executable(test_unicode_output_path test_unicode_output_path.cpp)
