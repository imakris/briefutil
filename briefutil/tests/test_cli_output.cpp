#include "cli_output.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <cstdio>

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void write_empty_file(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "could not create test PDF path");
    file.close();
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir root;
    require(root.isValid(), "could not create temporary directory");

    const QString first_pdf  = root.filePath("first.pdf");
    const QString second_pdf = root.filePath("second.pdf");
    write_empty_file(first_pdf);
    write_empty_file(second_pdf);

    require(
        briefutil_pdf_path_from_cli_stdout(first_pdf + "\n") == first_pdf,
        "single-line stdout should return the generated PDF path");
    require(
        briefutil_pdf_path_from_cli_stdout("notice\r\n" + first_pdf + "\r\n") == first_pdf,
        "CRLF stdout should return the generated PDF path");
    require(
        briefutil_pdf_path_from_cli_stdout(first_pdf + "\n" + second_pdf + "\n") == second_pdf,
        "multi-line stdout should return the last existing PDF path");
    require(
        briefutil_pdf_path_from_cli_stdout(
            first_pdf + "\nProfile load warning: missing.pdf\n") == first_pdf,
        "stdout diagnostics ending in PDF should not displace an existing path");
    require(
        briefutil_pdf_path_from_cli_stdout(root.filePath("missing.pdf") + "\n").isEmpty(),
        "missing PDF paths should not be accepted");
    require(
        briefutil_pdf_path_from_cli_stdout("PDF generation completed\n").isEmpty(),
        "stdout without a PDF path should return empty");

    return 0;
}
