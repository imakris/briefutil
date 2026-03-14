#include "letter_builder.h"
#include "pdf_renderer_haru.h"

#include <cmath>
#include <utility>


// ============================================================================
// Layout constants — extracted from st_simple.tex.cppstring
//
// All values in mm unless noted. Inches converted: 1in = 25.4mm.
// ============================================================================

// DIN 5008 Form B page geometry
static constexpr float k_margin_left_mm   = 25.0f;
static constexpr float k_margin_right_mm  = 20.0f;
static constexpr float k_margin_bottom_mm = 20.0f;

// DIN 5008 Form B info block
static constexpr float k_sender_x_mm     = 125.0f;
static constexpr float k_sender_y_mm     = 50.0f;
static constexpr float k_sender_w_mm     = 65.0f;

// DIN 5008 Form B address field
static constexpr float k_address_field_x_mm = 20.0f;
static constexpr float k_address_text_x_mm  = 25.0f;
static constexpr float k_address_field_w_mm = 85.0f;
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
static constexpr float k_comm_sender_y_mm = k_sender_y_mm;
static constexpr float k_top_rule_y_mm   = 45.0f;
static constexpr float k_top_rule_x1_mm  = 0.0f;
static constexpr float k_top_rule_x2_mm  = k_page_width_mm;
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

// Build the sender block text from profile lines + email, with a blank line
// between address and email (matching the \\~\\ in the LaTeX template).
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
// Letter builder — simple style
// ============================================================================

