#pragma once

#include "briefutil/document_model.h"
#include "briefutil/typography_config.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

#include <mark2haru/font_context.h>
#include <mark2haru/text_layout.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>


// ============================================================================
// mark2haru helpers
// ============================================================================

static inline QString path_to_qstring(const std::filesystem::path& path)
{
#if defined(_WIN32)
    return QString::fromStdWString(path.native());
#else
    auto u8 = path.u8string();
    return QString::fromUtf8(
        reinterpret_cast<const char*>(u8.c_str()),
        static_cast<int>(u8.size()));
#endif
}

static inline std::filesystem::path qstring_to_path(const QString& path)
{
#if defined(_WIN32)
    return std::filesystem::path(path.toStdWString());
#else
    const QByteArray utf8 = path.toUtf8();
    return std::filesystem::u8path(std::string_view(utf8.constData(), (size_t)utf8.size()));
#endif
}

static inline std::filesystem::path mark2haru_runtime_font_dir()
{
    if (const auto* app = QCoreApplication::instance()) {
        const QString app_dir = app->applicationDirPath();
        if (!app_dir.isEmpty()) {
            const QString app_fonts = QDir(app_dir).filePath("fonts");
            if (QFileInfo(app_fonts).isDir()) {
                return qstring_to_path(app_fonts);
            }
        }
    }

    const QString current_fonts = QDir::current().filePath("fonts");
    if (QFileInfo(current_fonts).isDir()) {
        return qstring_to_path(current_fonts);
    }

    return {};
}

static inline mark2haru::Font_source mark2haru_font_source(const std::string& value)
{
    if (value.empty()) {
        return {};
    }

    return mark2haru::Font_source::from_path(
        qstring_to_path(QString::fromUtf8(value.c_str())));
}

// briefutil's font slots and mark2haru's inline styles name the same five
// faces, so text this library wraps without any markup of its own still goes
// through the shared engine under the style whose face the caller asked for.
static inline mark2haru::Inline_style mark2haru_style_for(Font_id id)
{
    switch (id) {
        case Font_id::SANS:             return mark2haru::Inline_style::NORMAL;
        case Font_id::SANS_BOLD:        return mark2haru::Inline_style::BOLD;
        case Font_id::SANS_ITALIC:      return mark2haru::Inline_style::ITALIC;
        case Font_id::SANS_BOLD_ITALIC: return mark2haru::Inline_style::BOLD_ITALIC;
        case Font_id::MONO:             return mark2haru::Inline_style::CODE;
        default:                        return mark2haru::Inline_style::NORMAL;
    }
}

static inline mark2haru::Pdf_font mark2haru_font_for(Font_id id)
{
    return mark2haru::text_layout::font_for(mark2haru_style_for(id));
}

static inline mark2haru::Font_family_config mark2haru_font_family(const Font_family_config& fonts)
{
    mark2haru::Font_family_config family;
    family.regular     = mark2haru_font_source(fonts.sans);
    family.bold        = mark2haru_font_source(fonts.sans_bold);
    family.italic      = mark2haru_font_source(fonts.sans_italic);
    family.bold_italic = mark2haru_font_source(fonts.sans_bold_italic);
    family.mono        = mark2haru_font_source(fonts.mono);
    return family;
}

static inline std::shared_ptr<const mark2haru::Measurement_context>
make_mark2haru_measurement_context(
    const Font_family_config&  fonts,
    std::string*               error = nullptr)
{
    auto ctx = std::make_shared<mark2haru::Measurement_context>(
        mark2haru_font_family(fonts),
        mark2haru_runtime_font_dir());
    if (!ctx->loaded()) {
        if (error) {
            *error = ctx->error();
        }
        return {};
    }
    return ctx;
}

// Wraps text that carries no inline markup of its own, such as the subject
// line or a footer line, through mark2haru's engine. Doing it here rather than
// with a local greedy loop is what makes those lines break where a paragraph
// or a table cell in the same document breaks, and is what gives them the
// over-long-token splitting a private loop did not have.
static inline std::vector<std::string> wrap_mark2haru_text(
    const mark2haru::Measurement_context&  metrics,
    const std::string&                     text,
    Font_id                                font,
    float                                  size_pt,
    float                                  max_width_mm)
{
    const auto wrapped = mark2haru::text_layout::wrap_tokens(
        mark2haru::text_layout::tokenize_text(text, mark2haru_style_for(font)),
        mm_to_pt(max_width_mm),
        size_pt,
        size_pt,
        [&metrics](mark2haru::Pdf_font pdf_font, const std::string& piece, double pt) {
            return metrics.measure_text_width(pdf_font, piece, pt);
        });

    std::vector<std::string> lines;
    lines.reserve(wrapped.size());
    for (const auto& wrapped_line : wrapped) {
        std::string line;
        for (const auto& span : wrapped_line.spans) {
            line += span.text;
        }
        lines.push_back(std::move(line));
    }
    return lines;
}
