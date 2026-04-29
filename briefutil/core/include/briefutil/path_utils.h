#pragma once

#include <string>

namespace briefutil {

enum class path_class_t
{
    Empty,
    Relative,
    Absolute,
    DriveRelative,
    DriveRootRelative,
};

path_class_t classify_windows_path(const std::string& path);
bool is_current_drive_dependent_windows_path(const std::string& path);

std::string sanitize_filename_component(const std::string& input);
bool is_valid_profile_image_name(const std::string& name);

std::string join_path(const std::string& dir, const std::string& name);

} // namespace briefutil
