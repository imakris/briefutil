#pragma once

#include "briefutil/document_model.h"
#include "briefutil/localization.h"
#include "briefutil/pdf_measurement.h"
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
    // Every measurement the body layout makes, tables included, goes through
    // this one instance, so the whole document is placed against a single set
    // of loaded font metrics. It must be ready(): the caller reports an
    // unloadable font family before it lays anything out.
    const Pdf_measurement&                 measurement;
    float                                  left_mm           = 0;
    float                                  width_mm          = 0;
    color_t                                body_color        = { 0, 0, 0 };
    std::string                            profile_dir;
    typography_config_t                    typo;
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
