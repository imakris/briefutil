#include "briefutil/pdf_measurement.h"

#include "pdf_haru_support.h"
#include "pdf_mark2haru_support.h"

#include <QFile>
#include <QIODevice>

#include <algorithm>
#include <cstring>


// ============================================================================
// Haru measurement cache
// ============================================================================

static Haru_context& get_haru_measure_context(
    const font_family_config_t& fc,
    std::string* detail = nullptr)
{
    static Haru_context ctx;
    static font_family_config_t current_fc;

    bool need_init = !ctx.ready();
    if (!need_init && fc != current_fc) {
        ctx.destroy();
        need_init = true;
    }

    if (need_init) {
        ctx.init(fc);
        current_fc = fc;
        if (!ctx.ready() && detail) {
            *detail = ctx.last_error.empty()
                ? "Failed to initialize the libHaru measurement backend."
                : ctx.last_error;
        }
    }

    return ctx;
}


// ============================================================================
// mark2haru measurement cache
// ============================================================================

#if BRIEFUTIL_HAS_MARK2HARU
struct Mark2Haru_measure_context
{
    std::shared_ptr<const mark2haru::Measurement_context> metrics;
    font_family_config_t current_fc;
    std::string last_error;

    bool init(const font_family_config_t& fc)
    {
        last_error.clear();
        metrics = make_mark2haru_measurement_context(fc, &last_error);
        if (!metrics) {
            return false;
        }
        current_fc = fc;
        return true;
    }

    bool ready() const
    {
        return metrics && metrics->loaded();
    }
};

static Mark2Haru_measure_context& get_mark2haru_measure_context(
    const font_family_config_t& fc,
    std::string* detail = nullptr)
{
    static Mark2Haru_measure_context ctx;

    bool need_init = !ctx.ready();
    if (!need_init && fc != ctx.current_fc) {
        need_init = true;
    }

    if (need_init) {
        ctx.init(fc);
        if (!ctx.ready() && detail) {
            *detail = ctx.last_error.empty()
                ? "Failed to initialize the mark2haru measurement backend."
                : ctx.last_error;
        }
    }

    return ctx;
}
#endif


// ============================================================================
// Public readiness check
// ============================================================================

bool pdf_measurement_ready(
    Pdf_backend backend,
    const font_family_config_t& fonts,
    std::string* detail)
{
    switch (backend) {
        case Pdf_backend::Haru: {
            auto& ctx = get_haru_measure_context(fonts, detail);
            return ctx.ready();
        }
        case Pdf_backend::Mark2Haru: {
#if BRIEFUTIL_HAS_MARK2HARU
            auto& ctx = get_mark2haru_measure_context(fonts, detail);
            return ctx.ready();
#else
            if (detail) {
                *detail = "The mark2haru backend is not available in this build.";
            }
            return false;
#endif
        }
        default:
            break;
    }
    if (detail) {
        *detail = "Unknown PDF backend.";
    }
    return false;
}


// ============================================================================
// Text measurement
// ============================================================================

static text_metrics_t measure_with_haru(
    const std::string& text,
    Font_id font_id,
    float size_pt,
    float leading_pt,
    float max_width_mm,
    bool wrap,
    const font_family_config_t& fonts)
{
    auto& ctx = get_haru_measure_context(fonts);
    if (!ctx.ready()) {
        return {};
    }

    auto font = ctx.font_for(font_id);
    if (!font) {
        return {};
    }

    float lead = leading_pt > 0 ? leading_pt : size_pt;
    float max_width_pt = mm_to_pt(max_width_mm);

    std::vector<std::string> lines;
    if (wrap) {
        lines = do_wrap(font, size_pt, max_width_pt, text);
    }
    else {
        lines = split_lines(text);
    }

    float max_w = 0;
    for (const auto& line : lines) {
        float w = font_text_width_pt(font, size_pt, line);
        max_w = std::max(max_w, w);
    }

    int n = (int)lines.size();
    float height = n > 0 ? size_pt + (float)(n - 1) * lead : 0;

    return { max_w, height, n };
}

