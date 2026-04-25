#include "pdf_haru_support.h"

#include <cstdio>


// ============================================================================
// UTF-8 -> WinAnsi conversion
// ============================================================================

bool append_win_ansi_byte(std::string& out, unsigned codepoint)
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
        case 0x20AC: mapped = 0x80; break;
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

std::string utf8_to_win_ansi(const std::string& text)
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
// Haru_context
// ============================================================================

Haru_context::~Haru_context()
{
    destroy();
}

bool Haru_context::init(const font_family_config_t& fc)
{
    last_error.clear();
    current_fc = fc;
    pdf = HPDF_New(error_handler, this);
    if (!pdf) return false;
    HPDF_UseUTFEncodings(pdf);

    auto load_font = [&](const std::string& value) -> font_handle_t {
        font_handle_t out;
        if (value.empty()) return out;
        if (looks_like_font_file(value)) {
            const char* name = HPDF_LoadTTFontFromFile(pdf, value.c_str(), HPDF_TRUE);
            if (!name) return out;
            HPDF_Font f = HPDF_GetFont(pdf, name, "UTF-8");
            if (!f) {
                last_error.clear();
                HPDF_ResetError(pdf);
                f = HPDF_GetFont(pdf, name, "WinAnsiEncoding");
                if (!f) return out;
                out.handle = f;
                out.utf8 = false;
                return out;
            }
            out.handle = f;
            out.utf8 = true;
            return out;
        }
        HPDF_Font f = HPDF_GetFont(pdf, value.c_str(), "WinAnsiEncoding");
        if (!f) return out;
        out.handle = f;
        out.utf8 = false;
        return out;
    };

    fonts[(int)Font_id::SANS]             = load_font(fc.sans);
    fonts[(int)Font_id::SANS_BOLD]        = load_font(fc.sans_bold);
    fonts[(int)Font_id::SANS_ITALIC]      = load_font(fc.sans_italic);
    fonts[(int)Font_id::SANS_BOLD_ITALIC] = load_font(fc.sans_bold_italic);
    fonts[(int)Font_id::MONO]             = load_font(fc.mono);

    for (auto& f : fonts) {
        if (!f) return false;
    }
    return true;
}

void Haru_context::destroy()
{
    if (pdf) {
        HPDF_Free(pdf);
        pdf = nullptr;
    }
    for (auto& f : fonts) {
        f = {};
    }
}

bool Haru_context::ready() const
{
    if (!pdf) return false;
    for (auto& f : fonts) {
        if (!f) return false;
    }
    return true;
}

void HPDF_STDCALL Haru_context::error_handler(
    HPDF_STATUS error_no,
    HPDF_STATUS detail_no,
    void* user_data)
{
    auto* ctx = static_cast<Haru_context*>(user_data);
    char buf[128];
    std::snprintf(
        buf,
        sizeof(buf),
        "libHaru error 0x%04X (detail %u)",
        (unsigned)error_no,
        (unsigned)detail_no);
    if (ctx) ctx->last_error = buf;
}


// ============================================================================
// Text measurement and wrapping
// ============================================================================

float font_text_width_pt(
    const font_handle_t& font,
    float size_pt,
    const std::string& text)
{
    if (!font) return 0;
    auto encoded = encode_for_font(font, text);
    auto tw = HPDF_Font_TextWidth(
        font.handle,
        (const HPDF_BYTE*)encoded.c_str(),
        (HPDF_UINT)encoded.size());
    return tw.width * size_pt / 1000.0f;
}

std::vector<std::string> do_wrap(
    const font_handle_t& font,
    float size_pt,
    float max_width_pt,
    const std::string& text)
{
    std::vector<std::string> result;

    for (const auto& para : split_lines(text)) {
        if (para.empty()) {
            result.push_back("");
            continue;
        }

        std::string current;
        for_each_word(para, [&](const std::string& word) {
            std::string candidate = current.empty() ? word : current + " " + word;
            float w = font_text_width_pt(font, size_pt, candidate);
            if (w > max_width_pt && !current.empty()) {
                result.push_back(current);
                current = word;
            }
            else {
                current = candidate;
            }
        });
        if (!current.empty()) {
            result.push_back(current);
        }
    }

    return result;
}
