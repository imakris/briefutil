#include "briefutil/path_utils.h"

#include <algorithm>
#include <cctype>

namespace briefutil {

static std::string trim_ascii(std::string value)
{
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

path_class_t classify_windows_path(const std::string& path)
{
    if (path.empty()) {
        return path_class_t::Empty;
    }
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0]))
        && path[1] == ':') {
        if (path.size() >= 3 && (path[2] == '\\' || path[2] == '/')) {
            return path_class_t::Absolute;
        }
        return path_class_t::DriveRelative;
    }
    if (path.size() >= 2
        && ((path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/'))) {
        return path_class_t::Absolute;
    }
    if (path[0] == '\\' || path[0] == '/') {
        return path_class_t::DriveRootRelative;
    }
    return path_class_t::Relative;
}

bool is_current_drive_dependent_windows_path(const std::string& path)
{
    const auto cls = classify_windows_path(path);
    return cls == path_class_t::DriveRelative
        || cls == path_class_t::DriveRootRelative;
}

static bool is_reserved_windows_stem(std::string value)
{
    while (!value.empty() && (value.back() == ' ' || value.back() == '.')) {
        value.pop_back();
    }
    const auto dot = value.find('.');
    if (dot != std::string::npos) {
        value.resize(dot);
    }
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if (value == "CON" || value == "PRN" || value == "AUX" || value == "NUL") {
        return true;
    }
    if (value.size() == 4
        && (value.rfind("COM", 0) == 0 || value.rfind("LPT", 0) == 0)
        && value[3] >= '1' && value[3] <= '9') {
        return true;
    }
    return false;
}

std::string sanitize_filename_component(const std::string& input)
{
    std::string value = trim_ascii(input);
    std::string collapsed;
    collapsed.reserve(value.size());
    bool previous_space = false;
    for (char c : value) {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isspace(uc)) {
            if (!previous_space) {
                collapsed.push_back(' ');
                previous_space = true;
            }
            continue;
        }
        previous_space = false;
        if (uc < 0x20 || c == '<' || c == '>' || c == ':' || c == '"'
            || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
            collapsed.push_back('_');
        }
        else {
            collapsed.push_back(c);
        }
    }
    value = std::move(collapsed);
    if (is_reserved_windows_stem(value)) {
        value = "_" + value;
    }
    for (size_t i = value.size(); i > 0 && (value[i - 1] == ' ' || value[i - 1] == '.'); --i) {
        value[i - 1] = '_';
    }
    return value;
}

bool is_valid_profile_image_name(const std::string& name)
{
    if (name.empty()) {
        return true;
    }
    if (classify_windows_path(name) == path_class_t::Absolute
        || is_current_drive_dependent_windows_path(name)) {
        return false;
    }
    if (name == ".." || name.rfind("../", 0) == 0 || name.rfind("..\\", 0) == 0
        || name.find("/../") != std::string::npos || name.find("\\..\\") != std::string::npos
        || name.find("/..\\") != std::string::npos || name.find("\\../") != std::string::npos) {
        return false;
    }
    auto lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower.size() >= 4 && lower.ends_with(".png");
}

std::string join_path(const std::string& dir, const std::string& name)
{
    if (dir.empty()) {
        return name;
    }
    if (dir.ends_with('/') || dir.ends_with('\\')) {
        return dir + name;
    }
    return dir + "/" + name;
}

} // namespace briefutil
