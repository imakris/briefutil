#include "briefutil/default_profiles.h"
#include "briefutil/letter_builder.h"
#include "briefutil/pdf_backend.h"
#include "briefutil/pdf_measurement.h"
#include "briefutil/sender_profile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>
#include <string>


static std::string qs(const QString& s)
{
    return s.toStdString();
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QString tmp_dir = QDir::tempPath() + "/briefutil_unicode_output_test";
    auto cleanup = [&]() {
        QDir(tmp_dir).removeRecursively();
    };
    QDir().mkpath(tmp_dir);

    QString profile_path = tmp_dir + "/profile.json";
    QFile profile_file(profile_path);
    if (!profile_file.open(QIODevice::WriteOnly)) {
        std::fprintf(stderr, "FAIL: cannot write temp profile\n");
        cleanup();
        return 1;
    }
    profile_file.write(k_default_profile_simple_json);
    profile_file.close();

    auto loaded = load_sender_profile(qs(profile_path));
    if (!loaded.ok) {
        std::fprintf(stderr, "FAIL: profile load failed: %s\n", loaded.error.c_str());
        cleanup();
        return 1;
    }

    loaded.profile.signature_image.clear();

    const QString unicode_subject = QString::fromUtf8(
        "Bitte um "
        "\xC3\x9C"
        "berpr"
        "\xC3\xBC"
        "fung der Beitragsanpassung");

    Letter_input input;
    input.recipient = "Versicherung AG\nLeistungsabteilung\nBeispielweg 7\n12345 Berlin";
    input.subject = unicode_subject.toUtf8().toStdString();
    input.date = "27. M\xC3\xA4rz 2026";
    input.body = "Dies ist ein kurzer Testbrief.";

    QString output_path = tmp_dir + "/" + unicode_subject + ".pdf";
    QFile::remove(output_path);

    auto rendered = generate_letter_pdf(loaded.profile, input, qs(tmp_dir), qs(output_path));
    if (!rendered.ok) {
        std::fprintf(stderr, "FAIL: generate_letter_pdf failed: %s (%s)\n",
                     rendered.message.c_str(), rendered.detail.c_str());
        cleanup();
        return 1;
    }

    QFileInfo info(output_path);
    if (!info.exists()) {
        std::fprintf(stderr, "FAIL: output PDF does not exist at expected Unicode path\n");
        cleanup();
        return 1;
    }

    if (info.fileName() != unicode_subject + ".pdf") {
        std::fprintf(stderr, "FAIL: output filename was mangled\n");
        cleanup();
        return 1;
    }

    std::printf("[OK] PDF rendered to Unicode filename: %s\n", qPrintable(output_path));

    std::string mark2_detail;
    if (pdf_measurement_ready(Pdf_backend::Mark2Haru, default_font_family(), &mark2_detail)) {
        QString mark2_output_path = tmp_dir + "/" + unicode_subject + ".mark2haru.pdf";
        QFile::remove(mark2_output_path);
        auto rendered2 = generate_letter_pdf(loaded.profile, input, qs(tmp_dir),
                                             qs(mark2_output_path), default_theme(),
                                             din_5008_form_b(), default_localization(),
                                             Pdf_backend::Mark2Haru);
        if (!rendered2.ok) {
            std::fprintf(stderr, "FAIL: mark2haru generate_letter_pdf failed: %s (%s)\n",
                         rendered2.message.c_str(), rendered2.detail.c_str());
            cleanup();
            return 1;
        }
        QFileInfo info2(mark2_output_path);
        if (!info2.exists()) {
            std::fprintf(stderr, "FAIL: mark2haru output PDF does not exist at expected Unicode path\n");
            cleanup();
            return 1;
        }
        std::printf("[OK] mark2haru rendered to Unicode filename: %s\n",
                    qPrintable(mark2_output_path));
    }

    cleanup();
    return 0;
}
