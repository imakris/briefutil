#include "briefutil/sender_profile.h"
#include "briefutil/sender_profile_schema.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>


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

struct Expected_string_field
{
    const char*                   json_key;
    const char*                   qml_key;
    std::string Sender_profile::* member;
    const char*                   value;
};

struct Expected_string_array_field
{
    const char*                                json_key;
    const char*                                qml_key;
    std::vector<std::string> Sender_profile::* member;
};

static const Sender_string_field* find_string_field(const char* json_key)
{
    for (const auto& f : k_sender_string_fields) {
        if (std::strcmp(f.json_key, json_key) == 0) {
            return &f;
        }
    }
    return nullptr;
}

static const Sender_string_array_field* find_string_array_field(const char* json_key)
{
    for (const auto& f : k_sender_string_array_fields) {
        if (std::strcmp(f.json_key, json_key) == 0) {
            return &f;
        }
    }
    return nullptr;
}

static void require_unique_schema_keys()
{
    std::set<std::string> json_keys;
    std::set<std::string> qml_keys;

    for (const auto& f : k_sender_string_fields) {
        require(json_keys.insert(f.json_key).second, "duplicate sender string JSON key");
        require(qml_keys.insert(f.qml_key).second, "duplicate sender string QML key");
        require(std::strcmp(f.json_key, "language") != 0, "language should be explicit");
    }
    for (const auto& f : k_sender_string_array_fields) {
        require(json_keys.insert(f.json_key).second, "duplicate sender array JSON key");
        require(qml_keys.insert(f.qml_key).second, "duplicate sender array QML key");
        require(std::strcmp(f.json_key, "language") != 0, "language should be explicit");
    }
}

static void require_schema_maps_expected_fields()
{
    Sender_profile profile;
    profile.id                  = "Profile Id";
    profile.email               = "profile@example.org";
    profile.language            = "de";
    profile.return_address_line = "Return Address";
    profile.closing_phrase      = "Warm regards,";
    profile.signer_name         = "Signer Name";
    profile.signature_image     = "signature.png";
    profile.logo_image          = "logo.png";
    profile.signer_title        = "Title";
    profile.sender_lines        = { "Sender 1", "Sender 2" };
    profile.footer_lines        = { "Footer 1", "Footer 2" };

    const Expected_string_field expected_strings[] = {
        { "id",                  "id",                &Sender_profile::id,                  "Profile Id"          },
        { "email",               "email",             &Sender_profile::email,               "profile@example.org" },
        { "return_address_line", "returnAddressLine", &Sender_profile::return_address_line, "Return Address"      },
        { "closing_phrase",      "closingPhrase",     &Sender_profile::closing_phrase,      "Warm regards,"       },
        { "signer_name",         "signerName",        &Sender_profile::signer_name,         "Signer Name"         },
        { "signature_image",     "signatureImage",    &Sender_profile::signature_image,     "signature.png"       },
        { "logo_image",          "logoImage",         &Sender_profile::logo_image,          "logo.png"            },
        { "signer_title",        "signerTitle",       &Sender_profile::signer_title,        "Title"               },
    };
    const Expected_string_array_field expected_arrays[] = {
        { "sender_lines", "senderLines", &Sender_profile::sender_lines },
        { "footer_lines", "footerLines", &Sender_profile::footer_lines },
    };

    require(
        sizeof(k_sender_string_fields) / sizeof(k_sender_string_fields[0])
            == sizeof(expected_strings) / sizeof(expected_strings[0]),
        "unexpected sender string schema field count");
    require(
        sizeof(k_sender_string_array_fields) / sizeof(k_sender_string_array_fields[0])
            == sizeof(expected_arrays) / sizeof(expected_arrays[0]),
        "unexpected sender array schema field count");

    for (const auto& expected : expected_strings) {
        const auto* actual = find_string_field(expected.json_key);
        require(actual != nullptr, "missing sender string schema field");
        require(std::strcmp(actual->qml_key, expected.qml_key) == 0, "wrong sender string QML key");
        require(actual->member == expected.member, "wrong sender string member pointer");
        require(profile.*actual->member == expected.value, "wrong sender string member value");
    }

    for (const auto& expected : expected_arrays) {
        const auto* actual = find_string_array_field(expected.json_key);
        require(actual != nullptr, "missing sender array schema field");
        require(std::strcmp(actual->qml_key, expected.qml_key) == 0, "wrong sender array QML key");
        require(actual->member == expected.member, "wrong sender array member pointer");
        require(!(profile.*actual->member).empty(), "wrong sender array member value");
    }
}

