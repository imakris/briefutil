#include "briefutil/sender_profile.h"
#include "briefutil/owned_staging.h"
#include "briefutil/sender_profile_schema.h"
#include "briefutil/path_utils.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <string>

// Use Qt's JSON parser; it is already available through Qt6::Core.


static std::string qs(const QString& s) { return s.toStdString(); }

static std::vector<std::string> json_string_array(
    const QJsonObject& obj,
    const char*        key)
{
    std::vector<std::string> result;
    auto arr = obj.value(key).toArray();
    for (const auto& v : arr) {
        result.push_back(qs(v.toString()));
    }
    return result;
}

enum class Color_parse
{
    ABSENT,
    OK,
    INVALID,
};

// Parses a "[r, g, b]" colour, requiring exactly three integer channels in
// 0..255. ABSENT (key missing) leaves the caller's default in place; INVALID
// (key present but malformed) is surfaced as a profile load error rather than
// silently substituting a fallback or coercing bad values to black.
static Color_parse parse_json_color(
    const QJsonObject& obj,
    const char*        key,
    color_t&           out)
{
    if (!obj.contains(key)) {
        return Color_parse::ABSENT;
    }
    const auto value = obj.value(key);
    if (!value.isArray()) {
        return Color_parse::INVALID;
    }
    const auto arr = value.toArray();
    if (arr.size() != 3) {
        return Color_parse::INVALID;
    }

    float channels[3];
    for (int i = 0; i < 3; ++i) {
        const auto channel = arr[i];
        if (!channel.isDouble()) {
            return Color_parse::INVALID;
        }
        const double raw = channel.toDouble();
        if (raw < 0.0 || raw > 255.0 || raw != std::floor(raw)) {
            return Color_parse::INVALID;
        }
        channels[i] = (float)raw / 255.0f;
    }
    out = { channels[0], channels[1], channels[2] };
    return Color_parse::OK;
}

static QJsonArray json_string_array(const std::vector<std::string>& lines)
{
    QJsonArray result;
    for (const auto& line : lines) {
        result.push_back(QString::fromStdString(line));
    }
    return result;
}

static QJsonArray json_color_array(color_t color)
{
    return {
        qRound(color.r * 255.0f),
        qRound(color.g * 255.0f),
        qRound(color.b * 255.0f),
    };
}


Profile_load_result load_sender_profile(const std::string& json_path)
{
    QFile file(QString::fromStdString(json_path));
    if (!file.open(QIODevice::ReadOnly)) {
        return { false, {}, "Cannot open profile: " + json_path };
    }

    QJsonParseError parse_error;
    auto doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
    file.close();

    if (doc.isNull()) {
        return { false, {}, "JSON parse error: " + qs(parse_error.errorString()) };
    }

    auto obj = doc.object();
    Sender_profile p;

    for (const auto& f : k_sender_string_fields) {
        p.*f.member = qs(obj.value(f.json_key).toString());
    }
    for (const auto& f : k_sender_string_array_fields) {
        p.*f.member = json_string_array(obj, f.json_key);
    }
    p.language = qs(obj.value("language").toString());
    if (p.language.empty()) {
        p.language = "en";
    }

    auto style_str = obj.value("style").toString().toLower();
    p.style = (style_str == "commercial")
        ? Profile_style::COMMERCIAL : Profile_style::SIMPLE;
    if (parse_json_color(obj, "top_rule_color", p.top_rule_color) == Color_parse::INVALID) {
        return {
            false,
            {},
            "Profile 'top_rule_color' must be three integers in 0..255: " + json_path,
        };
    }

    auto reject_image = [&](const char* key) -> Profile_load_result {
        return {
            false,
            {},
            std::string("Profile '") + key
                + "' must be a relative .png asset name (no '..' or absolute path): "
                + json_path,
        };
    };
    if (!briefutil::is_valid_profile_image_name(p.signature_image)) {
        return reject_image("signature_image");
    }
    if (!briefutil::is_valid_profile_image_name(p.logo_image)) {
        return reject_image("logo_image");
    }

    if (p.id.empty()) {
        return { false, {}, "Profile missing 'id' field: " + json_path };
    }

    return { true, std::move(p), "" };
}

bool save_sender_profile(
    const Sender_profile&  profile,
    const std::string&     json_path,
    std::string*           error)
{
    QJsonObject obj;
    for (const auto& f : k_sender_string_fields) {
        obj.insert(f.json_key, QString::fromStdString(profile.*f.member));
    }
    for (const auto& f : k_sender_string_array_fields) {
        obj.insert(f.json_key, json_string_array(profile.*f.member));
    }
    obj.insert("language",      QString::fromStdString(profile.language));
    obj.insert("style",          profile.style == Profile_style::COMMERCIAL ? "commercial" : "simple");
    obj.insert("top_rule_color", json_color_array(profile.top_rule_color));

    // Staged in briefutil's own directory rather than beside the profile it
    // replaces. A save killed halfway then leaves its debris where briefutil
    // can account for it, and never an unrecognizable sibling in a directory
    // whose other contents are the user's.
    const QString                 target(QString::fromStdString(json_path));
    const QFileInfo               target_info(target);
    briefutil::Owned_staging_slot staging;
    if (!staging.open(target_info.absoluteDir(), target_info.fileName(), error)) {
        return false;
    }

    QFile file(staging.staged_path());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = "Cannot open profile for writing: " + json_path;
        }
        return false;
    }

    auto payload = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size() || !file.flush()) {
        if (error) {
            *error = "Failed to write profile: " + json_path;
        }
        return false;
    }
    file.close();

    std::string publish_detail;
    if (briefutil::publish_staged_file(staging.staged_path(), target, true, &publish_detail) !=
        briefutil::Publish_outcome::PUBLISHED)
    {
        if (error) {
            *error = "Failed to finalize profile write: " + json_path;
            if (!publish_detail.empty()) {
                *error += ": " + publish_detail;
            }
        }
        return false;
    }

    if (error) {
        error->clear();
    }
    return true;
}
