#include "letter_builder.h"
#include "pdf_renderer_haru.h"
#include "markdown_parser.h"
#include "rich_text_layout.h"

#include <cmath>
#include <utility>


// ============================================================================
// Layout constants for the native DIN 5008 letter layout.
// ============================================================================

// DIN 5008 Form B page geometry
static constexpr float k_margin_left_mm   = 25.0f;
static constexpr float k_margin_right_mm  = 20.0f;

// DIN 5008 Form B info block
static constexpr float k_sender_x_mm     = 125.0f;
static constexpr float k_sender_y_mm     = 50.0f;
static constexpr float k_sender_w_mm     = 65.0f;

// DIN 5008 Form B address field
static constexpr float k_address_text_x_mm  = 25.0f;
static constexpr float k_address_text_w_mm  = 80.0f;
static constexpr float k_return_x_mm        = k_address_text_x_mm;
static constexpr float k_return_y_mm        = 59.2f;
static constexpr float k_return_rule_y_mm   = 62.3f;

// Recipient block
static constexpr float k_recip_x_mm      = k_address_text_x_mm;
static constexpr float k_recip_y_mm      = 63.5f;
static constexpr float k_recip_w_mm      = k_address_text_w_mm;

// Date inside the DIN 5008 info block
static constexpr float k_date_x_mm       = k_sender_x_mm;
static constexpr float k_date_y_mm       = 84.0f;
static constexpr float k_info_block_min_h_mm = 40.0f;

// DIN 5008 content area: subject begins below the Form B address field/info block
static constexpr float k_subject_gap_mm  = 14.0f;
static constexpr float k_subject_to_body_mm = 12.69f;

// Body text area width
static constexpr float k_page_width_mm   = 210.0f;
static constexpr float k_page_height_mm  = 297.0f;
static constexpr float k_body_width_mm   = k_page_width_mm - k_margin_left_mm - k_margin_right_mm;

// Closing
static constexpr float k_closing_skip_baselines = 2.0f;

// Signature image
static constexpr float k_sig_width_mm    = 1.9f * 25.4f;     // 48.26

// Fold marks (from top of page)
static constexpr float k_fold1_mm        = 105.0f;
static constexpr float k_fold2_mm        = 210.0f;
static constexpr float k_punch_mm        = 148.5f;
static constexpr float k_mark_x_mm       = 3.0f;
static constexpr float k_fold_len_mm     = 5.0f;
static constexpr float k_punch_len_mm    = 8.0f;

// Font sizes
static constexpr float k_sender_size_pt  = 10.0f;
static constexpr float k_sender_lead_pt  = 12.0f;
static constexpr float k_return_size_pt  = 8.0f;
static constexpr float k_recip_size_pt   = 10.0f;
static constexpr float k_recip_lead_pt   = 12.0f;
static constexpr float k_date_size_pt    = 10.0f;
static constexpr float k_body_size_pt    = 10.0f;
static constexpr float k_body_lead_pt    = 12.0f;
static constexpr float k_footer_size_pt  = 9.0f;

// Colors
static constexpr Color k_black           = { 0, 0, 0 };
static constexpr Color k_footer_color    = { 9/255.0f, 92/255.0f, 105/255.0f };

// Commercial-specific layout
static constexpr float k_company_x_mm    = k_sender_x_mm;
static constexpr float k_company_y_mm    = 33.0f;
static constexpr float k_company_w_mm    = k_sender_w_mm;
static constexpr float k_company_size_pt = 24.0f;
static constexpr float k_top_rule_y_mm   = 45.0f;
static constexpr float k_top_rule_x1_mm  = 0.0f;
static constexpr float k_top_rule_width_pt = 3.0f;
static constexpr float k_footer_text_size_pt = 8.0f;

// Footer position
static constexpr float k_footer_y_mm     = k_page_height_mm - 18.0f;

// Continuation page top margin
static constexpr float k_cont_top_mm     = 25.0f;

static constexpr float k_pts_per_mm      = 72.0f / 25.4f;


// ============================================================================
// Helpers
// ============================================================================

static float pt_to_mm(float pt) { return pt / k_pts_per_mm; }

static int fit_line_count(float available_height_mm, float line_height_mm)
{
    if (available_height_mm <= 0.0f) {
        return 1;
    }

    return std::max(1, (int)std::floor(available_height_mm / line_height_mm));
}

static void add_fold_marks(Page& page)
{
    page.elements.push_back(Line_segment{
        k_mark_x_mm, k_fold1_mm, k_mark_x_mm + k_fold_len_mm, k_fold1_mm, 0.5f, k_black
    });
    page.elements.push_back(Line_segment{
        k_mark_x_mm, k_fold2_mm, k_mark_x_mm + k_fold_len_mm, k_fold2_mm, 0.5f, k_black
    });
    page.elements.push_back(Line_segment{
        k_mark_x_mm, k_punch_mm, k_mark_x_mm + k_punch_len_mm, k_punch_mm, 0.5f, k_black
    });
}

