#pragma once

#include "briefutil/sender_profile.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QString>

#include <string>
#include <utility>


// ============================================================================
// Test helpers (header-only, shared across the test executables)
// ============================================================================

inline std::string qs(const QString& s)
{
    return s.toStdString();
}

// RAII test fixture: creates a temp directory, seeds it with a profile JSON
// payload, and loads the profile. Cleans up the JSON file and the directory
// on destruction.
//
// Usage:
//     Profile_fixture fx("briefutil_test_simple",
//                        k_default_profile_simple_json);
//     if (!fx.ok) {
//         std::fprintf(stderr, "FAIL: setup: %s\n", fx.error.c_str());
//         return 1;
//     }
//     fx.profile.signature_image.clear(); // when the test has no signature PNG
//     auto br = build_letter(fx.profile, input, qs(fx.tmp_dir));
//
class Profile_fixture
{
public:
    Profile_fixture(const char*        unique_name,
                    const QByteArray&  json_payload)
    {
        tmp_dir      = QDir::tempPath() + "/" + QString::fromUtf8(unique_name);
        profile_path = tmp_dir + "/test_profile.json";

        QDir().mkpath(tmp_dir);

        QFile out(profile_path);
        if (!out.open(QIODevice::WriteOnly)) {
            error = "cannot write profile to " + qs(profile_path);
            return;
        }
        out.write(json_payload);
        out.close();

        auto loaded = load_sender_profile(qs(profile_path));
        if (!loaded.ok) {
            error = loaded.error;
            return;
        }
        profile = std::move(loaded.profile);
        ok      = true;
    }

    ~Profile_fixture()
    {
        if (!tmp_dir.isEmpty()) {
            // removeRecursively handles tests that write additional files
            // (PDFs, signature PNGs) into tmp_dir alongside the profile JSON.
            QDir(tmp_dir).removeRecursively();
        }
    }

    Profile_fixture(const Profile_fixture&)            = delete;
    Profile_fixture& operator=(const Profile_fixture&) = delete;

    QString          tmp_dir;
    QString          profile_path;
    Sender_profile   profile;
    bool             ok    = false;
    std::string      error;
};
