add_library(briefutil_core STATIC
    src/brief_service.cpp
    src/letter_builder.cpp
    src/path_utils.cpp
    src/pdf_measurement.cpp
    src/pdf_renderer_mark2haru.cpp
    src/rich_text_layout.cpp
    src/sender_profile.cpp
    src/template_store.cpp
)

add_library(briefutil::core ALIAS briefutil_core)

target_include_directories(briefutil_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/embedded
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(briefutil_core
    PUBLIC
        Qt6::Core
    PRIVATE
        mark2haru::mark2haru
)
