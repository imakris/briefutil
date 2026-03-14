#include "pdf_renderer_haru.h"

#include <hpdf.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>


// ============================================================================
// Unit conversion
// ============================================================================

static constexpr float k_points_per_mm = 72.0f / 25.4f;

static float mm_to_pt(float mm) { return mm * k_points_per_mm; }

static bool append_win_ansi_byte(std::string& out, unsigned codepoint)
{
    if (codepoint <= 0x7F) {
        out.push_back((char)codepoint);
        return true;
    }

    if (codepoint >= 0xA0 && codepoint <= 0xFF) {
        out.push_back((char)codepoint);
        return true;
    }

    unsigned char mapped = 0;
    switch (codepoint) {
    case 0x20AC: mapped = 0x80; break; // Euro
    case 0x201A: mapped = 0x82; break;
    case 0x0192: mapped = 0x83; break;
    case 0x201E: mapped = 0x84; break;
    case 0x2026: mapped = 0x85; break;
    case 0x2020: mapped = 0x86; break;
    case 0x2021: mapped = 0x87; break;
    case 0x02C6: mapped = 0x88; break;
    case 0x2030: mapped = 0x89; break;
    case 0x0160: mapped = 0x8A; break;
    case 0x2039: mapped = 0x8B; break;
    case 0x0152: mapped = 0x8C; break;
    case 0x017D: mapped = 0x8E; break;
    case 0x2018: mapped = 0x91; break;
    case 0x2019: mapped = 0x92; break;
    case 0x201C: mapped = 0x93; break;
    case 0x201D: mapped = 0x94; break;
    case 0x2022: mapped = 0x95; break;
    case 0x2013: mapped = 0x96; break;
    case 0x2014: mapped = 0x97; break;
    case 0x02DC: mapped = 0x98; break;
    case 0x2122: mapped = 0x99; break;
    case 0x0161: mapped = 0x9A; break;
    case 0x203A: mapped = 0x9B; break;
    case 0x0153: mapped = 0x9C; break;
    case 0x017E: mapped = 0x9E; break;
    case 0x0178: mapped = 0x9F; break;
    default:
        return false;
    }

    out.push_back((char)mapped);
    return true;
}

static std::string utf8_to_win_ansi(const std::string& text)
{
    std::string out;
    out.reserve(text.size());

    for (size_t i = 0; i < text.size(); ) {
        unsigned char c = (unsigned char)text[i];
        unsigned codepoint = 0;
        size_t advance = 1;

        if (c < 0x80) {
            codepoint = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            codepoint = ((unsigned)(c & 0x1F) << 6)
                | (unsigned)((unsigned char)text[i + 1] & 0x3F);
            advance = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            codepoint = ((unsigned)(c & 0x0F) << 12)
                | ((unsigned)((unsigned char)text[i + 1] & 0x3F) << 6)
                | (unsigned)((unsigned char)text[i + 2] & 0x3F);
            advance = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            codepoint = ((unsigned)(c & 0x07) << 18)
                | ((unsigned)((unsigned char)text[i + 1] & 0x3F) << 12)
                | ((unsigned)((unsigned char)text[i + 2] & 0x3F) << 6)
                | (unsigned)((unsigned char)text[i + 3] & 0x3F);
            advance = 4;
        } else {
            codepoint = '?';
        }

        if (!append_win_ansi_byte(out, codepoint)) {
            out.push_back('?');
        }
        i += advance;
    }

    return out;
}


// ============================================================================
// Shared PDF context for font access in measurement and rendering
// ============================================================================

struct Haru_context
{
    HPDF_Doc  pdf  = nullptr;
    HPDF_Font sans = nullptr;
    HPDF_Font sans_bold = nullptr;

    bool init()
    {
        last_error.clear();
        pdf = HPDF_New(error_handler, this);
        if (!pdf) return false;
        HPDF_SetCurrentEncoder(pdf, "WinAnsiEncoding");
        HPDF_UseUTFEncodings(pdf);
        sans      = HPDF_GetFont(pdf, "Helvetica",      "WinAnsiEncoding");
        sans_bold = HPDF_GetFont(pdf, "Helvetica-Bold",  "WinAnsiEncoding");
        return sans && sans_bold;
    }

    void destroy()
    {
        if (pdf) {
            HPDF_Free(pdf);
            pdf = nullptr;
        }
        sans = nullptr;
        sans_bold = nullptr;
    }

    HPDF_Font font_for(Font_id id) const
    {
        return id == Font_id::sans_bold ? sans_bold : sans;
    }

    std::string last_error;

