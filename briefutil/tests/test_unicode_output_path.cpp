#include "briefutil/default_profiles.h"
#include "briefutil/letter_builder.h"
#include "briefutil/sender_profile.h"

#include "test_helpers.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>

#include <cstdio>
#include <string>


int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    Profile_fixture fx("briefutil_unicode_output_test", k_default_profile_simple_json);
    if (!fx.ok) {
        std::fprintf(stderr, "FAIL: profile setup: %s\n", fx.error.c_str());
        return 1;
    }
    fx.profile.signature_image.clear();

    const QString unicode_subject = QString::fromUtf8(
        "Bitte um " "\xC3\x9C" "berpr" "\xC3\xBC" "fung der Beitragsanpassung");

    Letter_input input;
    input.recipient = "Versicherung AG\nLeistungsabteilung\nBeispielweg 7\n12345 Berlin";
    input.subject   = unicode_subject.toUtf8().toStdString();
    input.date      = "27. M\xC3\xA4rz 2026";
    input.body      = "Dies ist ein kurzer Testbrief.";

    QString output_path = fx.tmp_dir + "/" + unicode_subject + ".pdf";
    QFile::remove(output_path);

    auto rendered = generate_letter_pdf(fx.profile, input, qs(fx.tmp_dir), qs(output_path));
    if (!rendered.ok) {
        std::fprintf(
            stderr,
            "FAIL: generate_letter_pdf failed: %s (%s)\n",
            rendered.message.c_str(),
            rendered.detail.c_str());
        return 1;
    }

    QFileInfo info(output_path);
    if (!info.exists()) {
        std::fprintf(stderr, "FAIL: output PDF does not exist at expected Unicode path\n");
        return 1;
    }

    if (info.fileName() != unicode_subject + ".pdf") {
        std::fprintf(stderr, "FAIL: output filename was mangled\n");
        return 1;
    }

    std::printf("[OK] PDF rendered to Unicode filename: %s\n", qPrintable(output_path));
    return 0;
}
