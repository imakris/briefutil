add_library(briefutil_core STATIC
    src/letter_builder.cpp
    src/markdown_parser.cpp
    src/pdf_renderer_haru.cpp
    src/rich_text_layout.cpp
    src/sender_profile.cpp
)

add_library(briefutil::core ALIAS briefutil_core)

target_include_directories(briefutil_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${libharu_SOURCE_DIR}/include
        ${libharu_BINARY_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(briefutil_core
    PUBLIC
        hpdf
        Qt6::Core
)
