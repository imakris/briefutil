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
    QString* standard_error = nullptr)
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
        output_dir);
    require(exit_code == 0, "CLI smoke should exit successfully");
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
        output_dir);
    require(exit_code == 0 && QFile::exists(fixed_output),
            "CLI explicit --output should write the requested path");
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

    exit_code = run_cli({ "--to", "A", "--layout", "DIN_5008_FORM_B" }, template_dir, output_dir);
    require(exit_code == 0, "CLI layout option should accept case-insensitive names");
    exit_code = run_cli({ "--to", "A", "--profile", "Max Mustermann" }, template_dir, output_dir);
    require(exit_code == 0, "CLI should select a known profile id");
    exit_code = run_cli({ "--to", "A", "--profile", "Does Not Exist" }, template_dir, output_dir);
    require(exit_code == 2, "CLI unknown profile should exit 2");
    exit_code = run_cli({ "--help" }, template_dir, output_dir);
    require(exit_code == 0, "CLI --help should exit 0");

    const QString empty_recipient_file = root.filePath("empty-recipient.txt");
    QFile file(empty_recipient_file);
    require(file.open(QIODevice::WriteOnly), "could not create empty recipient file");
    file.close();
    exit_code = run_cli({ "--to-file", empty_recipient_file }, template_dir, output_dir, &error);
    require(exit_code == 2, "empty explicit recipient file should exit 2");
    require(error.contains("Recipient is required"), "empty recipient file should explain the error");

    return 0;
}