    static void HPDF_STDCALL error_handler(HPDF_STATUS error_no,
                                           HPDF_STATUS detail_no,
                                           void* user_data)
    {
        auto* ctx = static_cast<Haru_context*>(user_data);
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "libHaru error 0x%04X (detail %u)",
                      (unsigned)error_no, (unsigned)detail_no);
        if (ctx) ctx->last_error = buf;
    }

    bool ready() const
    {
        return pdf && sans && sans_bold;
    }
};


// ============================================================================
// Text wrapping (greedy, with explicit newline support)
// ============================================================================

static std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            lines.push_back(text.substr(pos));
            break;
        }
        lines.push_back(text.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return lines;
}

static float measure_string_width(HPDF_Font font, float size_pt,
                                  const char* str, size_t len)
{
    auto tw = HPDF_Font_TextWidth(font, (const HPDF_BYTE*)str, (HPDF_UINT)len);
    return tw.width * size_pt / 1000.0f;
}

static float measure_text_width_utf8(HPDF_Font font, float size_pt,
                                     const std::string& text)
{
    auto encoded = utf8_to_win_ansi(text);
    return measure_string_width(font, size_pt, encoded.c_str(), encoded.size());
}

static std::vector<std::string> do_wrap(HPDF_Font font, float size_pt,
                                        float max_width_pt,
                                        const std::string& text)
{
    std::vector<std::string> result;

    for (const auto& para : split_lines(text)) {
        if (para.empty()) {
            result.push_back("");
            continue;
        }

        // Split into words
        std::vector<std::string> words;
        size_t pos = 0;
        while (pos < para.size()) {
            while (pos < para.size() && para[pos] == ' ') pos++;
            if (pos >= para.size()) break;
            size_t end = para.find(' ', pos);
            if (end == std::string::npos) end = para.size();
            words.push_back(para.substr(pos, end - pos));
            pos = end;
        }

        std::string current;
        for (const auto& word : words) {
            std::string candidate = current.empty()
                ? word : current + " " + word;
            float w = measure_text_width_utf8(font, size_pt, candidate);
            if (w > max_width_pt && !current.empty()) {
                result.push_back(current);
                current = word;
            } else {
                current = candidate;
            }
        }
        if (!current.empty()) {
            result.push_back(current);
        }
    }

    return result;
}


// ============================================================================
// Public text measurement
// ============================================================================

// Thread-local measurement context — avoids creating a PDF doc per call
static Haru_context& get_measure_context()
{
    static Haru_context ctx;
    if (!ctx.ready()) {
        ctx.init();
    }
    return ctx;
}

Text_metrics measure_text(const std::string& text, Font_id font_id,
                          float size_pt, float leading_pt,
                          float max_width_mm, bool wrap)
{
    auto& ctx = get_measure_context();
    if (!ctx.ready()) {
        return {};
    }
    HPDF_Font font = ctx.font_for(font_id);
    if (!font) {
        return {};
    }
    float lead = leading_pt > 0 ? leading_pt : size_pt;
    float max_width_pt = mm_to_pt(max_width_mm);

    std::vector<std::string> lines;
    if (wrap) {
        lines = do_wrap(font, size_pt, max_width_pt, text);
    } else {
        lines = split_lines(text);
    }

    float max_w = 0;
    for (const auto& line : lines) {
        float w = measure_text_width_utf8(font, size_pt, line);
        max_w = std::max(max_w, w);
    }

    int n = (int)lines.size();
    float height = n > 0 ? size_pt + (float)(n - 1) * lead : 0;

    return { max_w, height, n };
}

std::vector<std::string> wrap_text(const std::string& text, Font_id font_id,
                                   float size_pt, float max_width_mm)
{
    auto& ctx = get_measure_context();
    if (!ctx.ready()) {
        return {};
    }
    HPDF_Font font = ctx.font_for(font_id);
    if (!font) {
        return {};
    }
    // Return UTF-8 lines. Width measurement will be approximate for non-ASCII
    // (multi-byte chars measured as multiple glyphs), but wrap boundaries are
    // correct since breaks only happen at spaces. The render path converts to
    // Latin-1 once at the final rendering step.
    return do_wrap(font, size_pt, mm_to_pt(max_width_mm), text);
}


// ============================================================================
// Renderer — draws a Document to PDF
// ============================================================================

// Convert top-left mm to bottom-left points
static float tl_x(float x_mm) { return mm_to_pt(x_mm); }

static float tl_y(float y_mm, float page_height_mm)
{
    return mm_to_pt(page_height_mm - y_mm);
}

