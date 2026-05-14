#include "briefutil/sender_profile.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

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

static color_t json_color(
    const QJsonObject& obj,
    const char*        key,
    color_t            fallback)
{
    auto arr = obj.value(key).toArray();
    if (arr.size() != 3) {
        return fallback;
    }
    return {
        (float)arr[0].toInt() / 255.0f,
        (float)arr[1].toInt() / 255.0f,
        (float)arr[2].toInt() / 255.0f,
    };
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

    p.id           = qs(obj.value("id").toString());
    p.sender_lines = json_string_array(obj, "sender_lines");
    p.email        = qs(obj.value("email").toString());
    p.language     = qs(obj.value("language").toString());
    if (p.language.empty()) {
        p.language = "en";
    }
    p.return_address_line = qs(obj.value("return_address_line").toString());
    p.closing_phrase      = qs(obj.value("closing_phrase").toString());
    p.signer_name         = qs(obj.value("signer_name").toString());
    p.signature_image     = qs(obj.value("signature_image").toString());

    auto style_str = obj.value("style").toString().toLower();
    p.style = (style_str == "commercial")
        ? Profile_style::COMMERCIAL : Profile_style::SIMPLE;

    // Commercial fields
    p.logo_image     = qs(obj.value("logo_image").toString());
    p.top_rule_color = json_color(obj, "top_rule_color",      p.top_rule_color);
    p.footer_lines   = json_string_array(obj, "footer_lines");
    p.signer_title   = qs(obj.value("signer_title").toString());

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
    obj.insert("id",                  QString::fromStdString(profile.id));
    obj.insert("style",               profile.style == Profile_style::COMMERCIAL ? "commercial" : "simple");
    obj.insert("sender_lines",        json_string_array(profile.sender_lines));
    obj.insert("email",               QString::fromStdString(profile.email));
    obj.insert("language",            QString::fromStdString(profile.language));
    obj.insert("return_address_line", QString::fromStdString(profile.return_address_line));
    obj.insert("closing_phrase",      QString::fromStdString(profile.closing_phrase));
    obj.insert("signer_name",         QString::fromStdString(profile.signer_name));
    obj.insert("signature_image",     QString::fromStdString(profile.signature_image));
    obj.insert("logo_image",          QString::fromStdString(profile.logo_image));
    obj.insert("top_rule_color",      json_color_array(profile.top_rule_color));
    obj.insert("footer_lines",        json_string_array(profile.footer_lines));
    obj.insert("signer_title",        QString::fromStdString(profile.signer_title));

    QSaveFile file(QString::fromStdString(json_path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = "Cannot open profile for writing: " + json_path;
        }
        return false;
    }

    auto payload = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        if (error) {
            *error = "Failed to write profile: " + json_path;
        }
        return false;
    }

    if (!file.commit()) {
        if (error) {
            *error = "Failed to finalize profile write: " + json_path;
        }
        return false;
    }

    if (error) {
        error->clear();
    }
    return true;
}
