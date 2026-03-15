#pragma once

#include "document_model.h"


// ============================================================================
// Letter layout specification — geometry and placement rules
//
// Describes WHERE each part of a letter is placed on the page.
// Content (WHAT is placed) comes from Sender_profile and Letter_input.
// Visual styling (HOW it looks) comes from Theme_config.
// ============================================================================

struct Letter_layout_spec
{
    // Page
    float page_width_mm   = 210.0f;
    float page_height_mm  = 297.0f;
    float margin_left_mm  = 25.0f;
    float margin_right_mm = 20.0f;

    // Sender/info block (right column)
    float sender_x_mm     = 125.0f;
    float sender_y_mm     = 50.0f;
    float sender_w_mm     = 65.0f;

    // Address field (left column)
    float address_text_x_mm = 25.0f;
    float address_text_w_mm = 80.0f;
    float return_y_mm       = 59.2f;
    float return_rule_y_mm  = 62.3f;

    // Recipient block
    float recip_y_mm = 63.5f;

    // Date
    float date_y_mm            = 84.0f;
    float info_block_min_h_mm  = 40.0f;

    // Subject/body start
    float subject_gap_mm      = 14.0f;
    float subject_to_body_mm  = 12.69f;

    // Closing
    float closing_skip_baselines = 2.0f;
    float sig_width_mm           = 48.26f;   // 1.9 * 25.4

    // Footer
    float footer_margin_mm = 18.0f;    // distance from page bottom

    // Continuation pages
    float cont_top_mm = 25.0f;

    // Fold/punch marks
    float fold1_y_mm     = 105.0f;
    float fold2_y_mm     = 210.0f;
    float punch_y_mm     = 148.5f;
    float mark_x_mm      = 3.0f;
    float fold_len_mm    = 5.0f;
    float punch_len_mm   = 8.0f;

    // Commercial decorative elements
    float company_x_mm        = 125.0f;
    float company_y_mm        = 33.0f;
    float company_w_mm        = 65.0f;
    float top_rule_y_mm       = 45.0f;
    float top_rule_x1_mm      = 0.0f;
    float top_rule_width_pt   = 3.0f;

    // Footer color
    color_t footer_color = { 9/255.0f, 92/255.0f, 105/255.0f };
};

inline Letter_layout_spec din_5008_form_b() { return {}; }
