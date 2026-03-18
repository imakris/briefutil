#include "briefutil/letter_builder.h"
#include "briefutil/markdown_parser.h"
#include "briefutil/pdf_renderer_haru.h"
#include "rich_text_layout.h"

#include <utility>


static constexpr color_t k_black = { 0, 0, 0 };


// ============================================================================
// Helpers
// ============================================================================

static void add_fold_marks(Page& page, const Letter_layout_spec& L)
{
    page.elements.push_back(line_segment_t{
        L.mark_x_mm, L.fold1_y_mm, L.mark_x_mm + L.fold_len_mm, L.fold1_y_mm, 0.5f, k_black
    });
    page.elements.push_back(line_segment_t{
        L.mark_x_mm, L.fold2_y_mm, L.mark_x_mm + L.fold_len_mm, L.fold2_y_mm, 0.5f, k_black
    });
    page.elements.push_back(line_segment_t{
        L.mark_x_mm, L.punch_y_mm, L.mark_x_mm + L.punch_len_mm, L.punch_y_mm, 0.5f, k_black
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
                                 const std::string& profile_dir,
                                 const Theme_config& theme,
                                 const Letter_layout_spec& layout)
{
    const auto& typo = theme.typo;
    const auto& L = layout;
    float body_width_mm = L.page_width_mm - L.margin_left_mm - L.margin_right_mm;
    float footer_y_base = L.page_height_mm - L.footer_margin_mm;

    Document doc;
    doc.page_width_mm  = L.page_width_mm;
    doc.page_height_mm = L.page_height_mm;

    auto sender_text = build_sender_text(profile);

    auto ret_metrics = measure_text(profile.return_address_line,
                                    Font_id::SANS,
                                    typo.return_size_pt,
                                    0,
                                    L.address_text_w_mm,
                                    false, theme.fonts);
    float return_rule_x2_mm = L.address_text_x_mm + pt_to_mm(ret_metrics.width_pt);

    auto sender_metrics = measure_text(sender_text,
                                       Font_id::SANS,
                                       typo.sender_size_pt,
                                       typo.sender_lead_pt,
                                       L.sender_w_mm,
                                       false, theme.fonts);
    auto date_metrics = measure_text(input.date,
                                     Font_id::SANS,
                                     typo.date_size_pt,
                                     0,
                                     L.sender_w_mm,
                                     false, theme.fonts);

    float sender_bottom_mm = L.sender_y_mm + pt_to_mm(sender_metrics.height_pt);
    float date_bottom_mm = L.date_y_mm + pt_to_mm(date_metrics.height_pt);
    float info_block_bottom_mm = std::max(
        L.sender_y_mm + L.info_block_min_h_mm,
        std::max(sender_bottom_mm, date_bottom_mm));

    bool has_subject = !input.subject.empty();
    float subject_y_mm = info_block_bottom_mm + L.subject_gap_mm;
    float body_y_mm = has_subject ? (subject_y_mm + L.subject_to_body_mm)
                                  : subject_y_mm;

    // Closing block height estimate
    // UTF-8: ü = \xc3\xbc, ß = \xc3\x9f
    std::string closing_text = "Mit freundlichen Gr" "\xc3\xbc" "\xc3" "\x9f" "en";
    float closing_height_mm = pt_to_mm(L.closing_skip_baselines * typo.body_lead_pt)
        + pt_to_mm(typo.body_lead_pt);

    float sig_height_mm = 0;
    std::string sig_path;
    if (!profile.signature_image.empty()) {
        sig_path = profile_dir + "/" + profile.signature_image;
        sig_height_mm = L.sig_width_mm * 0.4f;
    }
    float signer_height_mm = pt_to_mm(typo.body_lead_pt);
    if (!profile.signer_title.empty())
        signer_height_mm += pt_to_mm(typo.body_lead_pt);
    float total_closing_mm = closing_height_mm + sig_height_mm + signer_height_mm + 5.0f;

    // Parse and lay out the body using the markdown-aware layout engine
    auto body_blocks = parse_markdown(input.body);

    Layout_params lp;
    lp.left_mm     = L.margin_left_mm;
    lp.width_mm    = body_width_mm;
    lp.body_color  = k_black;
    lp.profile_dir = profile_dir;
    lp.typo        = typo;
    lp.fonts       = theme.fonts;

    float page_bottom = footer_y_base - 5.0f;

    auto body_layout = layout_body(body_blocks, lp,
                                   body_y_mm, page_bottom,
                                   L.cont_top_mm, page_bottom);

    if (!body_layout.error.empty()) {
        return { {}, body_layout.error };
    }

    // If closing doesn't fit on the last page, add a continuation page for it
    if (body_layout.last_page_used_mm + total_closing_mm > page_bottom) {
        body_layout.pages.push_back({});
        body_layout.last_page_used_mm = L.cont_top_mm;
    }

    int total_pages = (int)body_layout.pages.size();

    // Build pages
    for (int pi = 0; pi < total_pages; pi++) {
        Page page;
        add_fold_marks(page, L);

        if (pi == 0) {
            // -- First page: header elements --
            bool commercial = (profile.style == Profile_style::COMMERCIAL);

            if (commercial && !profile.logo_image.empty()) {
                auto logo_path = profile_dir + "/" + profile.logo_image;
                auto logo_dims = measure_png(logo_path);
                if (logo_dims.valid) {
                    page.elements.push_back(line_segment_t{
                        L.top_rule_x1_mm, L.top_rule_y_mm,
                        L.company_x_mm + L.company_w_mm, L.top_rule_y_mm,
                        L.top_rule_width_pt, profile.top_rule_color
                    });

                    float logo_w = L.company_w_mm;
                    float aspect = logo_dims.height_px / logo_dims.width_px;
                    float logo_h = logo_w * aspect;
                    float max_h = L.top_rule_y_mm - L.logo_rule_gap_mm - L.logo_top_mm;
                    if (max_h > 0 && logo_h > max_h) {
                        float scale = max_h / logo_h;
                        logo_w *= scale;
                        logo_h = max_h;
                    }
                    float logo_y = L.top_rule_y_mm - L.logo_rule_gap_mm - logo_h;

                    page.elements.push_back(Image_block{
                        L.company_x_mm, logo_y, logo_w, logo_path
                    });
                }
            }

            page.elements.push_back(Text_block{
                L.sender_x_mm, L.sender_y_mm, L.sender_w_mm,
                sender_text,
                Font_id::SANS, typo.sender_size_pt, typo.sender_lead_pt,
                k_black, false
            });

            page.elements.push_back(Text_block{
                L.address_text_x_mm, L.return_y_mm, L.address_text_w_mm,
                profile.return_address_line,
                Font_id::SANS, typo.return_size_pt, 0,
                L.footer_color, false
            });

            page.elements.push_back(line_segment_t{
                L.address_text_x_mm, L.return_rule_y_mm,
                return_rule_x2_mm, L.return_rule_y_mm,
                0.5f, k_black
            });

            page.elements.push_back(Text_block{
                L.address_text_x_mm, L.recip_y_mm, L.address_text_w_mm,
                input.recipient,
                Font_id::SANS, typo.recip_size_pt, typo.recip_lead_pt,
                k_black, false
            });

            page.elements.push_back(Text_block{
                L.sender_x_mm, L.date_y_mm, L.sender_w_mm,
                input.date,
                Font_id::SANS, typo.date_size_pt, 0,
                k_black, false
            });

            if (has_subject) {
                page.elements.push_back(Text_block{
                    L.margin_left_mm, subject_y_mm, body_width_mm,
                    input.subject,
                    Font_id::SANS_BOLD, typo.body_size_pt, typo.body_lead_pt,
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
                + pt_to_mm(L.closing_skip_baselines * typo.body_lead_pt);

            page.elements.push_back(Text_block{
                L.margin_left_mm, closing_y, body_width_mm,
                closing_text,
                Font_id::SANS, typo.body_size_pt, typo.body_lead_pt,
                k_black, false
            });

            float after_closing = closing_y + pt_to_mm(typo.body_lead_pt) + 2.0f;

            if (!sig_path.empty()) {
                page.elements.push_back(Image_block{
                    L.margin_left_mm, after_closing, L.sig_width_mm,
                    sig_path
                });
                after_closing += sig_height_mm + 2.0f;
            }

            page.elements.push_back(Text_block{
                L.margin_left_mm, after_closing, body_width_mm,
                profile.signer_name,
                Font_id::SANS, typo.body_size_pt, typo.body_lead_pt,
                k_black, false
            });

            if (!profile.signer_title.empty()) {
                after_closing += pt_to_mm(typo.body_lead_pt);
                page.elements.push_back(Text_block{
                    L.margin_left_mm, after_closing, body_width_mm,
                    profile.signer_title,
                    Font_id::SANS, typo.body_size_pt, typo.body_lead_pt,
                    k_black, false
                });
            }
        }

        // -- Footer --
        float footer_y = footer_y_base;

        // Page number (only on multi-page)
        if (total_pages > 1) {
            std::string page_num = "Seite " + std::to_string(pi + 1)
                + " von " + std::to_string(total_pages);
            auto page_num_metrics = measure_text(page_num, Font_id::SANS,
                                                 typo.footer_size_pt, 0, 200, false,
                                                 theme.fonts);
            float page_num_x = L.page_width_mm - L.margin_right_mm
                - pt_to_mm(page_num_metrics.width_pt);
            page.elements.push_back(Text_block{
                page_num_x, footer_y, body_width_mm,
                page_num,
                Font_id::SANS, typo.footer_size_pt, 0,
                k_black, false
            });
            footer_y += pt_to_mm(typo.footer_size_pt) + 3.0f;
        }

        // Commercial footer lines (on every page)
        if (profile.style == Profile_style::COMMERCIAL) {
            for (const auto& fl : profile.footer_lines) {
                page.elements.push_back(Text_block{
                    L.margin_left_mm, footer_y, body_width_mm,
                    fl,
                    Font_id::SANS, typo.footer_text_size_pt,
                    typo.footer_text_size_pt,
                    L.footer_color, false
                });
                footer_y += pt_to_mm(typo.footer_text_size_pt) + 1.0f;
            }
        }

        doc.pages.push_back(std::move(page));
    }

    return { std::move(doc), "" };
}


Render_result generate_letter_pdf(const Sender_profile& profile,
                                  const Letter_input& input,
                                  const std::string& profile_dir,
                                  const std::string& output_path,
                                  const Theme_config& theme,
                                  const Letter_layout_spec& layout)
{
    auto br = build_letter(profile, input, profile_dir, theme, layout);
    if (!br.error.empty()) {
        return { false, "", br.error, "" };
    }
    return render_pdf(br.doc, output_path, theme.fonts);
}