Document build_letter(const Sender_profile& profile,
                      const Letter_input& input,
                      const std::string& profile_dir)
{
    Document doc;
    doc.page_width_mm  = k_page_width_mm;
    doc.page_height_mm = k_page_height_mm;

    // Wrap body text and compute pagination
    auto body_lines = wrap_text(input.body, Font_id::sans,
                                k_body_size_pt, k_body_width_mm);

    auto ret_metrics = measure_text(profile.return_address_line,
                                    Font_id::sans,
                                    k_return_size_pt,
                                    0,
                                    k_address_text_w_mm,
                                    false);
    float return_rule_x2_mm = k_return_x_mm + pt_to_mm(ret_metrics.width_pt);

    auto sender_metrics = measure_text(build_sender_text(profile),
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

    // DIN 5008 Form B: subject begins below the info block. If there is no
    // subject, the letter starts directly at the regular content top.
    bool has_subject = !input.subject.empty();
    float subject_y_mm = info_block_bottom_mm + k_subject_gap_mm;
    float body_y_mm = has_subject ? (subject_y_mm + k_subject_to_body_mm)
                                  : subject_y_mm;

    // Compute available height on first page for body content
    float first_page_body_top_mm = body_y_mm;
    float first_page_body_bottom_mm = k_footer_y_mm - 5.0f;
    float first_page_avail_mm = first_page_body_bottom_mm - first_page_body_top_mm;

    // Closing block: "Mit freundlichen Grüßen" + signature + signer name
    // UTF-8: ü = \xc3\xbc, ß = \xc3\x9f
    std::string closing_text = "Mit freundlichen Gr" "\xc3\xbc" "\xc3" "\x9f" "en";
    float closing_height_mm = pt_to_mm(k_closing_skip_baselines * k_body_lead_pt)
        + pt_to_mm(k_body_lead_pt);   // closing line

    // Signature image height (estimate)
    float sig_height_mm = 0;
    std::string sig_path;
    if (!profile.signature_image.empty()) {
        sig_path = profile_dir + "/" + profile.signature_image;
        sig_height_mm = k_sig_width_mm * 0.4f;  // rough aspect ratio estimate
    }
    float signer_height_mm = pt_to_mm(k_body_lead_pt);
    float total_closing_mm = closing_height_mm + sig_height_mm + signer_height_mm + 5.0f;

    // Split body lines across pages
    float line_height_mm = pt_to_mm(k_body_lead_pt);

    int total_body_lines = (int)body_lines.size();
    int lines_first_page_with_closing =
        fit_line_count(first_page_avail_mm - total_closing_mm, line_height_mm);
    int lines_first_page_without_closing =
        fit_line_count(first_page_avail_mm, line_height_mm);

    // Continuation pages
    float cont_avail_mm = (k_footer_y_mm - 5.0f) - k_cont_top_mm;
    int lines_per_cont_with_closing =
        fit_line_count(cont_avail_mm - total_closing_mm, line_height_mm);
    int lines_per_cont_without_closing =
        fit_line_count(cont_avail_mm, line_height_mm);

    // Distribute lines across pages
    struct Page_content
    {
        int line_start;
        int line_count;
        bool has_closing;
    };
    std::vector<Page_content> page_plan;

    if (total_body_lines <= lines_first_page_with_closing) {
        page_plan.push_back({ 0, total_body_lines, true });
    } else {
        int offset = 0;
        int remaining = total_body_lines;

        int first_take = std::min(remaining, lines_first_page_without_closing);
        page_plan.push_back({ offset, first_take, false });
        offset += first_take;
        remaining -= first_take;

        while (remaining > 0) {
            if (remaining <= lines_per_cont_with_closing) {
                page_plan.push_back({ offset, remaining, true });
                break;
            }

            int take = std::min(remaining, lines_per_cont_without_closing);
            page_plan.push_back({ offset, take, false });
            offset += take;
            remaining -= take;
        }
    }

    int total_pages = (int)page_plan.size();

    // Build pages
    for (int pi = 0; pi < total_pages; pi++) {
        Page page;
        add_fold_marks(page);

        const auto& pc = page_plan[pi];

        if (pi == 0) {
            // -- First page: header elements --
            bool commercial = (profile.style == Profile_style::commercial);

            // Commercial: company name block
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

                // Top rule (colored line across the page)
                page.elements.push_back(Line_segment{
                    k_top_rule_x1_mm, k_top_rule_y_mm,
                    company_right_mm, k_top_rule_y_mm,
                    k_top_rule_width_pt, profile.top_rule_color
                });
            }

            // Sender block (commercial shifts down to 1.3in)
            float sender_y = commercial ? k_comm_sender_y_mm : k_sender_y_mm;
            page.elements.push_back(Text_block{
                k_sender_x_mm, sender_y, k_sender_w_mm,
                build_sender_text(profile),
                Font_id::sans, k_sender_size_pt, k_sender_lead_pt,
                k_black, false
            });

            // Return-address line
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

            // Recipient
            page.elements.push_back(Text_block{
                k_recip_x_mm, k_recip_y_mm, k_recip_w_mm,
                input.recipient,
                Font_id::sans, k_recip_size_pt, k_recip_lead_pt,
                k_black, false
            });

            // Date
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

        // -- Body lines for this page --
        float body_top = (pi == 0) ? first_page_body_top_mm : k_cont_top_mm;

        // Reassemble the body lines for this page into a single string
        std::string page_body;
        for (int li = 0; li < pc.line_count; li++) {
            if (li > 0) page_body += '\n';
            page_body += body_lines[pc.line_start + li];
        }

        page.elements.push_back(Text_block{
            k_margin_left_mm, body_top, k_body_width_mm,
            page_body,
            Font_id::sans, k_body_size_pt, k_body_lead_pt,
            k_black, false
        });

        // -- Closing (on last page) --
        if (pc.has_closing) {
            float closing_y = body_top
                + (float)pc.line_count * line_height_mm
                + pt_to_mm(k_closing_skip_baselines * k_body_lead_pt);

            page.elements.push_back(Text_block{
                k_margin_left_mm, closing_y, k_body_width_mm,
                closing_text,
                Font_id::sans, k_body_size_pt, k_body_lead_pt,
                k_black, false
            });

            float after_closing = closing_y + pt_to_mm(k_body_lead_pt) + 2.0f;

            // Signature image
            if (!sig_path.empty()) {
                page.elements.push_back(Image_block{
                    k_margin_left_mm, after_closing, k_sig_width_mm,
                    sig_path
                });
                after_closing += sig_height_mm + 2.0f;
            }

            // Signer name
            page.elements.push_back(Text_block{
                k_margin_left_mm, after_closing, k_body_width_mm,
                profile.signer_name,
                Font_id::sans, k_body_size_pt, k_body_lead_pt,
                k_black, false
            });

            // Signer title (commercial only)
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

    return doc;
}
