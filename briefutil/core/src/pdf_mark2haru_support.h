#pragma once

#include "briefutil/document_model.h"
#include "briefutil/typography_config.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

#include <mark2haru/font_context.h>

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

static inline mark2haru::Pdf_font mark2haru_font_for(Font_id id)
{
    switch (id) {
        case Font_id::SANS:             return mark2haru::Pdf_font::REGULAR;
        case Font_id::SANS_BOLD:        return mark2haru::Pdf_font::BOLD;
        case Font_id::SANS_ITALIC:      return mark2haru::Pdf_font::ITALIC;
        case Font_id::SANS_BOLD_ITALIC: return mark2haru::Pdf_font::BOLD_ITALIC;
        case Font_id::MONO:             return mark2haru::Pdf_font::MONO;
        default:                        return mark2haru::Pdf_font::REGULAR;
    }
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

static inline std::vector<std::string> wrap_mark2haru_text(
    const mark2haru::Measurement_context&  metrics,
    const std::string&                     text,
    Font_id                                font,
    float                                  size_pt,
    float                                  max_width_mm)
{
    std::vector<std::string> lines;
    const auto  pdf_font     = mark2haru_font_for(font);
    const float max_width_pt = mm_to_pt(max_width_mm);

    for (const auto& para : split_lines(text)) {
        if (para.empty()) {
            lines.push_back("");
            continue;
        }

        std::string current;
        for_each_word(para, [&](const std::string& word) {
            std::string candidate = current.empty() ? word : current + " " + word;
            float       w         = static_cast<float>(metrics.measure_text_width(pdf_font, candidate, size_pt));
            if (w > max_width_pt && !current.empty()) {
                lines.push_back(current);
                current = word;
            }
            else {
                current = candidate;
            }
        });
        if (!current.empty()) {
            lines.push_back(current);
        }
    }

    return lines;
}