#if BRIEFUTIL_HAS_MARK2HARU
static text_metrics_t measure_with_mark2haru(
    const std::string& text,
    Font_id font_id,
    float size_pt,
    float leading_pt,
    float max_width_mm,
    bool wrap,
    const font_family_config_t& fonts)
{
    auto& ctx = get_mark2haru_measure_context(fonts);
    if (!ctx.ready()) {
        return {};
    }

    const auto font = mark2haru_font_for(font_id);
    float lead = leading_pt > 0 ? leading_pt : size_pt;

    std::vector<std::string> lines;
    if (wrap) {
        lines = wrap_mark2haru_text(*ctx.metrics, text, font_id, size_pt, max_width_mm);
    }
    else {
        lines = split_lines(text);
    }

    float max_w = 0;
    for (const auto& line : lines) {
        max_w = std::max(max_w, (float)ctx.metrics->measure_text_width(font, line, size_pt));
    }

    int n = (int)lines.size();
    float height = n > 0 ? size_pt + (float)(n - 1) * lead : 0;
    return { max_w, height, n };
}
#endif

text_metrics_t measure_text(
    Pdf_backend backend,
    const std::string& text,
    Font_id font,
    float size_pt,
    float leading_pt,
    float max_width_mm,
    bool wrap,
    const font_family_config_t& fonts)
{
    switch (backend) {
        case Pdf_backend::Haru:
            return measure_with_haru(
                text,
                font,
                size_pt,
                leading_pt,
                max_width_mm,
                wrap,
                fonts);
        case Pdf_backend::Mark2Haru:
#if BRIEFUTIL_HAS_MARK2HARU
            return measure_with_mark2haru(
                text,
                font,
                size_pt,
                leading_pt,
                max_width_mm,
                wrap,
                fonts);
#else
            return {};
#endif
        default:
            return {};
    }
}

std::vector<std::string> wrap_text(
    Pdf_backend backend,
    const std::string& text,
    Font_id font,
    float size_pt,
    float max_width_mm,
    const font_family_config_t& fonts)
{
    auto metrics = measure_text(backend, text, font, size_pt, 0, max_width_mm, true, fonts);
    if (metrics.line_count == 0 && !text.empty()) {
        return {};
    }

    std::vector<std::string> lines;
    switch (backend) {
        case Pdf_backend::Haru: {
            auto& ctx = get_haru_measure_context(fonts);
            auto f = ctx.font_for(font);
            if (!f) return {};
            lines = do_wrap(f, size_pt, mm_to_pt(max_width_mm), text);
            break;
        }
        case Pdf_backend::Mark2Haru:
#if BRIEFUTIL_HAS_MARK2HARU
            {
                auto& ctx = get_mark2haru_measure_context(fonts);
                if (!ctx.ready()) return {};
                lines = wrap_mark2haru_text(*ctx.metrics, text, font, size_pt, max_width_mm);
            }
            break;
#else
            return {};
#endif
        default:
            return {};
    }

    return lines;
}


// ============================================================================
// PNG measurement
// ============================================================================

image_dimensions_t measure_png(const std::string& path)
{
    image_dimensions_t empty;
    if (path.empty()) return empty;

    // Layout only needs the intrinsic PNG size. Read it cheaply from IHDR and
    // leave full decode/validation to the backend renderer.
    QFile file(QString::fromUtf8(path.c_str()));
    if (!file.open(QIODevice::ReadOnly)) return empty;

    QByteArray header = file.read(24);
    if (header.size() < 24) return empty;

    static const unsigned char k_png_sig[8] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
    };
    auto* d = reinterpret_cast<const unsigned char*>(header.constData());
    if (std::memcmp(d, k_png_sig, 8) != 0) return empty;
    if (std::memcmp(d + 12, "IHDR", 4) != 0) return empty;

    auto rd_u32 = [&](int o) -> unsigned {
        return (unsigned(d[o]) << 24)
             | (unsigned(d[o + 1]) << 16)
             | (unsigned(d[o + 2]) << 8)
             |  unsigned(d[o + 3]);
    };

    image_dimensions_t dims;
    dims.width_px  = (float)rd_u32(16);
    dims.height_px = (float)rd_u32(20);
    dims.valid     = dims.width_px > 0 && dims.height_px > 0;
    return dims;
}
