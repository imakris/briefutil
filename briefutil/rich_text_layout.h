#pragma once

#include "body_content_model.h"
#include "document_model.h"
#include "typography_config.h"
#include <string>
#include <vector>


// ============================================================================
// Rich text layout engine
//
// Converts parsed body blocks into positioned Page_elements, handling
// mixed-style inline runs, headings, lists, images, and pagination.
// ============================================================================

struct Layout_params
{
    float       left_mm;
    float       width_mm;
    color_t     body_color     = { 0, 0, 0 };
    std::string profile_dir;
    Typography_config typo;
    Font_family_config fonts = default_font_family();
};

struct Layout_result
{
    // Positioned elements for each page's body area.
    // Each inner vector holds the elements for one page.
    std::vector<std::vector<Page_element>> pages;

    // Total height consumed on the last page (mm from that page's body top)
    float last_page_used_mm = 0;

    // Non-empty if layout failed (e.g. table too wide). Caller should
    // abort PDF generation and report this to the user.
    std::string error;
};

Layout_result layout_body(const std::vector<Body_block>& blocks,
                          const Layout_params& params,
                          float first_page_top_mm,
                          float first_page_bottom_mm,
                          float cont_page_top_mm,
                          float cont_page_bottom_mm);