// Build the sender block text from profile lines + email.
static std::string build_sender_text(const Sender_profile& profile)
{
    std::string text;
    for (const auto& line : profile.sender_lines) {
        if (!text.empty()) text += '\n';
        text += line;
    }
    if (!profile.email.empty()) {
        text += "\n\n";
        text += profile.email;
    }
    return text;
}


// ============================================================================
// Letter builder
// ============================================================================

Build_letter_result build_letter(const Sender_profile& profile,
                                 const Letter_input& input,
                                 const std::string& profile_dir)
{
    Document doc;
    doc.page_width_mm  = k_page_width_mm;
    doc.page_height_mm = k_page_height_mm;

    auto sender_text = build_sender_text(profile);

    auto ret_metrics = measure_text(profile.return_address_line,
                                    Font_id::sans,
                                    k_return_size_pt,
                                    0,
                                    k_address_text_w_mm,
                                    false);
    float return_rule_x2_mm = k_return_x_mm + pt_to_mm(ret_metrics.width_pt);

    auto sender_metrics = measure_text(sender_text,
                                       Font_id::sans,
                                       k_sender_size_pt,
                                       k_sender_lead_pt,
                                       k_sender_w_mm,
                                       false);
    auto date_metrics = measure_text(input.date,
                                     Font_id::sans,
                                     k_date_size_pt,
                                     0,
                                     k_sender_w_mm,
                                     false);

    float sender_bottom_mm = k_sender_y_mm + pt_to_mm(sender_metrics.height_pt);
    float date_bottom_mm = k_date_y_mm + pt_to_mm(date_metrics.height_pt);
    float info_block_bottom_mm = std::max(
        k_sender_y_mm + k_info_block_min_h_mm,
        std::max(sender_bottom_mm, date_bottom_mm));

    bool has_subject = !input.subject.empty();
    float subject_y_mm = info_block_bottom_mm + k_subject_gap_mm;
    float body_y_mm = has_subject ? (subject_y_mm + k_subject_to_body_mm)
                                  : subject_y_mm;

    // Closing block height estimate
    // UTF-8: ü = \xc3\xbc, ß = \xc3\x9f
    std::string closing_text = "Mit freundlichen Gr" "\xc3\xbc" "\xc3" "\x9f" "en";
    float closing_height_mm = pt_to_mm(k_closing_skip_baselines * k_body_lead_pt)
        + pt_to_mm(k_body_lead_pt);

    float sig_height_mm = 0;
    std::string sig_path;
    if (!profile.signature_image.empty()) {
        sig_path = profile_dir + "/" + profile.signature_image;
        sig_height_mm = k_sig_width_mm * 0.4f;
    }
    float signer_height_mm = pt_to_mm(k_body_lead_pt);
    if (!profile.signer_title.empty())
        signer_height_mm += pt_to_mm(k_body_lead_pt);
    float total_closing_mm = closing_height_mm + sig_height_mm + signer_height_mm + 5.0f;

    // Parse and lay out the body using the markdown-aware layout engine
    auto body_blocks = parse_markdown(input.body);

    Layout_params lp;
    lp.left_mm      = k_margin_left_mm;
    lp.width_mm     = k_body_width_mm;
    lp.body_size_pt = k_body_size_pt;
    lp.body_lead_pt = k_body_lead_pt;
    lp.body_color   = k_black;
    lp.profile_dir  = profile_dir;

    float page_bottom = k_footer_y_mm - 5.0f;

    auto body_layout = layout_body(body_blocks, lp,
                                   body_y_mm, page_bottom,
                                   k_cont_top_mm, page_bottom);

    if (!body_layout.error.empty()) {
        return { {}, body_layout.error };
    }

    // If closing doesn't fit on the last page, add a continuation page for it
    if (body_layout.last_page_used_mm + total_closing_mm > page_bottom) {
        body_layout.pages.push_back({});
        body_layout.last_page_used_mm = k_cont_top_mm;
    }

    int total_pages = (int)body_layout.pages.size();

    // Build pages
    for (int pi = 0; pi < total_pages; pi++) {
        Page page;
        add_fold_marks(page);

        if (pi == 0) {
            // -- First page: header elements --
            bool commercial = (profile.style == Profile_style::commercial);

            if (commercial && !profile.company_name.empty()) {
                auto company_metrics = measure_text(profile.company_name,
                                                    Font_id::sans_bold,
                                                    k_company_size_pt,
                                                    0,
                                                    k_company_w_mm,
                                                    false);
                float company_right_mm = k_company_x_mm + pt_to_mm(company_metrics.width_pt);

                page.elements.push_back(Text_block{
                    k_company_x_mm, k_company_y_mm, k_company_w_mm,
                    profile.company_name,
                    Font_id::sans_bold, k_company_size_pt, 0,
                    profile.company_name_color, false
                });

                page.elements.push_back(Line_segment{
                    k_top_rule_x1_mm, k_top_rule_y_mm,
                    company_right_mm, k_top_rule_y_mm,
                    k_top_rule_width_pt, profile.top_rule_color
                });
            }

            page.elements.push_back(Text_block{
                k_sender_x_mm, k_sender_y_mm, k_sender_w_mm,
                sender_text,
                Font_id::sans, k_sender_size_pt, k_sender_lead_pt,
                k_black, false
            });

            page.elements.push_back(Text_block{
                k_return_x_mm, k_return_y_mm, k_address_text_w_mm,
                profile.return_address_line,
                Font_id::sans, k_return_size_pt, 0,
                k_footer_color, false
            });

            page.elements.push_back(Line_segment{
                k_return_x_mm, k_return_rule_y_mm,
                return_rule_x2_mm, k_return_rule_y_mm,
                0.5f, k_black
            });

            page.elements.push_back(Text_block{
                k_recip_x_mm, k_recip_y_mm, k_recip_w_mm,
                input.recipient,
                Font_id::sans, k_recip_size_pt, k_recip_lead_pt,
                k_black, false
            });

            page.elements.push_back(Text_block{
                k_date_x_mm, k_date_y_mm, k_sender_w_mm,
                input.date,
                Font_id::sans, k_date_size_pt, 0,
                k_black, false
            });

            if (has_subject) {
                page.elements.push_back(Text_block{
                    k_margin_left_mm, subject_y_mm, k_body_width_mm,
                    input.subject,
                    Font_id::sans_bold, k_body_size_pt, k_body_lead_pt,
                    k_black, false
                });
            }
        }

        // -- Body elements from the layout engine --
        for (auto& elem : body_layout.pages[pi]) {
            page.elements.push_back(std::move(elem));
        }

        // -- Closing (on last page) --
        bool is_last_page = (pi == total_pages - 1);
        if (is_last_page) {
            float closing_y = body_layout.last_page_used_mm
                + pt_to_mm(k_closing_skip_baselines * k_body_lead_pt);

            page.elements.push_back(Text_block{
                k_margin_left_mm, closing_y, k_body_width_mm,
                closing_text,
                Font_id::sans, k_body_size_pt, k_body_lead_pt,
                k_black, false
            });

            float after_closing = closing_y + pt_to_mm(k_body_lead_pt) + 2.0f;

            if (!sig_path.empty()) {
                page.elements.push_back(Image_block{
                    k_margin_left_mm, after_closing, k_sig_width_mm,
                    sig_path
                });
                after_closing += sig_height_mm + 2.0f;
            }

            page.elements.push_back(Text_block{
                k_margin_left_mm, after_closing, k_body_width_mm,
                profile.signer_name,
                Font_id::sans, k_body_size_pt, k_body_lead_pt,
                k_black, false
            });

            if (!profile.signer_title.empty()) {
                after_closing += pt_to_mm(k_body_lead_pt);
                page.elements.push_back(Text_block{
                    k_margin_left_mm, after_closing, k_body_width_mm,
                    profile.signer_title,
                    Font_id::sans, k_body_size_pt, k_body_lead_pt,
                    k_black, false
                });
            }
        }

        // -- Footer --
        float footer_y = k_footer_y_mm;

        // Page number (only on multi-page)
        if (total_pages > 1) {
            std::string page_num = "Seite " + std::to_string(pi + 1)
                + " von " + std::to_string(total_pages);
            auto page_num_metrics = measure_text(page_num, Font_id::sans,
                                                 k_footer_size_pt, 0, 200, false);
            float page_num_x = k_page_width_mm - k_margin_right_mm
                - pt_to_mm(page_num_metrics.width_pt);
            page.elements.push_back(Text_block{
                page_num_x, footer_y, k_body_width_mm,
                page_num,
                Font_id::sans, k_footer_size_pt, 0,
                k_black, false
            });
            footer_y += pt_to_mm(k_footer_size_pt) + 3.0f;
        }

        // Commercial footer lines (on every page)
        if (profile.style == Profile_style::commercial) {
            for (const auto& fl : profile.footer_lines) {
                page.elements.push_back(Text_block{
                    k_margin_left_mm, footer_y, k_body_width_mm,
                    fl,
                    Font_id::sans, k_footer_text_size_pt,
                    k_footer_text_size_pt,
                    k_footer_color, false
                });
                footer_y += pt_to_mm(k_footer_text_size_pt) + 1.0f;
            }
        }

        doc.pages.push_back(std::move(page));
    }

    return { std::move(doc), "" };
}
