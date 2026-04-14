#include "briefutil/pdf_renderer.h"
#include "briefutil/pdf_measurement.h"

#include "pdf_haru_support.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>


// ============================================================================
// Renderer â€” libHaru backend
// ============================================================================

static float tl_x(float x_mm) { return mm_to_pt(x_mm); }

static float tl_y(float y_mm, float page_height_mm)
{
    return mm_to_pt(page_height_mm - y_mm);
}

static bool render_text_block(HPDF_Page page, Haru_context& ctx,
                              const Text_block& tb, float page_h_mm)
{
    auto font = ctx.font_for(tb.font);
    if (!font) {
        ctx.last_error = "Text rendering failed: font handle is unavailable.";
        return false;
    }
    float lead = tb.leading_pt > 0 ? tb.leading_pt : tb.size_pt;

    std::vector<std::string> lines;
    if (tb.wrap) {
        lines = wrap_text(Pdf_backend::Haru, tb.text, tb.font, tb.size_pt,
                          tb.width_mm, ctx.current_fc);
    }
    else {
        lines = split_lines(tb.text);
    }

    if (lines.empty()) return true;

    float x_pt = tl_x(tb.x_mm);
    float y_pt = tl_y(tb.y_mm, page_h_mm) - tb.size_pt;

    HPDF_Page_BeginText(page);
    HPDF_Page_SetFontAndSize(page, font.handle, tb.size_pt);
    HPDF_Page_SetRGBFill(page, tb.color.r, tb.color.g, tb.color.b);
    HPDF_Page_SetTextLeading(page, lead);
    HPDF_Page_MoveTextPos(page, x_pt, y_pt);

    for (size_t i = 0; i < lines.size(); i++) {
        if (i > 0) {
            HPDF_Page_MoveTextPos(page, 0, -lead);
        }
        auto encoded_line = encode_for_font(font, lines[i]);
        HPDF_Page_ShowText(page, encoded_line.c_str());
    }

    HPDF_Page_EndText(page);
    return ctx.last_error.empty();
}

static bool render_line_segment(HPDF_Page page, Haru_context& ctx,
                                const line_segment_t& ls, float page_h_mm)
{
    HPDF_Page_SetLineWidth(page, ls.stroke_width_pt);
    HPDF_Page_SetRGBStroke(page, ls.color.r, ls.color.g, ls.color.b);
    HPDF_Page_MoveTo(page, tl_x(ls.x1_mm), tl_y(ls.y1_mm, page_h_mm));
    HPDF_Page_LineTo(page, tl_x(ls.x2_mm), tl_y(ls.y2_mm, page_h_mm));
    HPDF_Page_Stroke(page);
    return ctx.last_error.empty();
}

static bool render_image_block(HPDF_Page page, Haru_context& ctx,
                               const Image_block& ib, float page_h_mm,
                               const Localization& loc)
{
    HPDF_Image img = HPDF_LoadPngImageFromFile(ctx.pdf, ib.path.c_str());
    if (!img) {
        ctx.last_error.clear();
        HPDF_ResetError(ctx.pdf);

        std::string placeholder = format_image_not_found(loc.image_not_found_format,
                                                          ib.path);
        auto font = ctx.font_for(Font_id::SANS_ITALIC);
        if (font) {
            auto encoded = encode_for_font(font, placeholder);
            float x_pt = tl_x(ib.x_mm);
            float y_pt = tl_y(ib.y_mm, page_h_mm) - 8.0f;
            HPDF_Page_BeginText(page);
            HPDF_Page_SetFontAndSize(page, font.handle, 8.0f);
            HPDF_Page_SetRGBFill(page, 0.6f, 0.0f, 0.0f);
            HPDF_Page_MoveTextPos(page, x_pt, y_pt);
            HPDF_Page_ShowText(page, encoded.c_str());
            HPDF_Page_EndText(page);
        }
        return true;
    }

    float target_w = mm_to_pt(ib.width_mm);
    float img_w = (float)HPDF_Image_GetWidth(img);
    float img_h = (float)HPDF_Image_GetHeight(img);
    if (img_w <= 0.0f) {
        ctx.last_error = "Image rendering failed: PNG has invalid width.";
        return false;
    }
    float target_h = target_w * (img_h / img_w);

    float x_pt = tl_x(ib.x_mm);
    float y_pt = tl_y(ib.y_mm, page_h_mm) - target_h;

    HPDF_Page_DrawImage(page, img, x_pt, y_pt, target_w, target_h);
    return ctx.last_error.empty();
}

static bool render_filled_rect(HPDF_Page page, Haru_context& ctx,
                               const filled_rect_t& fr, float page_h_mm)
{
    float x = tl_x(fr.x_mm);
    float y = tl_y(fr.y_mm, page_h_mm);
    float w = mm_to_pt(fr.width_mm);
    float h = mm_to_pt(fr.height_mm);

    HPDF_Page_SetRGBFill(page, fr.color.r, fr.color.g, fr.color.b);
    HPDF_Page_Rectangle(page, x, y - h, w, h);
    HPDF_Page_Fill(page);
    return ctx.last_error.empty();
}

