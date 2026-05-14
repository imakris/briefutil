#pragma once

#include "briefutil/sender_profile.h"

#include <string>
#include <vector>

namespace briefutil {

struct Profile_entry
{
    Sender_profile profile;
    std::string    path;
    std::string    base_dir;
};

std::string default_template_dir();
std::string default_output_dir();

std::string read_output_dir_conf(
    const std::string&         path);

std::string configured_output_dir(
    const std::string&         application_dir,
    const std::string&         current_dir);

bool ensure_template_dir_ready(
    const std::string&         dir_path,
    std::string*               error = nullptr);

std::vector<Profile_entry> discover_profiles(
    const std::string&         template_dir,
    std::vector<std::string>*  errors = nullptr);

} // namespace briefutil
