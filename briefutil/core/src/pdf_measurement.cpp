#include "briefutil/pdf_measurement.h"

#include "pdf_mark2haru_support.h"

#include <QFile>
#include <QIODevice>

#include <algorithm>
#include <cstring>


// ============================================================================
// Measurement cache
// ============================================================================

struct Pdf_measure_context
{
    std::shared_ptr<const mark2haru::Measurement_context>
                       m_metrics;
    Font_family_config m_current_fc;
    std::string        m_last_error;

    bool init(const Font_family_config& fc)
    {
        m_last_error.clear();
        m_metrics = make_mark2haru_measurement_context(fc, &m_last_error);
        if (!m_metrics) {
            return false;
        }
        m_current_fc = fc;
        return true;
    }

    bool ready() const
    {
        return m_metrics && m_metrics->loaded();
    }
};

static Pdf_measure_context& get_measure_context(
    const Font_family_config&  fc,
    std::string*               detail = nullptr)
{
    static Pdf_measure_context ctx;

    bool need_init = !ctx.ready();
    if (!need_init && fc != ctx.m_current_fc) {
        need_init = true;
    }

    if (need_init) {
        ctx.init(fc);
        if (!ctx.ready() && detail) {
            *detail = ctx.m_last_error.empty()
                ? "Failed to initialize the PDF measurement context."
                : ctx.m_last_error;
        }
    }

    return ctx;
}


// ============================================================================
// Public readiness check
// ============================================================================

bool pdf_measurement_ready(
    const Font_family_config&  fonts,
    std::string*               detail)
{
    auto& ctx = get_measure_context(fonts, detail);
    return ctx.ready();
}


// ============================================================================
// Text measurement
// ============================================================================

text_metrics_t measure_text(
    const std::string&         text,
    Font_id                    font,
    float                      size_pt,
    float                      leading_pt,
    float                      max_width_mm,
    bool                       wrap,
    const Font_family_config&  fonts)
{
    auto& ctx = get_measure_context(fonts);
    if (!ctx.ready()) {
        return {};
    }

    const auto  pdf_font = mark2haru_font_for(font);
    const float lead     = leading_pt > 0 ? leading_pt : size_pt;

    std::vector<std::string> lines;
    if (wrap) {
        lines = wrap_mark2haru_text(*ctx.m_metrics, text, font, size_pt, max_width_mm);
    }
    else {
        lines = split_lines(text);
    }

    float max_w = 0;
    for (const auto& line : lines) {
        max_w = std::max(
            max_w,
            static_cast<float>(ctx.m_metrics->measure_text_width(pdf_font, line, size_pt)));
    }

    const int line_count = static_cast<int>(lines.size());
    const float height = line_count > 0
        ? size_pt + static_cast<float>(line_count - 1) * lead
        : 0.0f;
    return { max_w, height, line_count };
}

std::vector<std::string> wrap_text(
    const std::string&         text,
    Font_id                    font,
    float                      size_pt,
    float                      max_width_mm,
    const Font_family_config&  fonts)
{
    auto& ctx = get_measure_context(fonts);
    if (!ctx.ready()) {
        return {};
    }
    return wrap_mark2haru_text(*ctx.m_metrics, text, font, size_pt, max_width_mm);
}


// ============================================================================
// PNG measurement
// ============================================================================

image_dimensions_t measure_png(const std::string& path)
{
    image_dimensions_t empty;
    if (path.empty()) {
        return empty;
    }

    // Layout only needs the intrinsic PNG size. Read it cheaply from IHDR and
    // leave full decode/validation to the PDF renderer.
    QFile file(QString::fromUtf8(path.c_str()));
    if (!file.open(QIODevice::ReadOnly)) {
        return empty;
    }

    QByteArray header = file.read(24);
    if (header.size() < 24) {
        return empty;
    }

    static constexpr unsigned char k_png_sig[8] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
    };
    auto* d = reinterpret_cast<const unsigned char*>(header.constData());
    if (std::memcmp(d, k_png_sig, 8)   != 0) { return empty; }
    if (std::memcmp(d + 12, "IHDR", 4) != 0) { return empty; }

    auto rd_u32 = [&](int o) -> unsigned {
        return
            (unsigned(d[o    ]) << 24) |
            (unsigned(d[o + 1]) << 16) |
            (unsigned(d[o + 2]) << 8)  |
             unsigned(d[o + 3]);
    };

    image_dimensions_t dims;
    dims.width_px  = (float)rd_u32(16);
    dims.height_px = (float)rd_u32(20);
    dims.valid     = dims.width_px > 0 && dims.height_px > 0;
    return dims;
}
