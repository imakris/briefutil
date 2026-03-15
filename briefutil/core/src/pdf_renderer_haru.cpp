#include "briefutil/pdf_renderer_haru.h"

#include <hpdf.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>


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
        }
        else
        if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            codepoint = ((unsigned)(c & 0x1F) << 6)
                | (unsigned)((unsigned char)text[i + 1] & 0x3F);
            advance = 2;
        }
        else
        if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            codepoint = ((unsigned)(c & 0x0F) << 12)
                | ((unsigned)((unsigned char)text[i + 1] & 0x3F) << 6)
                | (unsigned)((unsigned char)text[i + 2] & 0x3F);
            advance = 3;
        }
        else
        if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            codepoint = ((unsigned)(c & 0x07) << 18)
                | ((unsigned)((unsigned char)text[i + 1] & 0x3F) << 12)
                | ((unsigned)((unsigned char)text[i + 2] & 0x3F) << 6)
                | (unsigned)((unsigned char)text[i + 3] & 0x3F);
            advance = 4;
        }
        else {
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
    HPDF_Font sans_italic = nullptr;
    HPDF_Font sans_bold_italic = nullptr;
    HPDF_Font mono = nullptr;

    bool init(const Font_family_config& fc = default_font_family())
    {
        last_error.clear();
        pdf = HPDF_New(error_handler, this);
        if (!pdf) return false;
        HPDF_SetCurrentEncoder(pdf, "WinAnsiEncoding");
        HPDF_UseUTFEncodings(pdf);

        auto load_font = [&](const std::string& value) -> HPDF_Font {
            if (value.empty()) return nullptr;
            if (looks_like_font_file(value)) {
                const char* name = HPDF_LoadTTFontFromFile(pdf, value.c_str(), HPDF_TRUE);
                if (!name) return nullptr;
                return HPDF_GetFont(pdf, name, "WinAnsiEncoding");
            }
            return HPDF_GetFont(pdf, value.c_str(), "WinAnsiEncoding");
        };

        sans             = load_font(fc.sans);
        sans_bold        = load_font(fc.sans_bold);
        sans_italic      = load_font(fc.sans_italic);
        sans_bold_italic = load_font(fc.sans_bold_italic);
        mono             = load_font(fc.mono);
        return sans && sans_bold && sans_italic && sans_bold_italic && mono;
    }

    void destroy()
    {
        if (pdf) {
            HPDF_Free(pdf);
            pdf = nullptr;
        }
        sans = nullptr;
        sans_bold = nullptr;
        sans_italic = nullptr;
        sans_bold_italic = nullptr;
        mono = nullptr;
    }

    HPDF_Font font_for(Font_id id) const
    {
        switch (id) {
            case Font_id::SANS_BOLD:        return sans_bold;
            case Font_id::SANS_ITALIC:      return sans_italic;
            case Font_id::SANS_BOLD_ITALIC: return sans_bold_italic;
            case Font_id::MONO:             return mono;
            default:                        return sans;
        }
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
        return pdf && sans && sans_bold && sans_italic && sans_bold_italic && mono;
    }
};


