#include "briefutil/brief_service.h"

#include "briefutil/letter_builder.h"
#include "briefutil/path_utils.h"

#include <QDate>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace briefutil {

std::string normalize_language(const std::string& language)
{
    std::string value = language;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (value == "de" || value == "de-de" || value == "german" || value == "deutsch") {
        return "de";
    }
    return "en";
}

Localization localization_for_language(const std::string& language)
{
    return normalize_language(language) == "de"
        ? german_localization()
        : english_localization();
}

std::string localized_date(int year, int month, int day, const std::string& language)
{
    if (year <= 0 || month <= 0 || day <= 0) {
        const auto now = QDate::currentDate();
        year = now.year();
        month = now.month();
        day = now.day();
    }

    static const char* k_en_months[] = {
        "", "January", "February", "March", "April", "May", "June", "July",
        "August", "September", "October", "November", "December"
    };
    static const char* k_de_months[] = {
        "", "Januar", "Februar", "M\xc3\xa4rz", "April", "Mai", "Juni", "Juli",
        "August", "September", "Oktober", "November", "Dezember"
    };
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return {};
    }
    if (normalize_language(language) == "de") {
        return std::to_string(day) + ". " + k_de_months[month] + " " + std::to_string(year);
    }
    return std::string(k_en_months[month]) + " " + std::to_string(day) + ", "
        + std::to_string(year);
}

bool is_valid_font_config(const Font_family_config& fonts)
{
    auto valid_slot = [](const std::string& value) {
        if (value.empty()) {
            return true;
        }
        if (!looks_like_font_file(value)) {
            return false;
        }

        QFileInfo info(QString::fromStdString(value));
        return info.exists() && info.isFile();
    };

    return valid_slot(fonts.sans)
        && valid_slot(fonts.sans_bold)
        && valid_slot(fonts.sans_italic)
        && valid_slot(fonts.sans_bold_italic)
        && valid_slot(fonts.mono);
}

float font_scale_from_percent(double percent)
{
    if (!std::isfinite(percent)) {
        return 1.0f;
    }
    return static_cast<float>(std::clamp(percent / 100.0, 0.5, 2.0));
}

letter_layout_spec_t layout_spec_from_name(const std::string& preset)
{
    std::string value = preset;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (value == "din_5008_form_a") {
        return din_5008_form_a();
    }
    if (value == "us_letter") {
        return us_letter();
    }
    return din_5008_form_b();
}

static Generation_result failure(
    Generation_result_code code,
    std::string message,
    std::string detail = {})
{
    Generation_result result;
    result.code = code;
    result.message = std::move(message);
    result.detail = std::move(detail);
    return result;
}

static std::string make_output_path(const Generation_request& request)
{
    if (!request.output_path.empty()) {
        return request.output_path;
    }

    std::string stem = sanitize_filename_component(request.subject);
    if (stem.empty()) {
        stem = "letter";
    }
    std::string prefix = request.timestamp_prefix;
    if (prefix.empty()) {
        prefix = QDateTime::currentDateTime()
            .toString("yyyy-MM-dd HH-mm-ss-zzz ")
            .toStdString();
    }
    return join_path(request.output_dir, prefix + stem + ".pdf");
}

Generation_result generate_brief_pdf(const Generation_request& request)
{
    if (request.profile.profile.id.empty()) {
        return failure(
            Generation_result_code::INVALID_REQUEST,
            "Invalid sender profile selection.");
    }
    if (request.output_path.empty() && request.output_dir.empty()) {
        return failure(
            Generation_result_code::INVALID_REQUEST,
            "No output path or output directory was provided.");
    }
    if (!is_valid_font_config(request.theme.fonts)) {
        return failure(
            Generation_result_code::INVALID_FONT_CONFIG,
            "Invalid font configuration. Leave font fields empty for bundled fonts or provide explicit .ttf font files.");
    }

    const std::string output_path = make_output_path(request);
    QFileInfo output_info(QString::fromStdString(output_path));
    QDir output_dir = output_info.absoluteDir();
    if (!output_dir.exists() && !output_dir.mkpath(".")) {
        return failure(
            Generation_result_code::OUTPUT_ERROR,
            "Could not create the output directory.",
            output_dir.absolutePath().toStdString());
    }
    if (output_info.exists() && !request.overwrite_output) {
        return failure(
            Generation_result_code::OUTPUT_EXISTS,
            "The output PDF already exists.",
            output_path);
    }

    const std::string temp_path = output_dir.filePath(
        QString(".") + output_info.fileName() + ".tmp." + QString::number(QCoreApplication::applicationPid()))
        .toStdString();
    QFile::remove(QString::fromStdString(temp_path));

    Letter_input input;
    input.recipient = request.recipient;
    input.subject = request.subject;
    input.body = request.body;
    input.date = localized_date(
        request.date_year,
        request.date_month,
        request.date_day,
        request.profile.profile.language);

    const auto loc = localization_for_language(request.profile.profile.language);
    auto render_result = generate_letter_pdf(
        request.profile.profile,
        input,
        request.profile.profile_base_dir,
        temp_path,
        request.theme,
        request.layout,
        loc);

    if (!render_result.ok) {
        QFile::remove(QString::fromStdString(temp_path));
        return failure(
            Generation_result_code::RENDER_ERROR,
            render_result.message.empty() ? render_result.detail : render_result.message,
            render_result.detail);
    }

    const QString q_temp_path = QString::fromStdString(temp_path);
    const QString q_output_path = QString::fromStdString(output_path);
    QString backup_path;
    if (!request.overwrite_output && QFileInfo::exists(q_output_path)) {
        QFile::remove(q_temp_path);
        return failure(
            Generation_result_code::OUTPUT_EXISTS,
            "The output PDF already exists.",
            output_path);
    }
    if (request.overwrite_output && QFileInfo::exists(q_output_path)) {
        backup_path = output_dir.filePath(
            QString(".") + output_info.fileName() + ".replace."
            + QString::number(QCoreApplication::applicationPid()));
        QFile::remove(backup_path);
        if (!QFile::rename(q_output_path, backup_path)) {
            QFile::remove(q_temp_path);
            return failure(
                Generation_result_code::OUTPUT_ERROR,
                "Could not prepare the existing PDF for replacement.",
                output_path);
        }
    }

    if (!QFile::rename(q_temp_path, q_output_path)) {
        if (!backup_path.isEmpty()) {
            QFile::rename(backup_path, q_output_path);
        }
        QFile::remove(QString::fromStdString(temp_path));
        return failure(
            Generation_result_code::OUTPUT_ERROR,
            "Could not move the generated PDF into place.",
            output_path);
    }
    if (!backup_path.isEmpty()) {
        QFile::remove(backup_path);
    }

    Generation_result result;
    result.ok = true;
    result.code = Generation_result_code::OK;
    result.output_path = output_path;
    return result;
}

} // namespace briefutil
