#pragma once

#include "document_model.h"


// ============================================================================
// Letter layout specification - geometry and placement rules
//
// Describes WHERE each part of a letter is placed on the page.
// Content (WHAT is placed) comes from sender_profile_t and letter_input_t.
// Visual styling (HOW it looks) comes from theme_config_t.
// ============================================================================

struct letter_layout_spec_t
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
    float sig_default_aspect     = 0.4f;     // fallback when PNG can't be read
    float closing_after_pad_mm   = 2.0f;     // gap after closing line
    float signature_after_pad_mm = 2.0f;     // gap after signature image
    float closing_extra_room_mm  = 5.0f;     // safety margin in fit estimate

    // Footer
    float footer_margin_mm    = 18.0f;    // distance from page bottom
    float footer_line_gap_mm  = 3.0f;     // gap between footer lines
    float page_bottom_buffer_mm = 5.0f;   // safety gap above the footer

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
    float company_w_mm        = 65.0f;
    float logo_top_mm         = 5.0f;    // logo top edge must not go above this
    float logo_rule_gap_mm    = 2.0f;    // gap between logo bottom and top rule
    float top_rule_y_mm       = 45.0f;
    float top_rule_x1_mm      = 0.0f;
    float top_rule_width_pt   = 3.0f;

    // Footer color
    color_t footer_color = { 9/255.0f, 92/255.0f, 105/255.0f };
};


// ============================================================================
// Named layouts
//
// din_5008_form_b is the default office letter layout used in Germany when
// the page has a printed letterhead. Form A is for letters without a
// letterhead and pushes the address field higher up the page. us_letter is
// the same layout as form B with North American page dimensions.
// ============================================================================

inline letter_layout_spec_t din_5008_form_b() { return {}; }

inline letter_layout_spec_t din_5008_form_a()
{
    // Form A: address field starts ~18 mm higher than form B (top of address
    // field at ~27 mm vs. ~45 mm). Fold marks shift accordingly so the
    // window envelope still aligns with the recipient block.
    letter_layout_spec_t L;
    L.return_y_mm       = 41.2f;
    L.return_rule_y_mm  = 44.3f;
    L.recip_y_mm        = 45.5f;
    L.sender_y_mm       = 32.0f;
    L.date_y_mm         = 66.0f;
    L.top_rule_y_mm     = 27.0f;
    L.fold1_y_mm        = 87.0f;
    L.fold2_y_mm        = 192.0f;
    return L;
}

inline letter_layout_spec_t us_letter()
{
    letter_layout_spec_t L;
    L.page_width_mm  = 215.9f;   // 8.5 in
    L.page_height_mm = 279.4f;   // 11 in
    return L;
}
