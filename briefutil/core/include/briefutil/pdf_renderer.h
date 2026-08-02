#pragma once

#include "briefutil/document_model.h"
#include "briefutil/localization.h"
#include "briefutil/pdf_measurement.h"
#include "briefutil/typography_config.h"

#include <string>


// ============================================================================
// PDF rendering entry point
// ============================================================================

// `measurement` must be the one the document was laid out against, so the
// glyphs are drawn with the metrics that positioned them. build_letter hands
// its own back for exactly this.
Render_result render_pdf(
    const Document&            doc,
    const std::string&         output_path,
    const Pdf_measurement&     measurement,
    const Localization&        loc = default_localization());
