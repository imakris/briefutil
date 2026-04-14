#pragma once

#include "briefutil/document_model.h"
#include "briefutil/letter_layout_spec.h"
#include "briefutil/localization.h"
#include "briefutil/sender_profile.h"
#include "briefutil/pdf_backend.h"
#include "briefutil/typography_config.h"
#include <string>


// ============================================================================
// Letter builder - converts sender profile + user input into a document_t
// ============================================================================

struct letter_input_t
{
    std::string recipient;     // multi-line
    std::string subject;
    std::string body;          // multi-line, explicit newlines preserved
    std::string date;          // pre-formatted date string
};

struct build_letter_result_t
{
    document_t    doc;
    std::string error;   // non-empty if generation failed
};

build_letter_result_t build_letter(
    const sender_profile_t& profile,
    const letter_input_t& input,
    const std::string& profile_dir,
    const theme_config_t& theme = default_theme(),
    const letter_layout_spec_t& layout = din_5008_form_b(),
    const localization_t& loc = default_localization(),
    Pdf_backend pdf_backend = Pdf_backend::Haru);

// Convenience: build + render in one call.
render_result_t generate_letter_pdf(
    const sender_profile_t& profile,
    const letter_input_t& input,
    const std::string& profile_dir,
    const std::string& output_path,
    const theme_config_t& theme = default_theme(),
    const letter_layout_spec_t& layout = din_5008_form_b(),
    const localization_t& loc = default_localization(),
    Pdf_backend pdf_backend = Pdf_backend::Haru);
