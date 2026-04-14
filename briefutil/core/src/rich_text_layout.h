#pragma once

#include "briefutil/body_content_model.h"
#include "briefutil/document_model.h"
#include "briefutil/localization.h"
#include "briefutil/pdf_backend.h"
#include "briefutil/typography_config.h"
#include <string>
#include <vector>


// ============================================================================
// Rich text layout engine
//
// Converts parsed body blocks into positioned page_element_t values, handling
// mixed-style inline runs, headings, lists, images, and pagination.
// ============================================================================

struct layout_params_t
{
    float       left_mm;
    float       width_mm;
    color_t     body_color     = { 0, 0, 0 };
    std::string profile_dir;
    typography_config_t typo;
    font_family_config_t fonts = default_font_family();
    localization_t loc;
    Pdf_backend pdf_backend = Pdf_backend::Haru;
};

struct layout_result_t
{
    // Positioned elements for each page's body area.
    // Each inner vector holds the elements for one page.
    std::vector<std::vector<page_element_t>> pages;

    // Total height consumed on the last page (mm from that page's body top)
    float last_page_used_mm = 0;

    // Non-empty if layout failed (e.g. table too wide). Caller should
    // abort PDF generation and report this to the user.
    std::string error;
};

layout_result_t layout_body(const std::vector<body_block_t>& blocks,
                          const layout_params_t& params,
                          float first_page_top_mm,
                          float first_page_bottom_mm,
                          float cont_page_top_mm,
                          float cont_page_bottom_mm);
