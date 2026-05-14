#pragma once

#include <string>


// ============================================================================
// Localization strings the core writes into letters and error messages
//
// Defaults are English. Use german_localization() for the previous
// German-language defaults, or fill the struct yourself for any other
// language. All fields are plain UTF-8 strings.
//
// Tokens used in format strings:
//   {current}, {total}    in page_number_format
//   {path}                in image_not_found_format and error_pdf_open_failed_format
// ============================================================================

struct Localization
{
    // Closing line above the signer name (e.g. "Sincerely,").
    std::string    closing                 = "Sincerely,";

    // Page footer template for multi-page letters.
    // Tokens: {current}, {total}.
    std::string    page_number_format      = "Page {current} of {total}";

    // Placeholder text rendered when an embedded image cannot be loaded.
    // Tokens: {path}.
    std::string    image_not_found_format  = "[Image not found: {path}]";

    // User-facing error strings returned in Render_result::message.
    std::string    error_pdf_create_failed = "Failed to create the PDF.";
    std::string    error_pdf_save_failed   = "Failed to save the PDF.";
    std::string    error_table_too_wide    = "A table is too wide for the available page area.";
    std::string error_pdf_open_failed_format =
        "The PDF was created but could not be opened automatically: {path}";
};


// ============================================================================
// String substitution helpers
// ============================================================================

inline std::string format_replace(
    std::string        s,
    const std::string& token,
    const std::string& value)
{
    size_t pos = 0;
    while ((pos = s.find(token, pos)) != std::string::npos) {
        s.replace(pos, token.size(), value);
        pos += value.size();
    }
    return s;
}

inline std::string format_page_number(
    const std::string& format_template,
    int                current,
    int                total)
{
    auto out = format_replace(format_template, "{current}", std::to_string(current));
    return format_replace(out, "{total}", std::to_string(total));
}

inline std::string format_image_not_found(
    const std::string& format_template,
    const std::string& path)
{
    return format_replace(format_template, "{path}", path);
}

inline std::string format_pdf_open_failed(
    const std::string& format_template,
    const std::string& path)
{
    return format_replace(format_template, "{path}", path);
}


// ============================================================================
// Built-in presets
// ============================================================================

inline Localization default_localization() { return {}; }

inline Localization english_localization() { return {}; }

inline Localization german_localization()
{
    Localization L;
    // UTF-8: ü = \xc3\xbc, ß = \xc3\x9f
    L.closing                 = "Mit freundlichen Gr\xc3\xbc\xc3\x9f" "en";
    L.page_number_format      = "Seite {current} von {total}";
    L.image_not_found_format  = "[Bild nicht gefunden: {path}]";
    L.error_pdf_create_failed = "PDF-Erstellung fehlgeschlagen.";
    L.error_pdf_save_failed   = "PDF konnte nicht gespeichert werden.";
    L.error_table_too_wide    = "Eine Tabelle ist zu breit f\xc3\xbcr den "
                                "verf\xc3\xbcgbaren Seitenbereich.";
    L.error_pdf_open_failed_format =
        "PDF wurde erstellt, konnte aber nicht automatisch ge\xc3\xb6" "ffnet "
        "werden: {path}";
    return L;
}
