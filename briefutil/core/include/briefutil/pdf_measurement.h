#pragma once

#include "briefutil/document_model.h"
#include "briefutil/pdf_backend.h"
#include "briefutil/typography_config.h"

#include <string>
#include <vector>


// ============================================================================
// Backend-neutral measurement utilities
// ============================================================================

struct text_metrics_t
{
    float width_pt = 0;
    float height_pt = 0;   // total height including all wrapped lines
    int   line_count = 0;
};

struct image_dimensions_t
{
    float width_px  = 0;
    float height_px = 0;
    bool  valid     = false;
};

bool pdf_measurement_ready(
    Pdf_backend backend,
    const font_family_config_t& fonts = default_font_family(),
    std::string* detail = nullptr);

text_metrics_t measure_text(
    Pdf_backend backend,
    const std::string& text,
    Font_id font,
    float size_pt,
    float leading_pt,
    float max_width_mm,
    bool wrap,
    const font_family_config_t& fonts = default_font_family());

std::vector<std::string> wrap_text(
    Pdf_backend backend,
    const std::string& text,
    Font_id font,
    float size_pt,
    float max_width_mm,
    const font_family_config_t& fonts = default_font_family());

image_dimensions_t measure_png(const std::string& path);