// ============================================================================
// Text wrapping (greedy, with explicit newline support)
// ============================================================================

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
            }
            else {
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

// Measurement context — reinitializes when the font config changes.
static Haru_context& get_measure_context(const Font_family_config& fc = default_font_family())
{
    static Haru_context ctx;
    static Font_family_config current_fc;
    bool need_init = !ctx.ready();
    if (!need_init && fc != current_fc) {
        ctx.destroy();
        need_init = true;
    }
    if (need_init) {
        ctx.init(fc);
        current_fc = fc;
    }
    return ctx;
}

text_metrics_t measure_text(const std::string& text, Font_id font_id,
                          float size_pt, float leading_pt,
                          float max_width_mm, bool wrap,
                          const Font_family_config& fonts)
{
    auto& ctx = get_measure_context(fonts);
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
    }
    else {
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
                                   float size_pt, float max_width_mm,
                                   const Font_family_config& fonts)
{
    auto& ctx = get_measure_context(fonts);
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
// Image measurement
// ============================================================================

image_dimensions_t measure_png(const std::string& path)
{
    // Use a temporary context to avoid accumulating image objects
    // in the long-lived measurement context.
    Haru_context tmp;
    if (!tmp.init()) return {};

    HPDF_Image img = HPDF_LoadPngImageFromFile(tmp.pdf, path.c_str());
    if (!img) {
        tmp.destroy();
        return {};
    }

    image_dimensions_t dims = {
        (float)HPDF_Image_GetWidth(img),
        (float)HPDF_Image_GetHeight(img),
        true
    };
    tmp.destroy();
    return dims;
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
    }
    else {
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
                               const Image_block& ib, float page_h_mm)
{
    HPDF_Image img = HPDF_LoadPngImageFromFile(ctx.pdf, ib.path.c_str());
    if (!img) {
        // Image could not be loaded. Reset libHaru's error state and render
        // a visible placeholder so the user knows an image is missing.
        ctx.last_error.clear();
        HPDF_ResetError(ctx.pdf);

        std::string placeholder = "[Bild nicht gefunden: " + ib.path + "]";
        auto encoded = utf8_to_win_ansi(placeholder);
        HPDF_Font font = ctx.font_for(Font_id::SANS_ITALIC);
        if (font) {
            float x_pt = tl_x(ib.x_mm);
            float y_pt = tl_y(ib.y_mm, page_h_mm) - 8.0f;
            HPDF_Page_BeginText(page);
            HPDF_Page_SetFontAndSize(page, font, 8.0f);
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
    HPDF_Font font = ctx.font_for(ts.font);
    if (!font) {
        ctx.last_error = "Text span rendering failed: font handle unavailable.";
        return false;
    }

    auto encoded = utf8_to_win_ansi(ts.text);

    HPDF_Page_BeginText(page);
    HPDF_Page_SetFontAndSize(page, font, ts.size_pt);
    HPDF_Page_SetRGBFill(page, ts.color.r, ts.color.g, ts.color.b);
    HPDF_Page_MoveTextPos(page, tl_x(ts.x_mm),
                          tl_y(ts.y_mm, page_h_mm) - ts.size_pt);
    HPDF_Page_ShowText(page, encoded.c_str());
    HPDF_Page_EndText(page);
    return ctx.last_error.empty();
}


// ============================================================================
// Public render entry point
// ============================================================================

Render_result render_pdf(const Document& doc, const std::string& output_path,
                         const Font_family_config& fonts)
{
    Haru_context ctx;

    auto fail = [&](const char* message, const char* fallback_detail) {
        auto detail = ctx.last_error.empty()
            ? std::string(fallback_detail) : ctx.last_error;
        ctx.destroy();
        return Render_result{ false, "", message, detail };
    };

    if (!ctx.init(fonts)) {
        return fail("PDF-Erstellung fehlgeschlagen.", "Failed to initialize libHaru");
    }

    for (const auto& page_def : doc.pages) {
        HPDF_Page page = HPDF_AddPage(ctx.pdf);
        if (!page) {
            return fail("PDF-Erstellung fehlgeschlagen.", "HPDF_AddPage failed");
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
                    return render_image_block(page, ctx, e, doc.page_height_mm);
                else if constexpr (std::is_same_v<T, Text_span>)
                    return render_text_span(page, ctx, e, doc.page_height_mm);
                else if constexpr (std::is_same_v<T, filled_rect_t>)
                    return render_filled_rect(page, ctx, e, doc.page_height_mm);
                return false;
            }, elem);
            if (!ok) {
                return fail("PDF-Erstellung fehlgeschlagen.", "PDF rendering failed");
            }
        }
    }

    HPDF_STATUS status = HPDF_SaveToFile(ctx.pdf, output_path.c_str());
    if (status != HPDF_OK || !ctx.last_error.empty()) {
        if (ctx.last_error.empty()) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "HPDF_SaveToFile failed (status 0x%04X)", (unsigned)status);
            ctx.last_error = buf;
        }
        return fail("PDF konnte nicht gespeichert werden.", "");
    }
    ctx.destroy();

    return { true, output_path, "", "" };
}
