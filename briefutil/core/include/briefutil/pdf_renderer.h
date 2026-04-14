#pragma once

#include "briefutil/document_model.h"
#include "briefutil/localization.h"
#include "briefutil/pdf_backend.h"
#include "briefutil/typography_config.h"

#include <string>


// ============================================================================
// PDF rendering entry points
// ============================================================================

Render_result render_pdf_haru(const Document& doc,
                              const std::string& output_path,
                              const Font_family_config& fonts = default_font_family(),
                              const Localization& loc = default_localization());

Render_result render_pdf_mark2haru(const Document& doc,
                                   const std::string& output_path,
                                   const Font_family_config& fonts = default_font_family(),
                                   const Localization& loc = default_localization());

Render_result render_pdf(const Document& doc,
                         const std::string& output_path,
                         const Font_family_config& fonts = default_font_family(),
                         const Localization& loc = default_localization(),
                         Pdf_backend backend = Pdf_backend::Haru);

