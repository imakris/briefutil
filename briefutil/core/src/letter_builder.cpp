#include "briefutil/letter_builder.h"
#include "briefutil/localization.h"
#include "briefutil/pdf_measurement.h"
#include "briefutil/pdf_renderer.h"
#include "rich_text_layout.h"

#include <mark2haru/markdown.h>

#include <cstddef>
#include <utility>


static constexpr color_t k_black = { 0, 0, 0 };


// ============================================================================
// Helpers
// ============================================================================

static void add_fold_marks(Page& page, const letter_layout_spec_t& layout)
{
    page.elements.push_back(line_segment_t{
        layout.mark_x_mm, layout.fold1_y_mm, layout.mark_x_mm + layout.fold_len_mm, layout.fold1_y_mm, 0.5f, k_black
    });
    page.elements.push_back(line_segment_t{
        layout.mark_x_mm, layout.fold2_y_mm, layout.mark_x_mm + layout.fold_len_mm, layout.fold2_y_mm, 0.5f, k_black
    });
    page.elements.push_back(line_segment_t{
        layout.mark_x_mm, layout.punch_y_mm, layout.mark_x_mm + layout.punch_len_mm, layout.punch_y_mm, 0.5f, k_black
    });
}

// Build the sender block text from profile lines + email.
static std::string build_sender_text(const Sender_profile& profile)
{
    std::string text;
    for (const auto& line : profile.sender_lines) {
        if (!text.empty()) {
            text += '\n';
        }
        text += line;
    }
    if (!profile.email.empty()) {
        text += "\n\n";
        text += profile.email;
    }
    return text;
}

static float footer_block_height_mm(
    const Sender_profile&          profile,
    const typography_config_t&     typo,
    const letter_layout_spec_t&    layout,
    float                          body_width_mm,
    const Pdf_measurement&         measurement,
    bool                           include_page_number)
{
    float height_mm = 0.0f;
    if (include_page_number) {
        height_mm += pt_to_mm(typo.footer_size_pt);
    }

    if (profile.style != Profile_style::COMMERCIAL ||
        profile.footer_lines.empty())
    {
        return height_mm;
    }

    if (height_mm > 0.0f) {
        height_mm += layout.footer_line_gap_mm;
    }

    for (std::size_t i = 0; i < profile.footer_lines.size(); ++i) {
        const auto footer_metrics = measurement.measure_text(
            profile.footer_lines[i],
            Font_id::SANS,
            typo.footer_text_size_pt,
            typo.footer_text_size_pt,
            body_width_mm,
            true);
        height_mm += pt_to_mm(footer_metrics.height_pt);
        if (i + 1 < profile.footer_lines.size()) {
            height_mm += 1.0f;
        }
    }
    return height_mm;
}

static float footer_top_y_mm(
    const Sender_profile&          profile,
    const typography_config_t&     typo,
    const letter_layout_spec_t&    layout,
    float                          body_width_mm,
    const Pdf_measurement&         measurement,
    bool                           include_page_number)
{
    return
        layout.page_height_mm   -
        layout.footer_margin_mm -
        footer_block_height_mm(profile, typo, layout, body_width_mm, measurement, include_page_number);
}


// ============================================================================
// Letter builder
// ============================================================================

