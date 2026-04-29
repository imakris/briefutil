#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <cstdlib>
#include <cstdio>

#ifndef BRIEFUTIL_CLI_PATH
#error "BRIEFUTIL_CLI_PATH must point at the briefutil_cli executable"
#endif

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static int run_cli(
    const QStringList& args,
    const QString& template_dir,
    const QString& output_dir,
    QString* standard_error = nullptr,
    QString* standard_output = nullptr)
{
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("BRIEFUTIL_TEMPLATE_DIR", template_dir);
    env.insert("BRIEFUTIL_OUTPUT_DIR", output_dir);
    process.setProcessEnvironment(env);
    process.start(QString::fromUtf8(BRIEFUTIL_CLI_PATH), args);
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished();
        return -1;
    }
    if (standard_error) {
        *standard_error = QString::fromUtf8(process.readAllStandardError());
    }
    if (standard_output) {
        *standard_output = QString::fromUtf8(process.readAllStandardOutput());
    }
    return process.exitCode();
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir root;
    require(root.isValid(), "could not create temporary directory");

    const QString template_dir = root.filePath("templates");
    const QString output_dir = root.filePath("output");
    QDir().mkpath(template_dir);

    QString standard_output;
    int exit_code = run_cli(
        {
            "--to",
            "Ioannis Makris\\nAm Zirkus 3\\n10117 Berlin",
            "--subject",
            "CLI Test",
            "--body",
            "Hello from the CLI test.",
        },
        template_dir,
        output_dir,
        nullptr,
        &standard_output);
    require(exit_code == 0, "CLI smoke should exit successfully");
    require(
        standard_output.trimmed().split('\n', Qt::SkipEmptyParts).size() == 1,
        "CLI smoke should print exactly one generated path");
    require(
        QFile::exists(standard_output.trimmed()),
        "CLI smoke stdout path should exist");
    require(
        !QDir(output_dir).entryList({ "*.pdf" }, QDir::Files).isEmpty(),
        "CLI smoke should write a PDF into BRIEFUTIL_OUTPUT_DIR");

    const QString unicode_output = root.filePath(
        QString::fromUtf8("M\xc3\xa4rz Pr\xc3\xbc" "fung.pdf"));
    exit_code = run_cli(
        {
            "--to",
            "Ioannis Makris\\nAm Zirkus 3\\n10117 Berlin",
            "--subject",
            QString::fromUtf8("M\xc3\xa4rz Pr\xc3\xbc" "fung"),
            "--output",
            unicode_output,
        },
        template_dir,
        output_dir);
    require(exit_code == 0, "CLI should accept Unicode argument values");
    require(
        QFile::exists(unicode_output),
        "CLI should write to the exact Unicode --output path");
    const QString fixed_output = root.filePath("fixed-output.pdf");
    exit_code = run_cli(
        { "--to", "A", "--subject", "Fixed", "--output", fixed_output },
        template_dir,
        output_dir,
        nullptr,
        &standard_output);
    require(exit_code == 0 && QFile::exists(fixed_output),
            "CLI explicit --output should write the requested path");
    require(
        standard_output.trimmed() == fixed_output,
        "CLI should print the generated PDF path on stdout");
    require(
        QFile::exists(standard_output.trimmed()),
        "CLI stdout path should exist");
    exit_code = run_cli(
        { "--to", "A", "--subject", "Fixed", "--output", fixed_output },
        template_dir,
        output_dir);
    require(exit_code == 2, "CLI existing --output should fail without --force");
    exit_code = run_cli(
        { "--to", "A", "--subject", "Fixed", "--output", fixed_output, "--force" },
        template_dir,
        output_dir);
    require(exit_code == 0, "CLI --force should replace an existing --output path");

    QString error;
    exit_code = run_cli({ "--to", "A", "--header-scale", "abc" }, template_dir, output_dir, &error);
    require(exit_code == 2, "invalid numeric option should exit 2");
    require(error.contains("must be a number"), "invalid numeric option should explain the error");
    exit_code = run_cli({ "--to", "A", "--body-size", "0" }, template_dir, output_dir, &error);
    require(exit_code == 2, "out-of-range body size should exit 2");
    require(error.contains("between 6 and 24"), "out-of-range body size should explain the limit");
    exit_code = run_cli({ "--to", "A", "--body-leading", "100" }, template_dir, output_dir, &error);
    require(exit_code == 2, "out-of-range body leading should exit 2");
    require(error.contains("between 6 and 36"), "out-of-range body leading should explain the limit");
    exit_code = run_cli({ "--to", "A", "--body-size", "9,5" }, template_dir, output_dir, &error);
    require(exit_code == 2, "comma decimal body size should exit 2");
    require(error.contains("must be a number"), "comma decimal body size should explain the error");
    exit_code = run_cli({ "--to", "A", "--header-scale", "nan" }, template_dir, output_dir, &error);
    require(exit_code == 2, "non-finite scale should exit 2");
    require(error.contains("must be a number"), "non-finite scale should explain the error");
    exit_code = run_cli({ "--to", "A", "--body-scale", "20" }, template_dir, output_dir, &error);
    require(exit_code == 2, "out-of-range body scale should exit 2");
    require(error.contains("between 50 and 200"), "out-of-range body scale should explain the limit");

    exit_code = run_cli({ "--to", "A", "--layout", "DIN_5008_FORM_B" }, template_dir, output_dir);
    require(exit_code == 0, "CLI layout option should accept case-insensitive names");
    exit_code = run_cli({ "--to", "A", "--profile", "Max Mustermann" }, template_dir, output_dir);
    require(exit_code == 0, "CLI should select a known profile id");
    const QString profile_path = QDir(template_dir).filePath("Max Mustermann.json");
    const QString profile_font_output = root.filePath("profile-path-fonts.pdf");
    require(QFile::exists(profile_path), "seed profile should exist for --profile-path coverage");
    exit_code = run_cli(
        {
            "--to",
            "A",
            "--profile-path",
            profile_path,
            "--output",
            profile_font_output,
            "--font-sans",
            "Helvetica",
            "--font-sans-bold",
            "Helvetica-Bold",
            "--font-sans-italic",
            "Helvetica-Oblique",
            "--font-sans-bold-italic",
            "Helvetica-BoldOblique",
            "--font-mono",
            "Courier",
            "--body-size",
            "9.5",
            "--body-leading",
            "11.5",
        },
        template_dir,
        output_dir);
    require(exit_code == 0, "CLI should accept exact profile and GUI typography arguments");
    require(QFile::exists(profile_font_output), "CLI exact profile output should exist");
    const QString not_a_template_dir = root.filePath("not-a-template-dir");
    QFile not_a_template_dir_file(not_a_template_dir);
    require(
        not_a_template_dir_file.open(QIODevice::WriteOnly),
        "could not create template-dir blocker file");
    not_a_template_dir_file.close();
    const QString exact_profile_output = root.filePath("exact-profile-no-template-init.pdf");
    exit_code = run_cli(
        {
            "--to",
            "A",
            "--profile-path",
            profile_path,
            "--template-dir",
            not_a_template_dir,
            "--output",
            exact_profile_output,
        },
        template_dir,
        output_dir);
    require(
        exit_code == 0 && QFile::exists(exact_profile_output),
        "CLI --profile-path should not require template directory initialization");
    exit_code = run_cli(
        { "--to", "A", "--profile", "Max Mustermann", "--profile-path", profile_path },
        template_dir,
        output_dir,
        &error);
    require(exit_code == 2, "CLI should reject combined --profile and --profile-path");
    require(error.contains("either --profile or --profile-path"),
            "combined profile options should explain the error");
    exit_code = run_cli({ "--to", "A", "--profile", "Does Not Exist" }, template_dir, output_dir);
    require(exit_code == 2, "CLI unknown profile should exit 2");
    exit_code = run_cli({ "--help" }, template_dir, output_dir);
    require(exit_code == 0, "CLI --help should exit 0");

    const QString empty_recipient_file = root.filePath("empty-recipient.txt");
    QFile file(empty_recipient_file);
    require(file.open(QIODevice::WriteOnly), "could not create empty recipient file");
    file.close();
    const QString empty_recipient_output = root.filePath("empty-recipient.pdf");
    exit_code = run_cli(
        { "--to-file", empty_recipient_file, "--output", empty_recipient_output },
        template_dir,
        output_dir,
        &error);
    require(exit_code == 0, "empty explicit recipient file should be accepted");
    require(QFile::exists(empty_recipient_output), "empty recipient output should exist");

    return 0;
}
