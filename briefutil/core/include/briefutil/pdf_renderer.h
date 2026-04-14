#pragma once

#include "briefutil/document_model.h"
#include "briefutil/localization.h"
#include "briefutil/pdf_backend.h"
#include "briefutil/typography_config.h"

#include <string>


// ============================================================================
// PDF rendering entry points
// ============================================================================

render_result_t render_pdf_haru(
    const document_t& doc,
    const std::string& output_path,
    const font_family_config_t& fonts = default_font_family(),
    const localization_t& loc = default_localization());

render_result_t render_pdf_mark2haru(
    const document_t& doc,
    const std::string& output_path,
    const font_family_config_t& fonts = default_font_family(),
    const localization_t& loc = default_localization());

render_result_t render_pdf(
    const document_t& doc,
    const std::string& output_path,
    const font_family_config_t& fonts = default_font_family(),
    const localization_t& loc = default_localization(),
    Pdf_backend backend = Pdf_backend::Haru);