static QJsonObject read_json_object(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "could not read saved profile JSON");

    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    require(error.error == QJsonParseError::NoError, "saved profile JSON did not parse");
    require(doc.isObject(), "saved profile JSON should be an object");
    return doc.object();
}

static void require_language_stays_explicit()
{
    QTemporaryDir root;
    require(root.isValid(), "could not create temporary directory");

    const QString missing_language_path = root.filePath("missing_language.json");
    QFile missing_language(missing_language_path);
    require(missing_language.open(QIODevice::WriteOnly), "could not write missing-language profile");
    missing_language.write(R"({
        "id": "Missing Language",
        "sender_lines": ["Sender"],
        "style": "simple"
    })");
    missing_language.close();

    auto loaded_missing = load_sender_profile(qs(missing_language_path));
    require(loaded_missing.ok, "missing-language profile should load");
    require(loaded_missing.profile.language == "en", "missing language should default to English");

    Sender_profile profile;
    profile.id       = "Explicit Language";
    profile.language = "de";

    const QString saved_path = root.filePath("explicit_language.json");
    std::string   error;
    require(save_sender_profile(profile, qs(saved_path), &error), "could not save profile");

    const auto saved = read_json_object(saved_path);
    require(saved.value("language").toString() == "de", "saved profile should include language");

    auto loaded = load_sender_profile(qs(saved_path));
    require(loaded.ok, "explicit-language profile should load");
    require(loaded.profile.language == "de", "explicit language should round-trip");
}

static void require_invalid_profiles_rejected()
{
    QTemporaryDir root;
    require(root.isValid(), "could not create temporary directory");

    auto write_profile = [&](const char* name, const char* json) -> QString {
        const QString path = root.filePath(name);
        QFile file(path);
        require(file.open(QIODevice::WriteOnly), "could not write profile fixture");
        file.write(json);
        file.close();
        return path;
    };
    auto loads = [&](const char* name, const char* json) -> bool {
        return load_sender_profile(qs(write_profile(name, json))).ok;
    };

    require(!loads("traversal_image.json", R"({
        "id": "Traversal Image",
        "style": "simple",
        "signature_image": "../secret.png"
    })"), "signature image escaping the profile dir must be rejected");
    require(!loads("absolute_image.json", R"({
        "id": "Absolute Image",
        "style": "simple",
        "logo_image": "C:/Windows/system32/evil.png"
    })"), "absolute logo image path must be rejected");
    require(!loads("non_png_image.json", R"({
        "id": "Non Png Image",
        "style": "simple",
        "signature_image": "signature.jpg"
    })"), "non-.png signature image must be rejected");
    require(!loads("bad_color_range.json", R"({
        "id": "Bad Color Range",
        "style": "commercial",
        "top_rule_color": [200, 300, 200]
    })"), "out-of-range colour channel must be rejected");
    require(!loads("bad_color_size.json", R"({
        "id": "Bad Color Size",
        "style": "commercial",
        "top_rule_color": [200, 200]
    })"), "wrong-size colour array must be rejected");
    require(!loads("bad_color_type.json", R"({
        "id": "Bad Color Type",
        "style": "commercial",
        "top_rule_color": [200, "x", 200]
    })"), "non-integer colour channel must be rejected");

    auto valid = load_sender_profile(qs(write_profile("valid_commercial.json", R"({
        "id": "Valid Commercial",
        "style": "commercial",
        "signature_image": "signature.png",
        "logo_image": "logo.png",
        "top_rule_color": [10, 20, 30]
    })")));
    require(valid.ok, "valid commercial profile must load");
    require(valid.profile.signature_image == "signature.png",
        "valid profile should keep its signature image");
    require(valid.profile.logo_image == "logo.png",
        "valid profile should keep its logo image");

    // A minimal profile with no image fields and no colour key must load: an
    // empty image name is valid (no image), and an absent top_rule_color keeps
    // the struct default rather than being treated as malformed.
    auto minimal = load_sender_profile(qs(write_profile("minimal.json", R"({
        "id": "Minimal",
        "style": "simple"
    })")));
    require(minimal.ok, "minimal profile without image or colour must load");
    require(minimal.profile.signature_image.empty(),
        "minimal profile should have no signature image");
    require(minimal.profile.top_rule_color.r == 200 / 255.0f,
        "absent top_rule_color should keep the struct default");
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    require_unique_schema_keys();
    require_schema_maps_expected_fields();
    require_language_stays_explicit();
    require_invalid_profiles_rejected();

    std::printf("All sender profile schema tests passed.\n");
    return 0;
}
