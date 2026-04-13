#pragma once

#include <string>


// ============================================================================
// Font family configuration
// ============================================================================

inline bool looks_like_font_file(const std::string& s)
{
    if (s.size() < 4) return false;
    auto eq_ci = [&](size_t at, const char* lit) {
        for (size_t i = 0; i < 4; i++) {
            char a = s[at + i];
            if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
            if (a != lit[i]) return false;
        }
        return true;
    };
    size_t at = s.size() - 4;
    return eq_ci(at, ".ttf") || eq_ci(at, ".otf");
}

struct Font_family_config
{
    // Each slot holds either a libHaru base-14 name (e.g. "Helvetica")
    // or a .ttf/.otf file path. The renderer infers the loading method
    // per slot via looks_like_font_file().
    std::string sans;
    std::string sans_bold;
    std::string sans_italic;
    std::string sans_bold_italic;
    std::string mono;

    bool operator==(const Font_family_config&) const = default;
};

inline Font_family_config default_font_family()
{
    return {
        "Helvetica",
        "Helvetica-Bold",
        "Helvetica-Oblique",
        "Helvetica-BoldOblique",
        "Courier",
    };
}


// ============================================================================
// Typography configuration
// ============================================================================

struct Typography_config
{
    // Body text
    float body_size_pt    = 10.0f;
    float body_lead_pt    = 12.0f;

    // Heading scale factors (relative to body_size_pt)
    float heading1_scale  = 1.6f;
    float heading2_scale  = 1.3f;
    float heading3_scale  = 1.1f;

    // Code block scale (relative to body_size_pt)
    float code_scale      = 0.85f;

    // Letter chrome sizes
    float sender_size_pt  = 10.0f;
    float sender_lead_pt  = 12.0f;
    float return_size_pt  = 8.0f;
    float recip_size_pt   = 10.0f;
    float recip_lead_pt   = 12.0f;
    float date_size_pt    = 10.0f;
    float footer_size_pt  = 9.0f;
    float footer_text_size_pt = 8.0f;
    // Spacing
    float heading_space_after_mm = 2.0f;
    float paragraph_space_mm     = 3.0f;
    float list_indent_mm         = 8.0f;
    float list_item_space_mm     = 1.0f;
    float table_cell_pad_mm      = 2.0f;
};

inline Typography_config default_typography() { return {}; }


// ============================================================================
// Combined theme
// ============================================================================

struct Theme_config
{
    Font_family_config fonts = default_font_family();
    Typography_config  typo;
};

inline Theme_config default_theme() { return {}; }
