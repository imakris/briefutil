#include "proxy.h"
#include "briefutil/sender_profile.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
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

static void write_file(const QString& path, const QByteArray& bytes, const char* what)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), what);
    require(file.write(bytes) == bytes.size(), what);
}

static QByteArray read_file(const QString& path, const char* what)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), what);
    return file.readAll();
}

// Nothing in the user's template directory is briefutil's to delete, whatever
// it is called. The file below wears the exact name the importer derives for
// its own working copy, and it is the user's. The oracle is consent: briefutil
// may delete only what it created, and a filename is not proof that it created
// anything.
static void require_import_leaves_a_matching_user_file_alone()
{
    QTemporaryDir root;
    require(root.isValid(), "could not create temporary directory");

    const QString template_dir = root.filePath("templates");
    require(QDir().mkpath(template_dir), "could not create the template directory");

    qputenv("BRIEFUTIL_TEMPLATE_DIR", template_dir.toLocal8Bit());
    qputenv("BRIEFUTIL_OUTPUT_DIR", root.filePath("output").toLocal8Bit());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());

    Proxy proxy;

    const QByteArray sentinel  = "SENTINEL-USER-DATA-DO-NOT-DELETE";
    const QString    user_path = QDir(template_dir).filePath("signature.png.importing");
    write_file(user_path, sentinel, "could not create the user file");

    const QByteArray image      = "PNG-PAYLOAD-BYTES";
    const QString    source_dir = root.filePath("source");
    require(QDir().mkpath(source_dir), "could not create the import source directory");
    const QString source_path = QDir(source_dir).filePath("signature.png");
    write_file(source_path, image, "could not create the import source");

    const QString imported = proxy.import_template_image(QUrl::fromLocalFile(source_path));
    require(imported == "signature.png", "importing should claim the free asset name");
    require(
        read_file(QDir(template_dir).filePath(imported), "the imported asset should exist") == image,
        "the imported asset should hold the source bytes");

    require(
        read_file(user_path, "importing must not delete a user file") == sentinel,
        "importing must leave a user file byte-identical");
    require(
        !QDir(template_dir).exists(".briefutil-staging"),
        "importing must not leave its staging directory behind");
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    require_simple_save_drops_stale_invalid_logo();
    require_import_leaves_a_matching_user_file_alone();

    std::printf("All proxy sender profile tests passed.\n");
    return 0;
}
