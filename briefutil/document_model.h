#pragma once

#include <string>
#include <vector>
#include <variant>


// ============================================================================
// Document model — page-element types for native PDF rendering
//
// All coordinates are in mm from the top-left corner of the page.
// The renderer converts to libHaru's bottom-left point system internally.
// ============================================================================

struct Color
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

enum class Font_id
{
    sans,
    sans_bold,
    sans_italic,
    sans_bold_italic,
    mono,
};

struct Text_block
{
    float       x_mm;
    float       y_mm;
    float       width_mm;
    std::string text;
    Font_id     font    = Font_id::sans;
    float       size_pt = 10.0f;
    float       leading_pt = 0.0f;   // 0 = use size_pt
    Color       color;
    bool        wrap = false;         // greedy word-wrap to width_mm
};

struct Line_segment
{
    float x1_mm;
    float y1_mm;
    float x2_mm;
    float y2_mm;
    float stroke_width_pt = 0.5f;
    Color color;
};

struct Image_block
{
    float       x_mm;
    float       y_mm;
    float       width_mm;
    std::string path;
};

struct Text_span
{
    float       x_mm;
    float       y_mm;
    std::string text;
    Font_id     font    = Font_id::sans;
    float       size_pt = 10.0f;
    Color       color;
};

struct Filled_rect
{
    float x_mm;
    float y_mm;
    float width_mm;
    float height_mm;
    Color color;
};

using Page_element = std::variant<Text_block, Line_segment, Image_block, Text_span, Filled_rect>;

struct Page
{
    std::vector<Page_element> elements;
};

struct Document
{
    float page_width_mm  = 210.0f;   // A4
    float page_height_mm = 297.0f;
    std::vector<Page> pages;
};


// ============================================================================
// Render result
// ============================================================================

struct Render_result
{
    bool        ok = false;
    std::string output_path;
    std::string message;        // user-facing
    std::string detail;         // developer-facing
};
