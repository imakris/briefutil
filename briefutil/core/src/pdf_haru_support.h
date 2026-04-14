#pragma once

#include "briefutil/document_model.h"
#include "briefutil/typography_config.h"

#include <hpdf.h>

#include <string>
#include <vector>


// ============================================================================
// Shared libHaru font helpers
//
// These are used by pdf_measurement.cpp (for width measurement without ever
// writing a PDF) and by pdf_renderer_haru.cpp (for the actual rendering).
// The implementations live in pdf_haru_support.cpp so each TU doesn't carry
// its own private copy.
// ============================================================================

// Map one Unicode codepoint to its WinAnsi (CP1252) byte, appending to `out`.
// Returns false if the codepoint has no WinAnsi equivalent.
bool append_win_ansi_byte(std::string& out, unsigned codepoint);

// Convert a UTF-8 string to a WinAnsi (CP1252) byte string. Unrepresentable
// codepoints become '?'.
std::string utf8_to_win_ansi(const std::string& text);

struct font_handle_t
{
    HPDF_Font handle = nullptr;
    bool      utf8   = false;

    explicit operator bool() const { return handle != nullptr; }
};

// Encode `text` so it can be passed to HPDF_Page_ShowText for the given font.
inline std::string encode_for_font(const font_handle_t& f, const std::string& text)
{
    return f.utf8 ? text : utf8_to_win_ansi(text);
}

struct Haru_context
{
    HPDF_Doc      pdf = nullptr;
    font_handle_t fonts[5] = {};
    font_family_config_t current_fc = default_font_family();
    std::string   last_error;

    ~Haru_context();

    bool init(const font_family_config_t& fc = default_font_family());
    void destroy();

    font_handle_t font_for(Font_id id) const { return fonts[(int)id]; }
    bool ready() const;

    static void HPDF_STDCALL error_handler(
        HPDF_STATUS error_no,
        HPDF_STATUS detail_no,
        void* user_data);
};

// Text width in points for the given font and size.
float font_text_width_pt(
    const font_handle_t& font,
    float size_pt,
    const std::string& text);

// Greedy word-wrap that returns one line per wrapped segment. Explicit
// newlines in `text` force line breaks.
std::vector<std::string> do_wrap(
    const font_handle_t& font,
    float size_pt,
    float max_width_pt,
    const std::string& text);
