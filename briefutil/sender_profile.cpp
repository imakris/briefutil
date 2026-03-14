#include "sender_profile.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// Use Qt's JSON parser — already available since the app links Qt6::Core.


static std::string qs(const QString& s) { return s.toStdString(); }

static std::vector<std::string> json_string_array(const QJsonObject& obj,
                                                   const char* key)
{
    std::vector<std::string> result;
    auto arr = obj.value(key).toArray();
    for (const auto& v : arr) {
        result.push_back(qs(v.toString()));
    }
    return result;
}

static color_t json_color(const QJsonObject& obj, const char* key,
                          color_t fallback)
{
    auto arr = obj.value(key).toArray();
    if (arr.size() != 3) return fallback;
    return {
        (float)arr[0].toInt() / 255.0f,
        (float)arr[1].toInt() / 255.0f,
        (float)arr[2].toInt() / 255.0f,
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

    p.id                  = qs(obj.value("id").toString());
    p.sender_lines        = json_string_array(obj, "sender_lines");
    p.email               = qs(obj.value("email").toString());
    p.return_address_line  = qs(obj.value("return_address_line").toString());
    p.signer_name         = qs(obj.value("signer_name").toString());
    p.signature_image     = qs(obj.value("signature_image").toString());

    auto style_str = obj.value("style").toString().toLower();
    p.style = (style_str == "commercial")
        ? Profile_style::COMMERCIAL : Profile_style::SIMPLE;

    // Commercial fields
    p.company_name        = qs(obj.value("company_name").toString());
    p.company_name_color  = json_color(obj, "company_name_color",  p.company_name_color);
    p.top_rule_color      = json_color(obj, "top_rule_color",      p.top_rule_color);
    p.footer_lines        = json_string_array(obj, "footer_lines");
    p.signer_title        = qs(obj.value("signer_title").toString());

    if (p.id.empty()) {
        return { false, {}, "Profile missing 'id' field: " + json_path };
    }

    return { true, std::move(p), "" };
}