Build_letter_result build_letter(
    const Sender_profile&          profile,
    const Letter_input&            input,
    const std::string&             profile_dir,
    const Theme_config&            theme,
    const letter_layout_spec_t&    layout,
    const Localization&            loc)
{
    const auto typo          = scaled_typography(theme.typo);
    float      body_width_mm = layout.page_width_mm - layout.margin_left_mm - layout.margin_right_mm;

    Document doc;
    doc.page_width_mm  = layout.page_width_mm;
    doc.page_height_mm = layout.page_height_mm;

    auto sender_text = build_sender_text(profile);

    // One load of the font family for the whole letter. It is handed back in
    // the result so the renderer draws with the metrics that placed the text.
    auto measurement = std::make_shared<const Pdf_measurement>(theme.fonts);
    if (!measurement->ready()) {
        return { {}, measurement->error(), nullptr };
    }

    auto ret_metrics = measurement->measure_text(
        profile.return_address_line,
        Font_id::SANS,
        typo.return_size_pt,
        0,
        layout.address_text_w_mm,
        false);
    float return_rule_x2_mm = layout.address_text_x_mm + pt_to_mm(ret_metrics.width_pt);

    auto sender_metrics = measurement->measure_text(
        sender_text,
        Font_id::SANS,
        typo.sender_size_pt,
        typo.sender_lead_pt,
        layout.sender_w_mm,
        false);
    auto date_metrics = measurement->measure_text(
        input.date,
        Font_id::SANS,
        typo.date_size_pt,
        0,
        layout.sender_w_mm,
        false);

    float sender_bottom_mm = layout.sender_y_mm + pt_to_mm(sender_metrics.height_pt);
    float date_bottom_mm   = layout.date_y_mm + pt_to_mm(date_metrics.height_pt);
    float info_block_bottom_mm = std::max(
        layout.sender_y_mm + layout.info_block_min_h_mm,
        std::max(sender_bottom_mm, date_bottom_mm));

    bool  has_subject  = !input.subject.empty();
    float subject_y_mm = info_block_bottom_mm + layout.subject_gap_mm;
    auto  subject_metrics = measurement->measure_text(
        input.subject,
        Font_id::SANS_BOLD,
        typo.body_size_pt,
        typo.body_lead_pt,
        body_width_mm,
        true);
    float subject_extra_height_mm = has_subject
        ? pt_to_mm(subject_metrics.height_pt - typo.body_size_pt)
        : 0.0f;
    float body_y_mm = has_subject
        ? (subject_y_mm + layout.subject_to_body_mm + subject_extra_height_mm)
        : subject_y_mm;

    const std::string& closing_text = profile.closing_phrase.empty()
        ? loc.closing
        : profile.closing_phrase;

    // Closing block height estimate
    auto closing_metrics = measurement->measure_text(
        closing_text,
        Font_id::SANS,
        typo.body_size_pt,
        typo.body_lead_pt,
        body_width_mm,
        false);
    float closing_height_mm = pt_to_mm(
        layout.closing_skip_baselines * typo.body_lead_pt) + pt_to_mm(closing_metrics.height_pt);

    float sig_height_mm = 0;
    std::string sig_path;
    if (!profile.signature_image.empty()) {
        sig_path = profile_dir + "/" + profile.signature_image;
        // Use the real PNG aspect ratio when available so the closing-fit
        // estimate matches what the renderer will actually draw.
        auto sig_dims = measure_png(sig_path);
        float aspect = (sig_dims.valid && sig_dims.width_px > 0)
            ? (sig_dims.height_px / sig_dims.width_px)
            : layout.sig_default_aspect;
        sig_height_mm = layout.sig_width_mm * aspect;
    }
    const bool has_signer_title =
        !profile.signer_title.empty() && profile.signer_title != profile.signer_name;
    float signer_height_mm = pt_to_mm(typo.body_lead_pt);
    if (has_signer_title) {
        signer_height_mm += pt_to_mm(typo.body_lead_pt);
    }
    float total_closing_mm = closing_height_mm + sig_height_mm
        + signer_height_mm + layout.closing_extra_room_mm;

    // Parse and lay out the body using the markdown-aware layout engine
    auto body_blocks = mark2haru::parse_markdown(input.body);

    Layout_params lp{ *measurement };
    lp.left_mm     = layout.margin_left_mm;
    lp.width_mm    = body_width_mm;
    lp.body_color  = k_black;
    lp.profile_dir = profile_dir;
    lp.typo        = typo;
    lp.loc         = loc;

    auto make_body_layout = [&](bool include_page_number) {
        const float page_bottom = footer_top_y_mm(
            profile,
            typo,
            layout,
            body_width_mm,
            *measurement,
            include_page_number) - layout.page_bottom_buffer_mm;
        auto result = layout_body(
            body_blocks,
            lp,
            body_y_mm,
            page_bottom,
            layout.cont_top_mm,
            page_bottom);
        if (!result.error.empty()) {
            return result;
        }

        if (result.last_page_used_mm + total_closing_mm > page_bottom) {
            result.pages.push_back({});
            result.last_page_used_mm = layout.cont_top_mm;
        }
        return result;
    };

    auto body_layout = make_body_layout(false);

    if (!body_layout.error.empty()) {
        return { {}, body_layout.error };
    }

    if (body_layout.pages.size() > 1) {
        body_layout = make_body_layout(true);
        if (!body_layout.error.empty()) {
            return { {}, body_layout.error };
        }
    }

    int total_pages = (int)body_layout.pages.size();

    // The body layout moves the closing block onto a fresh continuation page
    // when it does not fit after the body. Guarantee it can fit there at all:
    // a closing taller than an entire continuation-page body (an oversized
    // signature image or a very large typography scale) cannot be placed
    // anywhere, and must fail rather than silently overflow into the footer.
    {
        const float lead_mm = pt_to_mm(typo.body_lead_pt);
        float closing_block_mm =
            pt_to_mm(layout.closing_skip_baselines * typo.body_lead_pt)
            + lead_mm + layout.closing_after_pad_mm;
        if (!sig_path.empty()) {
            closing_block_mm += sig_height_mm + layout.signature_after_pad_mm;
        }
        closing_block_mm += lead_mm; // signer name line
        if (has_signer_title) {
            closing_block_mm += lead_mm; // signer title line
        }

        const float closing_page_bottom = footer_top_y_mm(
            profile, typo, layout, body_width_mm, *measurement, total_pages > 1)
            - layout.page_bottom_buffer_mm;
        if (closing_block_mm > closing_page_bottom - layout.cont_top_mm) {
            return { {}, loc.error_closing_does_not_fit };
        }
    }

    // Build pages
    for (int pi = 0; pi < total_pages; pi++) {
        Page page;
        add_fold_marks(page, layout);

        if (pi == 0) {
            // -- First page: header elements --
            bool commercial = (profile.style == Profile_style::COMMERCIAL);

            if (commercial && !profile.logo_image.empty()) {
                auto logo_path = profile_dir + "/" + profile.logo_image;
                auto logo_dims = measure_png(logo_path);
                if (logo_dims.valid && logo_dims.width_px > 0) {
                    page.elements.push_back(line_segment_t{
                        layout.top_rule_x1_mm, layout.top_rule_y_mm,
                        layout.company_x_mm + layout.company_w_mm, layout.top_rule_y_mm,
                        layout.top_rule_width_pt, profile.top_rule_color
                    });

                    float logo_w = layout.company_w_mm;
                    float aspect = logo_dims.height_px / logo_dims.width_px;
                    float logo_h = logo_w * aspect;
                    float max_h  = layout.top_rule_y_mm - layout.logo_rule_gap_mm - layout.logo_top_mm;
                    if (max_h > 0 && logo_h > max_h) {
                        float scale = max_h / logo_h;
                        logo_w *= scale;
                        logo_h = max_h;
                    }
                    float logo_x = layout.company_x_mm + (layout.company_w_mm - logo_w) / 2;
                    float logo_y = layout.top_rule_y_mm - layout.logo_rule_gap_mm - logo_h;

                    page.elements.push_back(Image_block{
                        logo_x, logo_y, logo_w, logo_path
                    });
                }
            }

            page.elements.push_back(Text_block{
                layout.sender_x_mm, layout.sender_y_mm, layout.sender_w_mm,
                sender_text,
                Font_id::SANS, typo.sender_size_pt, typo.sender_lead_pt,
                k_black, false
            });

            page.elements.push_back(Text_block{
                layout.address_text_x_mm, layout.return_y_mm, layout.address_text_w_mm,
                profile.return_address_line,
                Font_id::SANS, typo.return_size_pt, 0,
                layout.footer_color, false
            });

            page.elements.push_back(line_segment_t{
                layout.address_text_x_mm, layout.return_rule_y_mm,
                return_rule_x2_mm, layout.return_rule_y_mm,
                0.5f, k_black
            });

            page.elements.push_back(Text_block{
                layout.address_text_x_mm, layout.recip_y_mm, layout.address_text_w_mm,
                input.recipient,
                Font_id::SANS, typo.recip_size_pt, typo.recip_lead_pt,
                k_black, false
            });

            page.elements.push_back(Text_block{
                layout.sender_x_mm, layout.date_y_mm, layout.sender_w_mm,
                input.date,
                Font_id::SANS, typo.date_size_pt, 0,
                k_black, false
            });

            if (has_subject) {
                page.elements.push_back(Text_block{
                    layout.margin_left_mm, subject_y_mm, body_width_mm,
                    input.subject,
                    Font_id::SANS_BOLD, typo.body_size_pt, typo.body_lead_pt,
                    k_black, true
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
                + pt_to_mm(layout.closing_skip_baselines * typo.body_lead_pt);

            page.elements.push_back(Text_block{
                layout.margin_left_mm, closing_y, body_width_mm,
                closing_text,
                Font_id::SANS, typo.body_size_pt, typo.body_lead_pt,
                k_black, false
            });

            float after_closing = closing_y + pt_to_mm(typo.body_lead_pt)
                + layout.closing_after_pad_mm;

            if (!sig_path.empty()) {
                page.elements.push_back(Image_block{
                    layout.margin_left_mm, after_closing, layout.sig_width_mm,
                    sig_path
                });
                after_closing += sig_height_mm + layout.signature_after_pad_mm;
            }

            page.elements.push_back(Text_block{
                layout.margin_left_mm, after_closing, body_width_mm,
                profile.signer_name,
                Font_id::SANS, typo.body_size_pt, typo.body_lead_pt,
                k_black, false
            });

            if (has_signer_title) {
                after_closing += pt_to_mm(typo.body_lead_pt);
                page.elements.push_back(Text_block{
                    layout.margin_left_mm, after_closing, body_width_mm,
                    profile.signer_title,
                    Font_id::SANS, typo.body_size_pt, typo.body_lead_pt,
                    k_black, false
                });
            }
        }

        // -- Footer --
        const bool include_page_number = total_pages > 1;
        float footer_y = footer_top_y_mm(
            profile,
            typo,
            layout,
            body_width_mm,
            *measurement,
            include_page_number);

        // Page number (only on multi-page)
        if (include_page_number) {
            std::string page_num = format_page_number(loc.page_number_format,
                pi + 1, total_pages);
            auto page_num_metrics = measurement->measure_text(
                page_num,
                Font_id::SANS,
                typo.footer_size_pt,
                0,
                body_width_mm,
                false);
            const float page_num_width_mm = pt_to_mm(page_num_metrics.width_pt);
            float page_num_x = layout.page_width_mm - layout.margin_right_mm
                - page_num_width_mm;
            page.elements.push_back(Text_block{
                page_num_x, footer_y, page_num_width_mm,
                page_num,
                Font_id::SANS, typo.footer_size_pt, 0,
                k_black, false
            });
            footer_y += pt_to_mm(typo.footer_size_pt) + layout.footer_line_gap_mm;
        }

        // Commercial footer lines (on every page)
        if (profile.style == Profile_style::COMMERCIAL) {
            for (const auto& fl : profile.footer_lines) {
                const auto footer_metrics = measurement->measure_text(
                    fl,
                    Font_id::SANS,
                    typo.footer_text_size_pt,
                    typo.footer_text_size_pt,
                    body_width_mm,
                    true);
                page.elements.push_back(Text_block{
                    layout.margin_left_mm, footer_y, body_width_mm,
                    fl,
                    Font_id::SANS, typo.footer_text_size_pt,
                    typo.footer_text_size_pt,
                    layout.footer_color, true
                });
                footer_y += pt_to_mm(footer_metrics.height_pt) + 1.0f;
            }
        }

        doc.pages.push_back(std::move(page));
    }

    return { std::move(doc), "", std::move(measurement) };
}


Render_result generate_letter_pdf(
    const Sender_profile&          profile,
    const Letter_input&            input,
    const std::string&             profile_dir,
    const std::string&             output_path,
    const Theme_config&            theme,
    const letter_layout_spec_t&    layout,
    const Localization&            loc)
{
    auto br = build_letter(profile, input, profile_dir, theme, layout, loc);
    if (!br.error.empty()) {
        return { false, "", br.error, "" };
    }
    return render_pdf(br.doc, output_path, *br.measurement, loc);
}
