#pragma once

#include "briefutil/letter_layout_spec.h"
#include "briefutil/localization.h"
#include "briefutil/sender_profile.h"
#include "briefutil/typography_config.h"

#include <string>

namespace briefutil {

enum class Generation_result_code
{
    OK,
    INVALID_REQUEST,
    INVALID_FONT_CONFIG,
    OUTPUT_EXISTS,
    OUTPUT_ERROR,
    RENDER_ERROR,
};

struct Profile_snapshot
{
    Sender_profile         profile;
    std::string            profile_path;
    std::string            profile_base_dir;
};

struct Generation_request
{
    Profile_snapshot       profile;
    std::string            recipient;
    std::string            subject;
    std::string            body;

    std::string            output_path;
    std::string            output_dir;
    std::string            timestamp_prefix;
    bool                   overwrite_output = false;

    int                    date_year        = 0;
    int                    date_month       = 0;
    int                    date_day         = 0;

    Theme_config           theme            = default_theme();
    letter_layout_spec_t   layout           = din_5008_form_b();
};

struct Generation_result
{
    bool                   ok               = false;
    Generation_result_code code             = Generation_result_code::INVALID_REQUEST;
    std::string            output_path;
    std::string            message;
    std::string            detail;
};

std::string normalize_language(const std::string& language);
Localization localization_for_language(const std::string& language);
std::string localized_date(int year, int month, int day, const std::string& language);

bool is_valid_font_config(const Font_family_config& fonts);
float font_scale_from_percent(double percent);
letter_layout_spec_t layout_spec_from_name(const std::string& preset);

Generation_result generate_brief_pdf(const Generation_request& request);

} // namespace briefutil
