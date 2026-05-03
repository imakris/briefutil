#pragma once

#include "briefutil/document_model.h"
#include "briefutil/localization.h"
#include "briefutil/typography_config.h"

#include <string>


// ============================================================================
// PDF rendering entry point
// ============================================================================

Render_result render_pdf(
    const Document& doc,
    const std::string& output_path,
    const Font_family_config& fonts = default_font_family(),
    const Localization& loc = default_localization());
