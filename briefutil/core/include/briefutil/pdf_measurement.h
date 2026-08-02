#pragma once

#include "briefutil/document_model.h"
#include "briefutil/typography_config.h"

#include <memory>
#include <string>
#include <vector>

namespace mark2haru { class Measurement_context; }


// ============================================================================
// PDF measurement utilities
// ============================================================================

struct text_metrics_t
{
    float  width_pt   = 0;
    float  height_pt  = 0; // total height including all wrapped lines
    int    line_count = 0;
};

struct image_dimensions_t
{
    float  width_px   = 0;
    float  height_px  = 0;
    bool   valid      = false;
};

// The loaded font metrics one document is measured and drawn against.
//
// Constructing this reads the five font files of the family, so a document
// builds one and passes it along; nothing derives its own metrics part of the
// way through. That is also what keeps the family used to place text and the
// family used to draw it from drifting apart, since the writer is handed the
// same instance the layout was measured with.
//
// An instance is not safe to use from two threads at once: the underlying font
// faces cache glyph lookups inside their const measurement calls, so give each
// thread its own instance.
class Pdf_measurement
{
public:
    explicit Pdf_measurement(const Font_family_config& fonts);

    // False when the family could not be loaded; error() says why. Nothing can
    // be measured in that state.
    bool ready() const
    {
        return static_cast<bool>(m_metrics);
    }

    const std::string& error() const
    {
        return m_error;
    }

    text_metrics_t measure_text(
        const std::string&         text,
        Font_id                    font,
        float                      size_pt,
        float                      leading_pt,
        float                      max_width_mm,
        bool                       wrap) const;

    std::vector<std::string> wrap_text(
        const std::string&         text,
        Font_id                    font,
        float                      size_pt,
        float                      max_width_mm) const;

    // The metrics themselves, for the PDF writer that has to embed and draw
    // with the very faces this measured against.
    const std::shared_ptr<const mark2haru::Measurement_context>& context() const
    {
        return m_metrics;
    }

private:
    std::shared_ptr<const mark2haru::Measurement_context>
                   m_metrics;
    std::string    m_error;
};

image_dimensions_t measure_png(
    const std::string&         path);
