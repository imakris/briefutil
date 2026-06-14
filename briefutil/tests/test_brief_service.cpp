#include "briefutil/brief_service.h"
#include "briefutil/template_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdlib>
#include <cstdio>

static void fail(const char* message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
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
    if (!dir.entryList({ "*.tmp.*", "*.replace.*", ".*.tmp.*", ".*.replace.*" },
            QDir::Files | QDir::Hidden).isEmpty())
    {
        fail("initial generation left temporary files behind");
    }

    auto exists = briefutil::generate_brief_pdf(request);
    if (exists.ok || exists.code != briefutil::Generation_result_code::OUTPUT_EXISTS) {
        fail("existing output should be rejected without overwrite");
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

    const auto leftovers = dir.entryList(
        { "*.tmp.*", "*.replace.*", ".*.tmp.*", ".*.replace.*" },
        QDir::Files | QDir::Hidden);
    if (!leftovers.isEmpty()) {
        fail("overwrite path left temporary files behind");
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

    invalid = request;
    invalid.output_path = root.filePath("partial-date-year-month.pdf").toStdString();
    invalid.overwrite_output = false;
    invalid.date_year  = 2026;
    invalid.date_month = 4;
    invalid.date_day   = 0;
    invalid_result = briefutil::generate_brief_pdf(invalid);
    if (invalid_result.ok ||
        invalid_result.code != briefutil::Generation_result_code::INVALID_REQUEST)
    {
        fail("date with year and month set should be INVALID_REQUEST");
    }

    invalid = request;
    invalid.output_path = root.filePath("partial-date-invalid-month.pdf").toStdString();
    invalid.overwrite_output = false;
    invalid.date_year  = 2026;
    invalid.date_month = 13;
    invalid.date_day   = 0;
    invalid_result = briefutil::generate_brief_pdf(invalid);
    if (invalid_result.ok ||
        invalid_result.code != briefutil::Generation_result_code::INVALID_REQUEST)
    {
        fail("date with invalid month and unset day should be INVALID_REQUEST");
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
