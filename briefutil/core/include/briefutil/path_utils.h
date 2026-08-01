#pragma once

#include <cstddef>
#include <string>

namespace briefutil {

enum class Path_class
{
    EMPTY,
    RELATIVE,
    ABSOLUTE,
    DRIVE_RELATIVE,
    DRIVE_ROOT_RELATIVE,
};

Path_class classify_windows_path(const std::string& path);
bool is_current_drive_dependent_windows_path(const std::string& path);

// Longest value sanitize_filename_component returns. A directory entry is
// capped at 255 bytes on the filesystems briefutil targets, and callers derive
// longer names from the sanitized value: brief_service prepends a 24-byte
// timestamp, appends ".pdf", and stages the result as ".<name>.tmp.<16 hex>".
// This bound leaves room for all of them and still keeps the full path clear of
// the Windows MAX_PATH limit for a reasonably deep output directory.
inline constexpr size_t k_max_filename_component_bytes = 120;

// Rewrite `input` into a value usable as a single filename component:
// whitespace collapsed, characters that no filesystem accepts replaced,
// reserved Windows device names escaped, and the result truncated on a UTF-8
// code-point boundary to k_max_filename_component_bytes.
std::string sanitize_filename_component(const std::string& input);
bool is_valid_profile_image_name(const std::string& name);

std::string join_path(const std::string& dir, const std::string& name);

} // namespace briefutil
