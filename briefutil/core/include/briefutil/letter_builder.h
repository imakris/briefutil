#pragma once

#include "briefutil/document_model.h"
#include "briefutil/letter_layout_spec.h"
#include "briefutil/localization.h"
#include "briefutil/sender_profile.h"
#include "briefutil/pdf_backend.h"
#include "briefutil/typography_config.h"
#include <string>


// ============================================================================
// Letter builder — converts sender profile + user input into a Document
// ============================================================================

struct Letter_input
{
    std::string recipient;     // multi-line
    std::string subject;
    std::string body;          // multi-line, explicit newlines preserved
    std::string date;          // pre-formatted date string
};

struct Build_letter_result
{
    Document    doc;
    std::string error;   // non-empty if generation failed
};

Build_letter_result build_letter(const Sender_profile& profile,
                                 const Letter_input& input,
                                 const std::string& profile_dir,
                                 const Theme_config& theme = default_theme(),
                                 const Letter_layout_spec& layout = din_5008_form_b(),
                                 const Localization& loc = default_localization(),
                                 Pdf_backend pdf_backend = Pdf_backend::Haru);

// Convenience: build + render in one call.
Render_result generate_letter_pdf(const Sender_profile& profile,
                                  const Letter_input& input,
                                  const std::string& profile_dir,
                                  const std::string& output_path,
                                  const Theme_config& theme = default_theme(),
                                  const Letter_layout_spec& layout = din_5008_form_b(),
                                  const Localization& loc = default_localization(),
                                  Pdf_backend pdf_backend = Pdf_backend::Haru);
