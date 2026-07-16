#include "proxy.h"
#include "briefutil/sender_profile.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QVariantMap>

#include <cstdio>
#include <cstdlib>
#include <string>

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static std::string qs(const QString& s)
{
    return s.toStdString();
}

static QJsonObject read_json_object(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "could not read profile JSON");

    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    require(error.error == QJsonParseError::NoError, "profile JSON did not parse");
    require(doc.isObject(), "profile JSON should be an object");
    return doc.object();
}

static QVariantMap editable_profile_payload(const Proxy& proxy)
{
    auto payload = proxy.get_sender_profile(0);
    require(!payload.isEmpty(), "proxy should return an editable profile");
    return payload;
}

static void require_simple_save_drops_stale_invalid_logo()
{
    QTemporaryDir root;
    require(root.isValid(), "could not create temporary directory");

    const QString profile_path = root.filePath("stale-logo.json");
    Sender_profile initial;
    initial.id            = "Stale Logo";
    initial.language      = "en";
    initial.style         = Profile_style::SIMPLE;
    initial.sender_lines  = { "Sender" };

    std::string error;
    require(save_sender_profile(initial, qs(profile_path), &error),
        "could not write initial profile fixture");

    const QByteArray template_dir = root.path().toLocal8Bit();
    const QByteArray output_dir   = root.filePath("output").toLocal8Bit();
    qputenv("BRIEFUTIL_TEMPLATE_DIR", template_dir);
    qputenv("BRIEFUTIL_OUTPUT_DIR", output_dir);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());

    Proxy proxy;
    require(proxy.get_sender_templates().size() == 1,
        "proxy should discover the initial profile");
    if (proxy.validate_font_value("Noto Sans", "sans") &&
        proxy.validate_font_value("Noto Sans", "sans_bold") &&
        proxy.validate_font_value("Noto Sans", "sans_italic") &&
        proxy.validate_font_value("Noto Sans", "sans_bold_italic"))
    {
        require(proxy.get_font_sans()             == "Noto Sans" &&
                proxy.get_font_sans_bold()        == "Noto Sans" &&
                proxy.get_font_sans_italic()      == "Noto Sans" &&
                proxy.get_font_sans_bold_italic() == "Noto Sans",
            "proxy should prefer the installed print font for a new configuration");
    }

    auto simple_payload = editable_profile_payload(proxy);
    simple_payload.insert("style", "simple");
    simple_payload.insert("logoImage", "C:/Windows/system32/evil.png");
    require(proxy.save_sender_profile(0, simple_payload),
        "simple proxy save should ignore a stale invalid logo image");

    const auto saved_json = read_json_object(profile_path);
    require(saved_json.value("style").toString() == "simple",
        "simple proxy save should keep simple style");
    require(saved_json.value("logo_image").toString().isEmpty(),
        "simple proxy save should clear stale logo_image");

    auto loaded = load_sender_profile(qs(profile_path));
    require(loaded.ok, "saved simple profile should reload with strict loader");
    require(loaded.profile.logo_image.empty(),
        "strict reload should see an empty simple logo image");

    auto saved_payload = proxy.get_sender_profile(0);
    require(saved_payload.value("logoImage").toString().isEmpty(),
        "proxy in-memory profile should also clear the simple logo image");

    auto commercial_payload = editable_profile_payload(proxy);
    commercial_payload.insert("style", "commercial");
    commercial_payload.insert("logoImage", "C:/Windows/system32/evil.png");
    commercial_payload.insert("topRuleColor", "#C8C8C8");
    require(!proxy.save_sender_profile(0, commercial_payload),
        "commercial proxy save should reject an invalid logo image");
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    require_simple_save_drops_stale_invalid_logo();

    std::printf("All proxy sender profile tests passed.\n");
    return 0;
}