static bool render_text_span(HPDF_Page page, Haru_context& ctx,
                             const Text_span& ts, float page_h_mm)
{
    auto font = ctx.font_for(ts.font);
    if (!font) {
        ctx.last_error = "Text span rendering failed: font handle unavailable.";
        return false;
    }

    auto encoded = encode_for_font(font, ts.text);

    HPDF_Page_BeginText(page);
    HPDF_Page_SetFontAndSize(page, font.handle, ts.size_pt);
    HPDF_Page_SetRGBFill(page, ts.color.r, ts.color.g, ts.color.b);
    HPDF_Page_MoveTextPos(page, tl_x(ts.x_mm),
                          tl_y(ts.y_mm, page_h_mm) - ts.size_pt);
    HPDF_Page_ShowText(page, encoded.c_str());
    HPDF_Page_EndText(page);
    return ctx.last_error.empty();
}

static bool save_pdf_stream(Haru_context& ctx, const QString& output_path)
{
    HPDF_STATUS status = HPDF_SaveToStream(ctx.pdf);
    if (status != HPDF_OK || !ctx.last_error.empty()) {
        if (ctx.last_error.empty()) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "HPDF_SaveToStream failed (status 0x%04X)", (unsigned)status);
            ctx.last_error = buf;
        }
        return false;
    }

    QFile file(output_path);
    if (!file.open(QIODevice::WriteOnly)) {
        ctx.last_error = ("Cannot open output file for writing: " + output_path).toStdString();
        return false;
    }

    status = HPDF_ResetStream(ctx.pdf);
    if (status != HPDF_OK) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "HPDF_ResetStream failed (status 0x%04X)", (unsigned)status);
        ctx.last_error = buf;
        return false;
    }

    constexpr HPDF_UINT32 chunk_size = 64 * 1024;
    QByteArray buffer((int)chunk_size, Qt::Uninitialized);

    HPDF_UINT32 remaining = HPDF_GetStreamSize(ctx.pdf);
    while (remaining > 0) {
        HPDF_UINT32 to_read = std::min(remaining, chunk_size);
        status = HPDF_ReadFromStream(ctx.pdf,
                                     reinterpret_cast<HPDF_BYTE*>(buffer.data()),
                                     &to_read);
        if (status != HPDF_OK && status != HPDF_STREAM_EOF) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "HPDF_ReadFromStream failed (status 0x%04X)", (unsigned)status);
            ctx.last_error = buf;
            return false;
        }

        if (to_read == 0 || to_read > remaining) {
            ctx.last_error = "HPDF_ReadFromStream returned an invalid byte count";
            return false;
        }

        if (file.write(buffer.constData(), to_read) != to_read) {
            ctx.last_error = ("Failed to write PDF data to: " + output_path).toStdString();
            return false;
        }

        remaining -= to_read;
        if (status == HPDF_STREAM_EOF && remaining != 0) {
            ctx.last_error = "HPDF_ReadFromStream reached EOF before the full PDF was read";
            return false;
        }
    }

    if (!file.flush()) {
        ctx.last_error = ("Failed to flush PDF data to: " + output_path).toStdString();
        return false;
    }

    return true;
}

static Render_result render_pdf_haru_qstring(const Document& doc, const QString& output_path,
                                             const Font_family_config& fonts,
                                             const Localization& loc)
{
    Haru_context ctx;

    auto fail = [&](const std::string& message, const char* fallback_detail) {
        auto detail = ctx.last_error.empty()
            ? std::string(fallback_detail) : ctx.last_error;
        return Render_result{ false, "", message, detail };
    };

    if (!ctx.init(fonts)) {
        return fail(loc.error_pdf_create_failed, "Failed to initialize libHaru");
    }

    for (const auto& page_def : doc.pages) {
        HPDF_Page page = HPDF_AddPage(ctx.pdf);
        if (!page) {
            return fail(loc.error_pdf_create_failed, "HPDF_AddPage failed");
        }
        HPDF_Page_SetWidth(page, mm_to_pt(doc.page_width_mm));
        HPDF_Page_SetHeight(page, mm_to_pt(doc.page_height_mm));

        for (const auto& elem : page_def.elements) {
            bool ok = std::visit([&](const auto& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, Text_block>)
                    return render_text_block(page, ctx, e, doc.page_height_mm);
                else if constexpr (std::is_same_v<T, line_segment_t>)
                    return render_line_segment(page, ctx, e, doc.page_height_mm);
                else if constexpr (std::is_same_v<T, Image_block>)
                    return render_image_block(page, ctx, e, doc.page_height_mm, loc);
                else if constexpr (std::is_same_v<T, Text_span>)
                    return render_text_span(page, ctx, e, doc.page_height_mm);
                else if constexpr (std::is_same_v<T, filled_rect_t>)
                    return render_filled_rect(page, ctx, e, doc.page_height_mm);
                return false;
            }, elem);
            if (!ok) {
                return fail(loc.error_pdf_create_failed, "PDF rendering failed");
            }
        }
    }

    if (!save_pdf_stream(ctx, output_path)) {
        return fail(loc.error_pdf_save_failed, "");
    }

    return { true, output_path.toUtf8().toStdString(), "", "" };
}

Render_result render_pdf_haru(const Document& doc,
                              const std::string& output_path,
                              const Font_family_config& fonts,
                              const Localization& loc)
{
    return render_pdf_haru_qstring(doc, QString::fromUtf8(output_path.c_str()), fonts, loc);
}
