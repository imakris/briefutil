#pragma once

#include "briefutil/document_model.h"
#include "briefutil/typography_config.h"
#include <string>


// ============================================================================
// PDF renderer — libHaru backend
//
// Renders a Document to a PDF file. Stateless: each call to render() is
// independent. All coordinate conversion (mm top-left -> pt bottom-left)
// happens internally.
// ============================================================================

Render_result render_pdf(const Document& doc, const std::string& output_path,
                         const Font_family_config& fonts = default_font_family());


// ============================================================================
// Text measurement — exposed for the letter builder's pagination logic
// ============================================================================

struct text_metrics_t
{
    float width_pt;
    float height_pt;      // total height including all wrapped lines
    int   line_count;
};

// Measure how a text block would render (wrapping, line count, dimensions).
// Does not draw anything. Uses Helvetica / Helvetica-Bold at the given size.
text_metrics_t measure_text(const std::string& text, Font_id font,
                          float size_pt, float leading_pt,
                          float max_width_mm, bool wrap,
                          const Font_family_config& fonts = default_font_family());

// Wrap text into lines that fit within max_width_mm at the given font/size.
std::vector<std::string> wrap_text(const std::string& text, Font_id font,
                                   float size_pt, float max_width_mm,
                                   const Font_family_config& fonts = default_font_family());


// ============================================================================
// Image measurement
// ============================================================================

struct image_dimensions_t
{
    float width_px  = 0;
    float height_px = 0;
    bool  valid     = false;
};

// Read the pixel dimensions of a PNG file without rendering it.
image_dimensions_t measure_png(const std::string& path);
