#pragma once

#include <string>
#include <vector>
#include <variant>


// ============================================================================
// Document model - page-element types for native PDF rendering
//
// All coordinates are in mm from the top-left corner of the page.
// The renderer converts to libHaru's bottom-left point system internally.
// ============================================================================

// -- Unit conversion utilities (mm ↔ pt) --

inline constexpr float k_pts_per_mm = 72.0f / 25.4f;

inline float mm_to_pt(float mm) { return mm * k_pts_per_mm; }
inline float pt_to_mm(float pt) { return pt / k_pts_per_mm; }

// Split text on newline boundaries, preserving empty segments.
inline std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            lines.push_back(text.substr(pos));
            break;
        }
        lines.push_back(text.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return lines;
}

// Invoke `fn(word)` for each space-separated, non-empty token in `s`.
// Used by every word-wrap implementation in the codebase.
template <typename F>
inline void for_each_word(const std::string& s, F&& fn)
{
    size_t pos = 0;
    while (pos < s.size()) {
        while (pos < s.size() && s[pos] == ' ') pos++;
        if (pos >= s.size()) break;
        size_t end = s.find(' ', pos);
        if (end == std::string::npos) end = s.size();
        fn(s.substr(pos, end - pos));
        pos = end;
    }
}


struct color_t
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

enum class Font_id
{
    SANS,
    SANS_BOLD,
    SANS_ITALIC,
    SANS_BOLD_ITALIC,
    MONO,
};

struct text_block_t
{
    float       x_mm;
    float       y_mm;
    float       width_mm;
    std::string text;
    Font_id     font       = Font_id::SANS;
    float       size_pt    = 10.0f;
    float       leading_pt = 0.0f;   // 0 = use size_pt
    color_t     color;
    bool        wrap = false;         // greedy word-wrap to width_mm
};

struct line_segment_t
{
    float x1_mm;
    float y1_mm;
    float x2_mm;
    float y2_mm;
    float stroke_width_pt = 0.5f;
    color_t color;
};

struct image_block_t
{
    float       x_mm;
    float       y_mm;
    float       width_mm;
    std::string path;
};

struct text_span_t
{
    float       x_mm;
    float       y_mm;
    std::string text;
    Font_id     font    = Font_id::SANS;
    float       size_pt = 10.0f;
    color_t     color;
};

struct filled_rect_t
{
    float x_mm;
    float y_mm;
    float width_mm;
    float height_mm;
    color_t color;
};

using page_element_t = std::variant<text_block_t, line_segment_t, image_block_t, text_span_t, filled_rect_t>;

struct page_t
{
    std::vector<page_element_t> elements;
};

struct document_t
{
    float page_width_mm  = 210.0f;   // A4
    float page_height_mm = 297.0f;
    std::vector<page_t> pages;
};


// ============================================================================
// Render result
// ============================================================================

struct render_result_t
{
    bool        ok = false;
    std::string output_path;
    std::string message;        // user-facing
    std::string detail;         // developer-facing
};
