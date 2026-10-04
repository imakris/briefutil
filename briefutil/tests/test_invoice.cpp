#include "briefutil/invoice_service.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static QByteArray read_file(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "read fixture or output");
    return file.readAll();
}

static void write_file(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "write fixture");
    require(file.write(bytes) == bytes.size(), "write complete fixture");
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory");
    const QString example_dir = QString::fromUtf8(BRIEFUTIL_INVOICE_EXAMPLES);
    const QByteArray input_bytes = read_file(example_dir + "/order.json");
    const QByteArray template_bytes = read_file(example_dir + "/template.json");
    const QString input_path = directory.filePath("order.json");
    const QString template_path = directory.filePath("template.json");
    write_file(input_path, input_bytes);
    write_file(template_path, template_bytes);

    briefutil::Invoice_request request;
    request.input_path     = input_path.toStdString();
    request.template_path  = template_path.toStdString();
    request.invoice_number = "EXAMPLE-2026-001";
    request.invoice_date   = "2026-10-04";
    request.output_path    = directory.filePath("invoice.pdf").toStdString();
    request.receipt_path   = directory.filePath("receipt.json").toStdString();
    const auto generated = briefutil::generate_invoice_pdf(request);
    require(generated.ok, generated.message.c_str());
    const QByteArray pdf = read_file(QString::fromStdString(request.output_path));
    require(pdf.startsWith("%PDF") && pdf.trimmed().endsWith("%%EOF"), "complete PDF");
    const QByteArray receipt_bytes = read_file(QString::fromStdString(request.receipt_path));
    const auto receipt = QJsonDocument::fromJson(receipt_bytes).object();
    auto hash = [](const QByteArray& bytes) {
        return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    };
    require(receipt.value("input_sha256") == hash(input_bytes), "receipt binds exact exported input bytes");
    require(receipt.value("pdf_sha256") == hash(pdf), "receipt binds PDF bytes");
    require(receipt.value("template_sha256") == hash(template_bytes), "receipt binds template bytes");
    require(receipt.value("invoice_number") == "EXAMPLE-2026-001", "explicit invoice number");
    require(receipt.value("invoice_date") == "2026-10-04", "explicit invoice date");
    require(receipt.value("seller") == QJsonDocument::fromJson(template_bytes).object().value("seller"),
        "receipt retains complete seller snapshot");
    require(!briefutil::generate_invoice_pdf(request).ok, "existing invoice outputs are refused");
    require(read_file(QString::fromStdString(request.output_path)) == pdf, "existing PDF is preserved");
    require(read_file(QString::fromStdString(request.receipt_path)) == receipt_bytes, "existing receipt is preserved");

    request.output_path  = directory.filePath("rejected.pdf").toStdString();
    request.receipt_path = directory.filePath("rejected.json").toStdString();
    auto input = QJsonDocument::fromJson(input_bytes).object();
    input.insert("total_amount_minor", 1999);
    write_file(input_path, QJsonDocument(input).toJson());
    require(!briefutil::generate_invoice_pdf(request).ok, "mismatched sum is rejected");
    input.insert("total_amount_minor", 2000);
    input.insert("tax_amount_minor", 0.1);
    write_file(input_path, QJsonDocument(input).toJson());
    require(!briefutil::generate_invoice_pdf(request).ok, "fractional minor units are rejected");
    input.insert("tax_amount_minor", -1);
    write_file(input_path, QJsonDocument(input).toJson());
    require(!briefutil::generate_invoice_pdf(request).ok, "negative minor units are rejected");
    input.insert("tax_amount_minor", 0);
    input.insert("payment_due_date", "2026-10-03");
    write_file(input_path, QJsonDocument(input).toJson());
    require(!briefutil::generate_invoice_pdf(request).ok, "due date before invoice is rejected");
    require(!QFile::exists(QString::fromStdString(request.output_path)), "invalid data publishes no PDF");
    write_file(input_path, input_bytes);
    request.invoice_date = "2026-02-30";
    require(!briefutil::generate_invoice_pdf(request).ok, "invalid calendar date is rejected");
    request.invoice_date = "2026-10-04";
    auto invoice_template = QJsonDocument::fromJson(template_bytes).object();
    auto seller = invoice_template.value("seller").toObject();
    seller.remove("payment_instructions");
    invoice_template.insert("seller", seller);
    write_file(template_path, QJsonDocument(invoice_template).toJson());
    require(!briefutil::generate_invoice_pdf(request).ok, "missing payment instructions are rejected");
    write_file(template_path, template_bytes);

    QProcess cli;
    const QStringList arguments{
        "--invoice-json", input_path,
        "--invoice-template", template_path,
        "--invoice-number", "EXAMPLE-2026-002",
        "--invoice-date", "2026-10-04",
        "--output", QString::fromStdString(request.output_path),
        "--receipt", QString::fromStdString(request.receipt_path),
    };
    cli.start(QString::fromUtf8(BRIEFUTIL_CLI_PATH), arguments);
    require(cli.waitForFinished(30000) && cli.exitCode() == 0, "headless invoice CLI succeeds");
    require(read_file(QString::fromStdString(request.receipt_path)).contains("EXAMPLE-2026-002"),
        "CLI emits receipt");
    cli.start(QString::fromUtf8(BRIEFUTIL_CLI_PATH), arguments + QStringList{ "--force" });
    require(cli.waitForFinished(30000) && cli.exitCode() == 2, "invoice CLI refuses letter-only overwrite flag");

    request.invoice_number.clear();
    request.invoice_date.clear();
    int document_index = 0;
    auto render_document = [&](const QJsonObject& value, bool expected) {
        const QString stem = "document-" + QString::number(++document_index);
        request.output_path = directory.filePath(stem + ".pdf").toStdString();
        request.receipt_path = directory.filePath(stem + ".json").toStdString();
        const QByteArray bytes = QJsonDocument(value).toJson();
        write_file(input_path, bytes);
        const auto result = briefutil::generate_invoice_pdf(request);
        if (expected) {
            require(result.ok, result.message.c_str());
            const auto returned = QJsonDocument::fromJson(
                read_file(QString::fromStdString(request.receipt_path))).object();
            require(returned.size() == 10 && returned.value("version") == 2, "version 2 receipt contract");
            for (const char* field : { "document_id", "order_id", "kind", "number", "date", "seller" }) {
                require(returned.value(field) == value.value(field), "receipt retains reserved document identity");
            }
            require(returned.value("input_sha256") == hash(bytes), "document receipt binds exact input");
            require(returned.value("pdf_sha256") == hash(read_file(QString::fromStdString(request.output_path))),
                "document receipt binds rendered PDF");
        }
        else {
            require(!result.ok, "invalid reserved document is rejected");
            require(!QFile::exists(QString::fromStdString(request.output_path)), "rejected document creates no PDF");
            require(!QFile::exists(QString::fromStdString(request.receipt_path)), "rejected document creates no receipt");
        }
    };
    const auto document = QJsonDocument::fromJson(read_file(example_dir + "/document.json")).object();
    const auto credit = QJsonDocument::fromJson(read_file(example_dir + "/credit-note.json")).object();
    const auto mixed = QJsonDocument::fromJson(read_file(example_dir + "/mixed-tax-document.json")).object();
    render_document(document, true);
    render_document(credit, true);
    render_document(mixed, true);

    request.invoice_number = "OVERRIDE";
    render_document(document, false);
    request.invoice_number.clear();
    request.invoice_date = "2026-10-06";
    render_document(document, false);
    request.invoice_date.clear();

    auto changed = document;
    auto changed_totals = changed.value("totals").toObject();
    changed_totals.insert("gross_minor", 2381);
    changed.insert("totals", changed_totals);
    render_document(changed, false);
    changed = document;
    auto changed_lines = changed.value("lines").toArray();
    changed_lines.append(changed_lines.first());
    changed.insert("lines", changed_lines);
    render_document(changed, false);
    changed = credit;
    changed.insert("original_document", QJsonValue::Null);
    render_document(changed, false);
    changed = document;
    auto changed_seller = changed.value("seller").toObject();
    changed_seller.insert("vat_number", "DIFFERENT");
    changed.insert("seller", changed_seller);
    render_document(changed, false);

    auto change_tax = [&](const char* field, const QJsonValue& value) {
        auto result = document;
        auto lines = result.value("lines").toArray();
        auto line = lines.first().toObject();
        auto taxes = line.value("taxes").toArray();
        auto component = taxes.first().toObject();
        component.insert(field, value);
        taxes[0] = component;
        line.insert("taxes", taxes);
        lines[0] = line;
        result.insert("lines", lines);
        return result;
    };
    render_document(change_tax("amount_minor", 379), false);
    render_document(change_tax("taxable_base_minor", 0.5), false);
    render_document(change_tax("rate_ppm", 190000.5), false);
    changed = document;
    changed_lines = changed.value("lines").toArray();
    auto overflowing = changed_lines.first().toObject();
    overflowing.insert("quantity", 2);
    overflowing.insert("unit_net_minor", 9007199254740991.0);
    changed_lines[0] = overflowing;
    changed.insert("lines", changed_lines);
    render_document(changed, false);

    changed = document;
    changed.insert("currency", "USD");
    changed.insert("tax_reporting", QJsonObject{
        { "currency", "EUR" }, { "currency_minor_digits", 2 },
        { "tax_minor", 350 }, { "fx_reference", "SAMPLE-CONVERSION-001" },
    });
    render_document(changed, true);
    write_file(input_path, QJsonDocument(document).toJson());
    cli.start(QString::fromUtf8(BRIEFUTIL_CLI_PATH), QStringList{
        "--invoice-json", input_path, "--invoice-template", template_path,
        "--output", directory.filePath("reserved-cli.pdf"),
        "--receipt", directory.filePath("reserved-cli.receipt.json"),
    });
    require(cli.waitForFinished(30000) && cli.exitCode() == 0,
        "version 2 CLI uses server-reserved number and date without overrides");
    std::puts("Invoice input validation, publication, exact digests and CLI checks passed.");
    return 0;
}
