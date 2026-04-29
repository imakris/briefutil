#pragma once

#include "briefutil/letter_layout_spec.h"
#include "briefutil/localization.h"
#include "briefutil/pdf_backend.h"
#include "briefutil/sender_profile.h"
#include "briefutil/typography_config.h"

#include <string>

namespace briefutil {

enum class generation_result_code
{
    Ok,
    Invalid_request,
    Invalid_font_config,
    Backend_unavailable,
    Output_exists,
    Output_error,
    Render_error,
};

struct profile_snapshot_t
{
    sender_profile_t profile;
    std::string profile_path;
    std::string profile_base_dir;
};

struct generation_request_t
{
    profile_snapshot_t profile;
    std::string recipient;
    std::string subject;
    std::string body;

    std::string output_path;
    std::string output_dir;
    std::string timestamp_prefix;
    bool overwrite_output = false;

    int date_year = 0;
    int date_month = 0;
    int date_day = 0;

    theme_config_t theme = default_theme();
    letter_layout_spec_t layout = din_5008_form_b();
    Pdf_backend backend = Pdf_backend::Haru;
};

struct generation_result_t
{
    bool ok = false;
    generation_result_code code = generation_result_code::Invalid_request;
    std::string output_path;
    std::string message;
    std::string detail;
};

std::string normalize_language(const std::string& language);
localization_t localization_for_language(const std::string& language);
std::string localized_date(int year, int month, int day, const std::string& language);

bool is_valid_font_config(const font_family_config_t& fonts);
float font_scale_from_percent(double percent);
letter_layout_spec_t layout_spec_from_name(const std::string& preset);

generation_result_t generate_brief_pdf(const generation_request_t& request);

} // namespace briefutil
