#include "briefutil/pdf_renderer.h"
#include "briefutil/pdf_measurement.h"

#include "pdf_mark2haru_support.h"

#include <mark2haru/pdf_writer.h>
#include <mark2haru/png_image.h>

#include <type_traits>


// ============================================================================
// mark2haru renderer
// ============================================================================

static float pt_x(float x_mm) { return mm_to_pt(x_mm); }
static float pt_y(float y_mm) { return mm_to_pt(y_mm); }

static bool render_text_block(
    mark2haru::Pdf_writer& writer,
    const Text_block& tb,
    const Font_family_config& fonts)
{
    std::vector<std::string> lines;
    if (tb.wrap) {
        lines = wrap_text(
            tb.text,
            tb.font,
            tb.size_pt,
            tb.width_mm,
            fonts);
    }
    else {
        lines = split_lines(tb.text);
    }

    if (lines.empty()) {
        return true;
    }

    float lead = tb.leading_pt > 0 ? tb.leading_pt : tb.size_pt;
    float y_top_pt = pt_y(tb.y_mm);
    auto font = mark2haru_font_for(tb.font);
    const mark2haru::color_t color = { tb.color.r, tb.color.g, tb.color.b };

    for (size_t i = 0; i < lines.size(); ++i) {
        writer.draw_text(
            pt_x(tb.x_mm),
            y_top_pt + static_cast<float>(i) * lead,
            tb.size_pt,
            font,
            lines[i],
            color);
    }
    return true;
}

static bool render_text_span(
    mark2haru::Pdf_writer& writer,
    const Text_span& ts)
{
    auto font = mark2haru_font_for(ts.font);
    writer.draw_text(
        pt_x(ts.x_mm),
        pt_y(ts.y_mm),
        ts.size_pt,
        font,
        ts.text,
        { ts.color.r, ts.color.g, ts.color.b });
    return true;
}

static bool render_line_segment(
    mark2haru::Pdf_writer& writer,
    const line_segment_t& ls)
{
    writer.stroke_line(
        pt_x(ls.x1_mm),
        pt_y(ls.y1_mm),
        pt_x(ls.x2_mm),
        pt_y(ls.y2_mm),
        { ls.color.r, ls.color.g, ls.color.b },
        ls.stroke_width_pt);
    return true;
}

static bool render_filled_rect(
    mark2haru::Pdf_writer& writer,
    const filled_rect_t& fr)
{
    writer.fill_rect(
        pt_x(fr.x_mm),
        pt_y(fr.y_mm),
        mm_to_pt(fr.width_mm),
        mm_to_pt(fr.height_mm),
        { fr.color.r, fr.color.g, fr.color.b });
    return true;
}

static bool render_image_block(
    const Image_block& ib,
    const Localization& loc,
    mark2haru::Pdf_writer& writer)
{
    auto image_path = qstring_to_path(QString::fromUtf8(ib.path.c_str()));

    mark2haru::Png_image image;
    if (!image.load_from_file(image_path)) {
        const std::string placeholder =
            format_image_not_found(loc.image_not_found_format, ib.path);
        writer.draw_text(
            pt_x(ib.x_mm),
            pt_y(ib.y_mm),
            8.0,
            mark2haru::Pdf_font::ITALIC,
            placeholder,
            { 0.6, 0.0, 0.0 });
        return true;
    }

    if (image.width_px() <= 0) {
        return false;
    }

    const float target_w = mm_to_pt(ib.width_mm);
    const float target_h = target_w * (float(image.height_px()) / float(image.width_px()));
    return writer.draw_png(pt_x(ib.x_mm), pt_y(ib.y_mm), target_w, target_h, image);
}

static Render_result render_pdf_mark2haru_impl(
    const Document& doc,
    const std::string& output_path,
    const Font_family_config& fonts,
    const Localization& loc)
{
    std::string detail;
    auto metrics = make_mark2haru_measurement_context(fonts, &detail);
    if (!metrics) {
        return { false, "", loc.error_pdf_create_failed,
                 detail.empty()
                    ? "Failed to initialize the mark2haru measurement context."
                    : detail };
    }

    mark2haru::Pdf_writer writer(
        mm_to_pt(doc.page_width_mm),
        mm_to_pt(doc.page_height_mm),
        metrics);
    if (!writer.fonts_loaded()) {
        return { false, "", loc.error_pdf_create_failed, writer.font_error() };
    }

    bool first_page = true;
    for (const auto& page_def : doc.pages) {
        if (!first_page) {
            writer.begin_page();
        }
        first_page = false;

        for (const auto& elem : page_def.elements) {
            bool ok = std::visit([&](const auto& e) {
                using Element_type = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<Element_type, Text_block>) {
                    return render_text_block(writer, e, fonts);
                }
                else
                if constexpr (std::is_same_v<Element_type, line_segment_t>) {
                    return render_line_segment(writer, e);
                }
                else
                if constexpr (std::is_same_v<Element_type, Image_block>) {
                    return render_image_block(e, loc, writer);
                }
                else
                if constexpr (std::is_same_v<Element_type, Text_span>) {
                    return render_text_span(writer, e);
                }
                else
                if constexpr (std::is_same_v<Element_type, filled_rect_t>) {
                    return render_filled_rect(writer, e);
                }
                return false;
            }, elem);
            if (!ok) {
                return { false, "", loc.error_pdf_create_failed, "mark2haru rendering failed." };
            }
        }
    }

    const QString output_qpath = QString::fromUtf8(output_path.c_str());
    if (!writer.save(qstring_to_path(output_qpath))) {
        return { false, "", loc.error_pdf_save_failed,
                 "mark2haru failed to save the PDF stream." };
    }

    return { true, output_path, "", "" };
}

Render_result render_pdf(
    const Document& doc,
    const std::string& output_path,
    const Font_family_config& fonts,
    const Localization& loc)
{
    return render_pdf_mark2haru_impl(doc, output_path, fonts, loc);
}
