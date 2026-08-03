#include "briefutil/brief_service.h"
#include "briefutil/owned_staging.h"
#include "briefutil/path_utils.h"
#include "briefutil/template_store.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdlib>
#include <cstdio>

static void fail(const char* message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

// Every entry briefutil creates beside the output PDF is hidden, so this covers
// the staging directory and the publish lock without naming either. Directories
// count: a stranded staging directory is exactly the accumulation this is here
// to catch.
static QStringList hidden_leftovers(const QDir& dir)
{
    QStringList leftovers;
    const auto  entries = dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const auto& entry : entries) {
        if (entry.startsWith('.')) {
            leftovers.push_back(entry);
        }
    }
    return leftovers;
}

static QByteArray read_all(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail("could not read a generated PDF");
    }
    return file.readAll();
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    if (briefutil::localized_date(2026, 3, 1, "de") != "1. M\xc3\xa4rz 2026") {
        fail("German March date should use proper UTF-8 spelling");
    }
    if (briefutil::localized_date(2026, 4, 29, "en") != "April 29, 2026") {
        fail("English date should match the CLI/service contract");
    }

    QTemporaryDir root;
    if (!root.isValid()) {
        fail("could not create temporary directory");
    }
    QDir dir(root.path());

    const QString template_dir = root.filePath("templates");
    std::string error;
    if (!briefutil::ensure_template_dir_ready(template_dir.toStdString(), &error)) {
        std::fprintf(stderr, "FAIL: ensure_template_dir_ready: %s\n", error.c_str());
        return 1;
    }

    auto profiles = briefutil::discover_profiles(template_dir.toStdString());
    if (profiles.empty()) {
        fail("seeded profiles were not discovered");
    }

    const QString output_path = root.filePath("letter.pdf");
    briefutil::Generation_request request;
    request.profile.profile = profiles.front().profile;
    request.profile.profile_path = profiles.front().path;
    request.profile.profile_base_dir = profiles.front().base_dir;
    request.recipient = "Recipient\nStreet 1\n12345 City";
    request.subject = "Service Smoke";
    request.body = "Hello from service.";
    request.output_path = output_path.toStdString();
    request.date_year = 2026;
    request.date_month = 4;
    request.date_day = 29;

    auto generated = briefutil::generate_brief_pdf(request);
    if (!generated.ok || !QFileInfo::exists(output_path)) {
        std::fprintf(
            stderr,
            "FAIL: initial generate_brief_pdf failed: %s (%s)\n",
            generated.message.c_str(),
            generated.detail.c_str());
        return 1;
    }
    if (!hidden_leftovers(dir).isEmpty()) {
        fail("initial generation left working files behind");
    }

    const QByteArray original_bytes = read_all(output_path);
    auto exists = briefutil::generate_brief_pdf(request);
    if (exists.ok || exists.code != briefutil::Generation_result_code::OUTPUT_EXISTS) {
        fail("existing output should be rejected without overwrite");
    }
    if (read_all(output_path) != original_bytes) {
        fail("a rejected generation must leave the existing PDF byte-identical");
    }
    if (!hidden_leftovers(dir).isEmpty()) {
        fail("rejected generation left working files behind");
    }

    request.overwrite_output = true;
    auto overwritten = briefutil::generate_brief_pdf(request);
    if (!overwritten.ok || !QFileInfo::exists(output_path)) {
        std::fprintf(
            stderr,
            "FAIL: overwrite generate_brief_pdf failed: %s (%s)\n",
            overwritten.message.c_str(),
            overwritten.detail.c_str());
        return 1;
    }

    const auto leftovers = hidden_leftovers(dir);
    if (!leftovers.isEmpty()) {
        std::fprintf(
            stderr,
            "FAIL: overwrite path left working files behind: %s\n",
            leftovers.join(", ").toUtf8().constData());
        return 1;
    }

    // Nothing in the user's output directory is briefutil's to delete, whatever
    // it is called. One of these two files wears the exact shape briefutil once
    // used for its own staging files; the other is ordinary content. The oracle
    // is consent: briefutil may delete only what it created, and a filename is
    // not proof that it created anything. That binds on the refusal path too -
    // a request briefutil declines must not touch the directory on its way out.
    {
        const QDir user_dir(root.filePath("user-files"));
        if (!QDir().mkpath(user_dir.absolutePath())) {
            fail("could not create the user output directory");
        }

        const QByteArray  user_bytes = "SENTINEL-USER-DATA-DO-NOT-DELETE";
        const QStringList user_files = {
            user_dir.filePath(".letter.pdf.tmp.my-notes"),
            user_dir.filePath("notes.txt"),
        };
        for (const auto& user_file : user_files) {
            QFile file(user_file);
            if (!file.open(QIODevice::WriteOnly) || file.write(user_bytes) != user_bytes.size()) {
                fail("could not create a user file in the output directory");
            }
        }

        auto user_files_intact = [&](const char* stage) {
            for (const auto& user_file : user_files) {
                QFile file(user_file);
                if (!file.open(QIODevice::ReadOnly) || file.readAll() != user_bytes) {
                    std::fprintf(
                        stderr,
                        "FAIL: %s did not leave a user file intact: %s\n",
                        stage,
                        user_file.toUtf8().constData());
                    return false;
                }
            }
            return true;
        };

        auto user_request = request;
        user_request.output_path      = user_dir.filePath("letter.pdf").toStdString();
        user_request.overwrite_output = false;

        auto user_published = briefutil::generate_brief_pdf(user_request);
        if (!user_published.ok) {
            std::fprintf(
                stderr,
                "FAIL: generation beside user files failed: %s (%s)\n",
                user_published.message.c_str(),
                user_published.detail.c_str());
            return 1;
        }
        if (!user_files_intact("a successful generation")) {
            return 1;
        }

        auto user_refused = briefutil::generate_brief_pdf(user_request);
        if (user_refused.ok ||
            user_refused.code != briefutil::Generation_result_code::OUTPUT_EXISTS)
        {
            fail("a second generation beside user files should be refused");
        }
        if (!user_files_intact("a refused generation")) {
            return 1;
        }

        user_request.overwrite_output = true;
        auto user_overwritten = briefutil::generate_brief_pdf(user_request);
        if (!user_overwritten.ok) {
            std::fprintf(
                stderr,
                "FAIL: overwriting generation beside user files failed: %s (%s)\n",
                user_overwritten.message.c_str(),
                user_overwritten.detail.c_str());
            return 1;
        }
        if (!user_files_intact("an overwriting generation")) {
            return 1;
        }
    }

    // A run killed before publishing leaves its staged file behind. The next run
    // for that target reclaims it, so debris cannot accumulate; and it reclaims
    // only that, because another target's staging slot is not its to empty.
    {
        const QDir reclaim_dir(root.filePath("reclaim"));
        if (!QDir().mkpath(reclaim_dir.absolutePath())) {
            fail("could not create the reclamation output directory");
        }

        auto abandon_staged_file = [&](const QString& target_name) {
            QString staged_path;
            {
                briefutil::Owned_staging_slot slot;
                std::string                   slot_error;
                if (!slot.open(reclaim_dir, target_name, &slot_error)) {
                    fail("could not open an owned staging slot");
                }
                staged_path = slot.staged_path();
            }
            // The slot above discards itself, so put back exactly what a run
            // killed between staging and publishing leaves: the slot directory
            // and one staged file inside it.
            if (!QDir().mkpath(QFileInfo(staged_path).absolutePath())) {
                fail("could not recreate an abandoned staging slot");
            }
            QFile staged(staged_path);
            if (!staged.open(QIODevice::WriteOnly) || staged.write("stale", 5) != 5) {
                fail("could not recreate an abandoned staged file");
            }
            return staged_path;
        };

        const QString own_debris   = abandon_staged_file("letter.pdf");
        const QString other_debris = abandon_staged_file("unrelated.pdf");

        auto reclaim_request = request;
        reclaim_request.output_path      = reclaim_dir.filePath("letter.pdf").toStdString();
        reclaim_request.overwrite_output = false;

        auto reclaimed = briefutil::generate_brief_pdf(reclaim_request);
        if (!reclaimed.ok) {
            std::fprintf(
                stderr,
                "FAIL: generation over abandoned staging debris failed: %s (%s)\n",
                reclaimed.message.c_str(),
                reclaimed.detail.c_str());
            return 1;
        }
        if (QFileInfo::exists(own_debris)) {
            fail("a dead run's staged file must be reclaimed by the next run for that target");
        }
        if (!QFileInfo::exists(other_debris)) {
            fail("reclamation must stop at the staging slot of the target being published");
        }
    }

    // Two runs cannot be in one slot at once. Emptying the slot on open is what
    // reclaims a dead run's debris without reading any name as proof of who
    // wrote it, so an open granted while another run still held the slot would
    // delete that run's staged file out from under it. The second open is
    // refused instead, and the file the holder staged is still there afterwards.
    {
        const QDir contended_dir(root.filePath("contended"));
        if (!QDir().mkpath(contended_dir.absolutePath())) {
            fail("could not create the contended staging directory");
        }

        briefutil::Owned_staging_slot held;
        std::string                   held_error;
        if (!held.open(contended_dir, "letter.pdf", &held_error)) {
            fail("could not open the first staging slot");
        }
        QFile staged(held.staged_path());
        if (!staged.open(QIODevice::WriteOnly) || staged.write("in progress", 11) != 11) {
            fail("could not write into the first staging slot");
        }
        staged.close();

        briefutil::Owned_staging_slot contender;
        if (contender.open(contended_dir, "letter.pdf", nullptr)) {
            fail("a slot another run is holding must not be granted a second time");
        }

        QFile still_staged(held.staged_path());
        if (!still_staged.open(QIODevice::ReadOnly) || still_staged.readAll() != "in progress") {
            fail("a refused slot open must leave the holder's staged file untouched");
        }
    }

    // A subject long enough to push the derived output name past a directory
    // entry's byte limit must still produce a usable file.
    auto long_subject = request;
    long_subject.subject          = std::string(500, 'S');
    long_subject.output_path.clear();
    long_subject.output_dir       = dir.filePath("long").toStdString();
    long_subject.overwrite_output = false;
    auto long_subject_result = briefutil::generate_brief_pdf(long_subject);
    if (!long_subject_result.ok) {
        std::fprintf(
            stderr,
            "FAIL: long subject generation failed: %s (%s)\n",
            long_subject_result.message.c_str(),
            long_subject_result.detail.c_str());
        return 1;
    }
    {
        // "yyyy-MM-dd HH-mm-ss-zzz " (24) + the sanitized stem + ".pdf".
        const size_t max_name_bytes = 24 + briefutil::k_max_filename_component_bytes + 4;
        const QString produced =
            QFileInfo(QString::fromStdString(long_subject_result.output_path)).fileName();
        if (static_cast<size_t>(produced.toUtf8().size()) > max_name_bytes) {
            fail("a long subject produced an unbounded output filename");
        }
    }

    auto invalid = request;
    invalid.profile.profile.id.clear();
    invalid.output_path = root.filePath("invalid.pdf").toStdString();
    invalid.overwrite_output = false;
    auto invalid_result = briefutil::generate_brief_pdf(invalid);
    if (invalid_result.ok ||
        invalid_result.code != briefutil::Generation_result_code::INVALID_REQUEST)
    {
        fail("empty profile id should be INVALID_REQUEST");
    }

    invalid = request;
    invalid.output_path.clear();
    invalid.output_dir.clear();
    invalid.overwrite_output = false;
    invalid_result = briefutil::generate_brief_pdf(invalid);
    if (invalid_result.ok ||
        invalid_result.code != briefutil::Generation_result_code::INVALID_REQUEST)
    {
        fail("missing output target should be INVALID_REQUEST");
    }

    invalid = request;
    invalid.output_path = root.filePath("bad-font.pdf").toStdString();
    invalid.overwrite_output = false;
    invalid.theme.fonts.sans = "unsupported.otf";
    invalid_result = briefutil::generate_brief_pdf(invalid);
    if (invalid_result.ok ||
        invalid_result.code != briefutil::Generation_result_code::INVALID_FONT_CONFIG)
    {
        fail("invalid font config should be INVALID_FONT_CONFIG");
    }

    // A fully specified but impossible calendar date (Feb 31) must be rejected
    // rather than rendered verbatim.
    invalid = request;
    invalid.output_path = root.filePath("bad-date.pdf").toStdString();
    invalid.overwrite_output = false;
    invalid.date_year  = 2026;
    invalid.date_month = 2;
    invalid.date_day   = 31;
    invalid_result = briefutil::generate_brief_pdf(invalid);
    if (invalid_result.ok ||
        invalid_result.code != briefutil::Generation_result_code::INVALID_REQUEST)
    {
        fail("impossible calendar date should be INVALID_REQUEST");
    }

    invalid = request;
    invalid.output_path = root.filePath("partial-date-year.pdf").toStdString();
    invalid.overwrite_output = false;
    invalid.date_year  = 2026;
    invalid.date_month = 0;
    invalid.date_day   = 0;
    invalid_result = briefutil::generate_brief_pdf(invalid);
    if (invalid_result.ok ||
        invalid_result.code != briefutil::Generation_result_code::INVALID_REQUEST)
    {
        fail("date with only year set should be INVALID_REQUEST");
    }

    auto fallback = request;
    fallback.output_path = root.filePath("fallback-date.pdf").toStdString();
    fallback.overwrite_output = false;
    fallback.date_year  = 0;
    fallback.date_month = 0;
    fallback.date_day   = 0;
    auto fallback_result = briefutil::generate_brief_pdf(fallback);
    if (!fallback_result.ok || !QFileInfo::exists(QString::fromStdString(fallback.output_path))) {
        std::fprintf(
            stderr,
            "FAIL: fully unset date should fall back to today: %s (%s)\n",
            fallback_result.message.c_str(),
            fallback_result.detail.c_str());
        return 1;
    }

    return 0;
}
