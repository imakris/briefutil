#include "briefutil/brief_service.h"

#include "briefutil/letter_builder.h"
#include "briefutil/owned_staging.h"
#include "briefutil/path_utils.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>

#include <algorithm>
#include <cmath>
#include <string>

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
        year  = now.year();
        month = now.month();
        day   = now.day();
    }

    static constexpr const char* k_en_months[] = {
        "", "January", "February", "March", "April", "May", "June", "July",
        "August", "September", "October", "November", "December"
    };
    static constexpr const char* k_de_months[] = {
        "", "Januar", "Februar", "M\xc3\xa4rz", "April", "Mai", "Juni", "Juli",
        "August", "September", "Oktober", "November", "Dezember"
    };
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return {};
    }
    if (normalize_language(language) == "de") {
        return std::to_string(day) + ". " + k_de_months[month] + " " + std::to_string(year);
    }
    return
        std::string(k_en_months[month]) + " "  +
        std::to_string(day)             + ", " +
        std::to_string(year);
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

    return
        valid_slot(fonts.sans)             &&
        valid_slot(fonts.sans_bold)        &&
        valid_slot(fonts.sans_italic)      &&
        valid_slot(fonts.sans_bold_italic) &&
        valid_slot(fonts.mono);
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
    if (value == "din_5008_form_a") { return din_5008_form_a(); }
    if (value == "us_letter")       { return us_letter();       }
    return din_5008_form_b();
}

static Generation_result failure(
    Generation_result_code code,
    std::string            message,
    std::string            detail = {})
{
    Generation_result result;
    result.code    = code;
    result.message = std::move(message);
    result.detail  = std::move(detail);
    return result;
}

// How long a run waits for another briefutil run that is already publishing
// the same target. Generation takes well under a second, so a wait this long
// only happens when the other run is itself blocked, and failing then is more
// useful than blocking the caller indefinitely.
static constexpr int k_target_lock_timeout_ms = 30000;

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
            "Invalid font configuration. Leave font fields empty "
            "for bundled fonts or provide explicit .ttf font files.");
    }
    const bool date_unset =
        request.date_year  <= 0 &&
        request.date_month <= 0 &&
        request.date_day   <= 0;
    const bool date_complete =
        request.date_year  > 0 &&
        request.date_month > 0 &&
        request.date_day   > 0;

    if (!date_unset && !date_complete) {
        return failure(
            Generation_result_code::INVALID_REQUEST,
            "The letter date must be fully specified or fully unset.");
    }
    if (date_complete &&
        !QDate(request.date_year, request.date_month, request.date_day).isValid())
    {
        return failure(
            Generation_result_code::INVALID_REQUEST,
            "The letter date is not a valid calendar date.");
    }

    const std::string output_path   = make_output_path(request);
    const QString     q_output_path = QString::fromStdString(output_path);
    QFileInfo         output_info(q_output_path);
    QDir              output_dir    = output_info.absoluteDir();
    if (!output_dir.exists() && !output_dir.mkpath(".")) {
        return
            failure(
                Generation_result_code::OUTPUT_ERROR,
                "Could not create the output directory.",
                output_dir.absolutePath().toStdString()
            );
    }

    // Serialize every run that publishes this target, across processes. The
    // lock is what makes the existence check below exact instead of a guess:
    // while it is held no other briefutil run can create, replace or stage this
    // target. A stale lock time of zero means a lock is only ever reclaimed once
    // its owning process is provably gone, so a long render is never robbed of
    // its lock.
    const QString lock_path = output_dir.filePath(
        QStringLiteral(".") + output_info.fileName() + ".lock");

    QLockFile target_lock(lock_path);
    target_lock.setStaleLockTime(0);
    if (!target_lock.tryLock(k_target_lock_timeout_ms)) {
        return
            failure(
                Generation_result_code::OUTPUT_ERROR,
                "Another briefutil run is already writing this PDF.",
                output_path
            );
    }

    // Refusing comes before anything is staged, so a request briefutil declines
    // leaves the output directory exactly as it found it.
    if (!request.overwrite_output && QFileInfo::exists(q_output_path)) {
        return failure(
            Generation_result_code::OUTPUT_EXISTS,
            "The output PDF already exists.",
            output_path);
    }

    // Staging happens inside briefutil's own directory, never beside the user's
    // files. Opening the slot reclaims what a run that died mid-render left
    // there, and it can do so without deciding whether any given name is
    // briefutil's, because everything under the slot is briefutil's by
    // construction. The slot is discarded when this scope ends.
    Owned_staging_slot staging;
    std::string        staging_error;
    if (!staging.open(output_dir, output_info.fileName(), &staging_error)) {
        return
            failure(
                Generation_result_code::OUTPUT_ERROR,
                "Could not create the staging directory.",
                staging_error
            );
    }
    const QString&    q_temp_path = staging.staged_path();
    const std::string temp_path   = q_temp_path.toStdString();

    Letter_input input;
    input.recipient = request.recipient;
    input.subject   = request.subject;
    input.body      = request.body;
    input.date      = localized_date(
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
        return
            failure(
                Generation_result_code::RENDER_ERROR,
                render_result.message.empty() ? render_result.detail : render_result.message,
                render_result.detail
            );
    }

    std::string          publish_detail;
    const Publish_outcome published = publish_staged_file(
        q_temp_path,
        q_output_path,
        request.overwrite_output,
        &publish_detail);

    if (published != Publish_outcome::PUBLISHED) {
        if (published == Publish_outcome::TARGET_EXISTS) {
            return failure(
                Generation_result_code::OUTPUT_EXISTS,
                "The output PDF already exists.",
                output_path);
        }
        return
            failure(
                Generation_result_code::OUTPUT_ERROR,
                "Could not move the generated PDF into place.",
                publish_detail.empty() ? output_path : output_path + ": " + publish_detail
            );
    }

    Generation_result result;
    result.ok          = true;
    result.code        = Generation_result_code::OK;
    result.output_path = output_path;
    return result;
}

} // namespace briefutil
