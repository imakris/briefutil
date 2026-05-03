#pragma once

#include "briefutil/document_model.h"
#include "briefutil/localization.h"
#include "briefutil/typography_config.h"

#include <string>


// ============================================================================
// PDF rendering entry point
// ============================================================================

render_result_t render_pdf(
    const document_t& doc,
    const std::string& output_path,
    const font_family_config_t& fonts = default_font_family(),
    const localization_t& loc = default_localization());
