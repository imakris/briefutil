#pragma once

#include "briefutil/document_model.h"
#include "briefutil/localization.h"
#include "briefutil/typography_config.h"

#include <mark2haru/markdown.h>

#include <string>
#include <vector>


// ============================================================================
// Rich text layout engine
//
// Converts parsed body blocks into positioned Page_element values, handling
// mixed-style inline runs, headings, lists, images, and pagination.
// ============================================================================

struct Layout_params
{
    float                                  left_mm;
    float                                  width_mm;
    color_t                                body_color        = { 0, 0, 0 };
    std::string                            profile_dir;
    typography_config_t                    typo;
    Font_family_config                     fonts             = default_font_family();
    Localization                           loc;
};

struct Layout_result
{
    // Positioned elements for each page's body area.
    // Each inner vector holds the elements for one page.
    std::vector<std::vector<Page_element>> pages;

    // Y position where layout ended on the last page, in mm from the page top.
    // This is an absolute page coordinate, not a height relative to the body.
    float                                  last_page_used_mm = 0;

    // Non-empty if layout failed (e.g. table too wide). Caller should
    // abort PDF generation and report this to the user.
    std::string                            error;
};

Layout_result layout_body(
    const std::vector<mark2haru::Block>&   blocks,
    const Layout_params&                   params,
    float                                  first_page_top_mm,
    float                                  first_page_bottom_mm,
    float                                  cont_page_top_mm,
    float                                  cont_page_bottom_mm);
