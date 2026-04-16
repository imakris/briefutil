set(BRIEFUTIL_MARK2HARU_DIR "" CACHE PATH "Optional path to a mark2haru checkout for the mark2haru backend")
set(BRIEFUTIL_MARK2HARU_FONT_DIR "" CACHE PATH "Optional font directory used by the mark2haru backend")

set(BRIEFUTIL_HAS_MARK2HARU 0)
if(BRIEFUTIL_MARK2HARU_DIR AND EXISTS "${BRIEFUTIL_MARK2HARU_DIR}/CMakeLists.txt")
    set(_briefutil_saved_cxx_standard "${CMAKE_CXX_STANDARD}")
    set(_briefutil_saved_cxx_standard_required "${CMAKE_CXX_STANDARD_REQUIRED}")
    set(_briefutil_saved_cxx_extensions "${CMAKE_CXX_EXTENSIONS}")
    add_subdirectory("${BRIEFUTIL_MARK2HARU_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/mark2haru" EXCLUDE_FROM_ALL)
    set(CMAKE_CXX_STANDARD "${_briefutil_saved_cxx_standard}")
    set(CMAKE_CXX_STANDARD_REQUIRED "${_briefutil_saved_cxx_standard_required}")
    set(CMAKE_CXX_EXTENSIONS "${_briefutil_saved_cxx_extensions}")
    unset(_briefutil_saved_cxx_standard)
    unset(_briefutil_saved_cxx_standard_required)
    unset(_briefutil_saved_cxx_extensions)
    if(TARGET mark2haru::mark2haru)
        set(BRIEFUTIL_HAS_MARK2HARU 1)
        if(BRIEFUTIL_MARK2HARU_FONT_DIR STREQUAL "")
            set(BRIEFUTIL_MARK2HARU_FONT_DIR "${BRIEFUTIL_MARK2HARU_DIR}/fonts")
        endif()
    else()
        message(FATAL_ERROR
            "BRIEFUTIL_MARK2HARU_DIR was provided, but mark2haru::mark2haru was not defined by the checkout.")
    endif()
endif()

add_library(briefutil_core STATIC
    src/letter_builder.cpp
    src/markdown_parser.cpp
    src/pdf_haru_support.cpp
    src/pdf_measurement.cpp
    src/pdf_renderer.cpp
    src/pdf_renderer_haru.cpp
    src/pdf_renderer_mark2haru.cpp
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

target_compile_definitions(briefutil_core
    PUBLIC
        BRIEFUTIL_HAS_MARK2HARU=${BRIEFUTIL_HAS_MARK2HARU}
)

if(BRIEFUTIL_HAS_MARK2HARU)
    file(TO_CMAKE_PATH "${BRIEFUTIL_MARK2HARU_FONT_DIR}" BRIEFUTIL_MARK2HARU_FONT_DIR_CMAKE)
    target_compile_definitions(briefutil_core
        PUBLIC
            BRIEFUTIL_MARK2HARU_FONT_DIR="${BRIEFUTIL_MARK2HARU_FONT_DIR_CMAKE}"
    )
    target_link_libraries(briefutil_core PRIVATE mark2haru::mark2haru)
endif()