static bool render_text_block(HPDF_Page page, Haru_context& ctx,
                              const Text_block& tb, float page_h_mm)
{
    HPDF_Font font = ctx.font_for(tb.font);
    if (!font) {
        ctx.last_error = "Text rendering failed: font handle is unavailable.";
        return false;
    }
    float lead = tb.leading_pt > 0 ? tb.leading_pt : tb.size_pt;

    std::vector<std::string> lines;
    if (tb.wrap) {
        lines = do_wrap(font, tb.size_pt, mm_to_pt(tb.width_mm), tb.text);
    } else {
        lines = split_lines(tb.text);
    }

    if (lines.empty()) return true;

    float x_pt = tl_x(tb.x_mm);
    float y_pt = tl_y(tb.y_mm, page_h_mm) - tb.size_pt;

    HPDF_Page_BeginText(page);
    HPDF_Page_SetFontAndSize(page, font, tb.size_pt);
    HPDF_Page_SetRGBFill(page, tb.color.r, tb.color.g, tb.color.b);
    HPDF_Page_SetTextLeading(page, lead);
    HPDF_Page_MoveTextPos(page, x_pt, y_pt);

    for (size_t i = 0; i < lines.size(); i++) {
        if (i > 0) {
            HPDF_Page_MoveTextPos(page, 0, -lead);
        }
        auto encoded_line = utf8_to_win_ansi(lines[i]);
        HPDF_Page_ShowText(page, encoded_line.c_str());
    }

    HPDF_Page_EndText(page);
    return ctx.last_error.empty();
}

static bool render_line_segment(HPDF_Page page, Haru_context& ctx,
                                const Line_segment& ls, float page_h_mm)
{
    HPDF_Page_SetLineWidth(page, ls.stroke_width_pt);
    HPDF_Page_SetRGBStroke(page, ls.color.r, ls.color.g, ls.color.b);
    HPDF_Page_MoveTo(page, tl_x(ls.x1_mm), tl_y(ls.y1_mm, page_h_mm));
    HPDF_Page_LineTo(page, tl_x(ls.x2_mm), tl_y(ls.y2_mm, page_h_mm));
    HPDF_Page_Stroke(page);
    return ctx.last_error.empty();
}

static bool render_image_block(HPDF_Page page, Haru_context& ctx,
                               const Image_block& ib, float page_h_mm)
{
    HPDF_Image img = HPDF_LoadPngImageFromFile(ctx.pdf, ib.path.c_str());
    if (!img) {
        if (ctx.last_error.empty()) {
            ctx.last_error = "Image rendering failed: could not load PNG \"" +
                             ib.path + "\".";
        }
        return false;
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


// ============================================================================
// Public render entry point
// ============================================================================

Render_result render_pdf(const Document& doc, const std::string& output_path)
{
    Haru_context ctx;
    if (!ctx.init()) {
        return { false, "", "PDF-Erstellung fehlgeschlagen.",
                 ctx.last_error.empty()
                     ? "Failed to initialize libHaru"
                     : ctx.last_error };
    }

    for (const auto& page_def : doc.pages) {
        HPDF_Page page = HPDF_AddPage(ctx.pdf);
        if (!page) {
            auto detail = ctx.last_error.empty()
                ? std::string("HPDF_AddPage failed")
                : ctx.last_error;
            ctx.destroy();
            return { false, "", "PDF-Erstellung fehlgeschlagen.", detail };
        }
        HPDF_Page_SetWidth(page, mm_to_pt(doc.page_width_mm));
        HPDF_Page_SetHeight(page, mm_to_pt(doc.page_height_mm));

        for (const auto& elem : page_def.elements) {
            bool ok = std::visit([&](const auto& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, Text_block>) {
                    return render_text_block(page, ctx, e, doc.page_height_mm);
                } else if constexpr (std::is_same_v<T, Line_segment>) {
                    return render_line_segment(page, ctx, e, doc.page_height_mm);
                } else if constexpr (std::is_same_v<T, Image_block>) {
                    return render_image_block(page, ctx, e, doc.page_height_mm);
                }
                return false;
            }, elem);
            if (!ok) {
                auto detail = ctx.last_error.empty()
                    ? std::string("PDF rendering failed")
                    : ctx.last_error;
                ctx.destroy();
                return { false, "", "PDF-Erstellung fehlgeschlagen.", detail };
            }
        }
    }

    HPDF_STATUS status = HPDF_SaveToFile(ctx.pdf, output_path.c_str());
    ctx.destroy();

    if (status != HPDF_OK || !ctx.last_error.empty()) {
        std::string detail = ctx.last_error;
        if (detail.empty()) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "HPDF_SaveToFile failed (status 0x%04X)", (unsigned)status);
            detail = buf;
        }
        return { false, "", "PDF konnte nicht gespeichert werden.", detail };
    }

    return { true, output_path, "", "" };
}
