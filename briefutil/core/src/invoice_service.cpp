#include "briefutil/invoice_service.h"

#include "briefutil/document_model.h"
#include "briefutil/owned_staging.h"
#include "briefutil/path_utils.h"
#include "briefutil/pdf_measurement.h"
#include "briefutil/pdf_renderer.h"
#include "rich_text_layout.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLockFile>
#include <QRegularExpression>
#include <QString>

#include <mark2haru/table_layout.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace briefutil {
namespace {

constexpr qint64 k_max_json_integer = 9007199254740991LL;
constexpr qint64 k_max_json_bytes   = 1024 * 1024;

constexpr float  k_left_mm          = 25.0f;
constexpr float  k_width_mm         = 160.0f;
constexpr float  k_page_bottom_mm   = 270.0f;
constexpr float  k_cell_padding_pt  = 4.0f;

constexpr float  k_content_left_mm  = k_left_mm + k_cell_padding_pt / k_pts_per_mm;
constexpr float  k_content_width_mm = k_width_mm - 2.0f * k_cell_padding_pt / k_pts_per_mm;

Generation_result failure(const std::string& message)
{
    Generation_result result;
    result.message = message;
    return result;
}

bool read_json(
    const std::string& path, QByteArray& bytes, QJsonObject& object, std::string& error)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly) || file.size() > k_max_json_bytes) {
        error = "Cannot read JSON input, or it exceeds 1 MiB.";
        return false;
    }
    bytes = file.readAll();
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        error = "Expected a JSON object.";
        return false;
    }
    object = document.object();
    if (object.value("version") != QJsonValue(1)) {
        error = "Unsupported invoice JSON version.";
        return false;
    }
    return true;
}

bool required_text(const QJsonObject& object, const char* name, std::string& error)
{
    const auto value = object.value(name);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        error = std::string("Missing or empty invoice field: ") + name;
        return false;
    }
    for (const QChar character : value.toString()) {
        if (character.isNull() || (character.category() == QChar::Other_Control &&
            character != '\n' && character != '\r' && character != '\t'))
        {
            error = std::string("Invalid control character in invoice field: ") + name;
            return false;
        }
    }
    return true;
}

bool integer_value(const QJsonObject& object, const char* name, qint64& out)
{
    const auto value = object.value(name);
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || number > k_max_json_integer ||
        number != std::floor(number))
    {
        return false;
    }
    out = static_cast<qint64>(number);
    return true;
}

QString text(const QJsonObject& object, const char* name)
{
    return object.value(name).toString();
}

QString digest(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString money(qint64 minor)
{
    return QString::number(minor / 100) + "." +
        QString::number(minor % 100).rightJustified(2, '0') + " EUR";
}

bool color(const QJsonObject& object, const char* key, color_t& result)
{
    if (!object.contains(key)) {
        return true;
    }
    const QString value = text(object, key);
    static const QRegularExpression pattern("^#[0-9A-Fa-f]{6}$");
    if (!pattern.match(value).hasMatch()) {
        return false;
    }
    result = {
        value.mid(1, 2).toUInt(nullptr, 16) / 255.0f,
        value.mid(3, 2).toUInt(nullptr, 16) / 255.0f,
        value.mid(5, 2).toUInt(nullptr, 16) / 255.0f,
    };
    return true;
}

class Invoice_layout
{
public:
    Invoice_layout(const Pdf_measurement& measurement, color_t banner, color_t rule)
    :
        m_measurement(measurement),
        m_banner(banner),
        m_rule(rule)
    {
        new_page();
    }

    void new_page()
    {
        m_document.pages.emplace_back();
        m_y = 25.0f;
    }

    void reserve(float height)
    {
        if (m_y + height > k_page_bottom_mm) {
            new_page();
        }
    }

    void line(const std::string& value, Font_id font = Font_id::SANS, float size = 10.0f)
    {
        const float height = pt_to_mm(size + 4.0f);
        reserve(height);
        elements().push_back(Text_block{ k_content_left_mm, m_y, k_content_width_mm, value, font, size });
        m_y += height;
    }

    void paragraph(const QString& value, float size = 10.0f)
    {
        for (const auto& line_text : m_measurement.wrap_text(
            value.toStdString(), Font_id::SANS, size, k_content_width_mm))
        {
            line(line_text, Font_id::SANS, size);
        }
        m_y += 3.0f;
    }

    void block(float x, float y, float width, const QString& value, float size = 10.0f)
    {
        elements().push_back(Text_block{
            x, y, width, value.toStdString(), Font_id::SANS, size, size + 3.0f, {}, true });
    }

    bool table(
        const mark2haru::Table_block& table,
        const std::vector<float>&    widths_mm,
        float                        size = 10.0f,
        bool                         right_align_last = false)
    {
        mark2haru::Table_columns columns;
        columns.column_count = static_cast<int>(widths_mm.size());
        columns.valid        = true;
        for (float width : widths_mm) {
            columns.widths_pt.push_back(mm_to_pt(width));
        }
        mark2haru::table_style_t style;
        style.text_size_pt    = size;
        style.text_leading_pt = size + 4.0;
        style.cell_padding_pt = k_cell_padding_pt;
        style.header_fill     = { m_banner.r, m_banner.g, m_banner.b };

        for (int index = 0; index < static_cast<int>(table.rows.size()); ++index) {
            const float height = pt_to_mm(mark2haru::measure_table_row_height(
                table, index, columns, style, *m_measurement.context()));
            if (height > k_page_bottom_mm - 25.0f) {
                return false;
            }
            if (m_y + height > k_page_bottom_mm) {
                new_page();
                if (table.has_header && index > 0) {
                    auto header = mark2haru::layout_table_row(
                        table, 0, columns, mm_to_pt(k_left_mm), mm_to_pt(m_y),
                        style, *m_measurement.context());
                    append_mark2haru_table_elements(header, elements(), false);
                    m_y += pt_to_mm(header.height_pt);
                    if (m_y + height > k_page_bottom_mm) {
                        return false;
                    }
                }
            }
            auto row = mark2haru::layout_table_row(
                table, index, columns, mm_to_pt(k_left_mm), mm_to_pt(m_y),
                style, *m_measurement.context());
            if (right_align_last) {
                const double right = mm_to_pt(k_left_mm + k_width_mm) - style.cell_padding_pt;
                const double left  = right - columns.widths_pt.back() + style.cell_padding_pt;
                // The amount cells contain plain numeric lines. Align the actual
                // table spans at the amount column's right edge using the same
                // font metrics that laid them out, including wrapped lines.
                std::map<double, double> line_ends;
                for (const auto& element : row.elements) {
                    const auto* span = std::get_if<mark2haru::Table_text_span>(&element);
                    if (span && span->x_pt >= left) {
                        const double end = span->x_pt + m_measurement.context()->measure_text_width(
                            span->font, span->text, span->size_pt);
                        line_ends[span->y_pt] = std::max(line_ends[span->y_pt], end);
                    }
                }
                for (auto& element : row.elements) {
                    auto* span = std::get_if<mark2haru::Table_text_span>(&element);
                    if (span && span->x_pt >= left) {
                        span->x_pt += right - line_ends.at(span->y_pt);
                    }
                }
            }
            append_mark2haru_table_elements(row, elements(), false);
            m_y += height;
        }
        return true;
    }

    void rule()
    {
        reserve(3.0f);
        elements().push_back(line_segment_t{ k_left_mm, m_y, k_left_mm + k_width_mm, m_y, 0.5f, m_rule });
        m_y += 3.0f;
    }

    Document finish()
    {
        for (size_t index = 0; index < m_document.pages.size(); ++index) {
            m_document.pages[index].elements.push_back(Text_block{
                k_left_mm, 285.0f, k_width_mm,
                std::to_string(index + 1) + " / " + std::to_string(m_document.pages.size()),
                Font_id::SANS, 8.0f });
        }
        return std::move(m_document);
    }

    std::vector<Page_element>& elements() { return m_document.pages.back().elements; }
    float& y() { return m_y; }

private:
    const Pdf_measurement& m_measurement;
    color_t                m_banner;
    color_t                m_rule;
    Document               m_document;
    float                  m_y = 25.0f;
};

} // namespace

Generation_result generate_invoice_pdf(const Invoice_request& request)
{
    if (request.input_path.empty() || request.template_path.empty() ||
        request.output_path.empty() || request.receipt_path.empty())
    {
        return failure("Invoice generation requires input, template, PDF and receipt paths.");
    }
    const QString number = QString::fromStdString(request.invoice_number);
    const QString date   = QString::fromStdString(request.invoice_date);
    const QDate invoice_date = QDate::fromString(date, Qt::ISODate);
    if (number.trimmed().isEmpty() || number.size() > 120 ||
        number.contains('\n') || number.contains('\r') ||
        !invoice_date.isValid() || invoice_date.toString(Qt::ISODate) != date)
    {
        return failure("Provide an invoice number and an ISO invoice date (YYYY-MM-DD).");
    }

    QByteArray input_bytes;
    QByteArray template_bytes;
    QJsonObject input;
    QJsonObject invoice_template;
    std::string error;
    if (!read_json(request.input_path, input_bytes, input, error) ||
        !read_json(request.template_path, template_bytes, invoice_template, error))
    {
        return failure(error);
    }
    for (const char* field : { "order_id", "product_name", "recipient_email", "company_name",
        "billing_address", "country_code", "vat_number", "tax_note", "payment_due_date" })
    {
        if (!required_text(input, field, error)) {
            return failure(error);
        }
    }
    const QJsonObject seller = invoice_template.value("seller").toObject();
    for (const char* field : { "company_name", "registered_address", "vat_number",
        "company_number", "payment_instructions" })
    {
        if (!required_text(seller, field, error)) {
            return failure(error);
        }
    }
    qint64 net = 0, tax = 0, total = 0, updates = 0, installations = 0;
    if (!integer_value(input, "unit_amount_minor", net) ||
        !integer_value(input, "tax_amount_minor", tax) ||
        !integer_value(input, "total_amount_minor", total) ||
        !integer_value(input, "update_term_months", updates) ||
        !integer_value(input, "desktop_slot_grant", installations) ||
        net + tax != total || net == 0 || updates == 0 || installations == 0 ||
        text(input, "currency") != "EUR")
    {
        return failure("Invoice amounts must be exact EUR minor units with net + tax = total; terms must be positive integers.");
    }
    const QString due = text(input, "payment_due_date");
    const QDate due_date = QDate::fromString(due, Qt::ISODate);
    if (!due_date.isValid() || due_date.toString(Qt::ISODate) != due || due_date < invoice_date) {
        return failure("Payment due date must be an ISO date on or after the invoice date.");
    }
    color_t banner = { 0.92f, 0.92f, 0.92f };
    color_t rule   = { 0.35f, 0.35f, 0.35f };
    if (!color(invoice_template, "banner_color", banner) || !color(invoice_template, "rule_color", rule)) {
        return failure("Template colors must use #RRGGBB notation.");
    }
    const QString logo = text(invoice_template, "logo_image");
    std::string logo_path;
    float logo_height = 0.0f;
    if (!logo.isEmpty()) {
        if (!is_valid_profile_image_name(logo.toStdString())) {
            return failure("Template logo must be a relative PNG path within its directory.");
        }
        logo_path = QFileInfo(QString::fromStdString(request.template_path)).absoluteDir().filePath(logo).toStdString();
        const auto dimensions = measure_png(logo_path);
        if (!dimensions.valid) {
            return failure("Cannot read the template PNG logo.");
        }
        logo_height = std::min(18.0f, 65.0f * dimensions.height_px / dimensions.width_px);
    }

    const Pdf_measurement measurement(default_font_family());
    if (!measurement.ready()) {
        return failure(measurement.error());
    }
    Invoice_layout layout(measurement, banner, rule);
    layout.elements().push_back(filled_rect_t{ 0, 0, 210.0f, 54.0f, banner });
    if (!logo_path.empty()) {
        const auto dimensions = measure_png(logo_path);
        const float width = logo_height * dimensions.width_px / dimensions.height_px;
        layout.elements().push_back(Image_block{
            k_content_left_mm + k_content_width_mm - width, 51.0f - logo_height, width, logo_path });
    }
    else {
        layout.block(k_left_mm, 35.0f, k_width_mm, text(seller, "company_name"), 17.0f);
    }
    const QString seller_block = text(seller, "company_name") + "\n" +
        text(seller, "registered_address") + "\nVAT: " + text(seller, "vat_number") +
        "\nCompany number: " + text(seller, "company_number");
    const QString buyer_block = text(input, "company_name") + "\n" + text(input, "billing_address") +
        "\n" + text(input, "country_code") + "\nVAT: " + text(input, "vat_number");
    using mark2haru::Inline_style;
    using mark2haru::Table_block;
    using mark2haru::Table_cell;
    using mark2haru::Table_row;
    auto cell = [](const QString& value, Inline_style style = Inline_style::NORMAL) {
        return Table_cell{ { { value.toStdString(), style } } };
    };
    const std::string table_error = "Invoice table row is too tall for an A4 page; shorten its text.";

    layout.y() = 59.0f;
    const Table_block seller_table{ { Table_row{ { cell(""), cell(seller_block) } } }, false };
    if (!layout.table(seller_table, { 95.0f, 65.0f }, 8.5f, true)) {
        return failure(table_error);
    }
    layout.elements().push_back(Text_block{
        k_left_mm, 82.0f, 80.0f, "INVOICE", Font_id::SANS, 20.0f });

    layout.y() = std::max(104.0f, layout.y() + 12.0f);
    const Table_block metadata{
        {
            Table_row{ { cell("BILL TO", Inline_style::BOLD), cell(""), cell("INVOICE NUMBER", Inline_style::BOLD) } },
            Table_row{ {
                cell(buyer_block), cell(""),
                cell(number + "\n\nDATE\n" + date + "\n\nPAYMENT DUE\n" + due),
            } },
        },
        false,
    };
    if (!layout.table(metadata, { 85.0f, 20.0f, 55.0f }, 9.0f)) {
        return failure(table_error);
    }
    layout.y() += 12.0f;
    const Table_cell description{ {
        { text(input, "product_name").toStdString(), Inline_style::BOLD },
        { ("\n1 licence\nNamed user: " + text(input, "recipient_email") + "\n" +
            QString::number(installations) + " installations; permanent use; " +
            QString::number(updates) + " months of updates.").toStdString(), Inline_style::NORMAL },
    } };
    const Table_block items{
        {
            Table_row{ { cell("DESCRIPTION"), cell("AMOUNT") } },
            Table_row{ { description, cell(money(net)) } },
        },
        true,
    };
    if (!layout.table(items, { 125.0f, 35.0f }, 9.0f, true)) {
        return failure(table_error);
    }
    layout.y() += 15.0f;
    layout.rule();
    layout.y() += 3.0f;
    const Table_cell payment{ {
        { "PAYMENT DETAILS\n\n", Inline_style::BOLD },
        { (text(seller, "payment_instructions") + "\n\nPayment reference: " + number).toStdString(), Inline_style::NORMAL },
    } };
    const Table_cell total_labels{ {
        { "Subtotal\n\nVAT\n\n", Inline_style::NORMAL },
        { "TOTAL", Inline_style::BOLD },
    } };
    const Table_cell total_amounts{ {
        { (money(net) + "\n\n" + money(tax) + "\n\n").toStdString(), Inline_style::NORMAL },
        { money(total).toStdString(), Inline_style::BOLD },
    } };
    const Table_block payment_and_totals{ { Table_row{ { payment, total_labels, total_amounts } } }, false };
    if (!layout.table(payment_and_totals, { 90.0f, 35.0f, 35.0f }, 9.0f, true)) {
        return failure(table_error);
    }
    layout.y() += 5.0f;
    layout.paragraph(text(input, "tax_note"), 8.0f);
    layout.paragraph("Order reference: " + text(input, "order_id"), 8.0f);
    const Document document = layout.finish();

    const QFileInfo pdf_info(QString::fromStdString(request.output_path));
    const QFileInfo receipt_info(QString::fromStdString(request.receipt_path));
    const QString pdf_path     = pdf_info.absoluteFilePath();
    const QString receipt_path = receipt_info.absoluteFilePath();
    if (pdf_path.compare(receipt_path, Qt::CaseInsensitive) == 0) {
        return failure("PDF and receipt paths must be different.");
    }
    // Both targets use the same exclusion/publication primitives as letters.
    // Lock in path order so concurrent requests cannot invert the two locks.
    std::vector<QString> paths{ pdf_path, receipt_path };
    std::sort(paths.begin(), paths.end());
    std::vector<std::unique_ptr<QLockFile>> locks;
    for (const auto& path : paths) {
        const QFileInfo info(path);
        QDir directory = info.absoluteDir();
        if (!directory.exists() && !directory.mkpath(".")) {
            return failure("Cannot create invoice output directory.");
        }
        auto lock = std::make_unique<QLockFile>(directory.filePath("." + info.fileName() + ".lock"));
        lock->setStaleLockTime(0);
        if (!lock->tryLock(30000)) {
            return failure("Another briefutil run is writing an invoice output.");
        }
        locks.push_back(std::move(lock));
        if (QFileInfo::exists(path)) {
            return failure("Invoice PDF or receipt already exists; choose new output paths.");
        }
    }
    Owned_staging_slot pdf_staging;
    Owned_staging_slot receipt_staging;
    if (!pdf_staging.open(pdf_info.absoluteDir(), pdf_info.fileName(), &error) ||
        !receipt_staging.open(receipt_info.absoluteDir(), receipt_info.fileName(), &error))
    {
        return failure(error);
    }
    const auto rendered = render_pdf(document, pdf_staging.staged_path().toStdString(), measurement);
    if (!rendered.ok) {
        return failure(rendered.message.empty() ? rendered.detail : rendered.message);
    }
    QFile pdf(pdf_staging.staged_path());
    if (!pdf.open(QIODevice::ReadOnly)) {
        return failure("Cannot read rendered invoice PDF.");
    }
    QCryptographicHash pdf_hash(QCryptographicHash::Sha256);
    if (!pdf_hash.addData(&pdf)) {
        return failure("Cannot hash rendered invoice PDF.");
    }
    pdf.close();
    const QJsonObject receipt{
        { "version",         1 },
        { "order_id",        input.value("order_id") },
        { "input_sha256",    digest(input_bytes) },
        { "invoice_number",  number },
        { "invoice_date",    date },
        { "pdf_sha256",      QString::fromLatin1(pdf_hash.result().toHex()) },
        { "template_sha256", digest(template_bytes) },
        { "seller",          seller },
    };
    QFile receipt_file(receipt_staging.staged_path());
    const QByteArray receipt_bytes = QJsonDocument(receipt).toJson(QJsonDocument::Indented);
    if (!receipt_file.open(QIODevice::WriteOnly) || receipt_file.write(receipt_bytes) != receipt_bytes.size()) {
        return failure("Cannot stage invoice receipt.");
    }
    if (!receipt_file.flush()) {
        return failure("Cannot flush invoice receipt.");
    }
    receipt_file.close();
    if (publish_staged_file(pdf_staging.staged_path(), pdf_path, false, &error) != Publish_outcome::PUBLISHED) {
        return failure("Cannot publish invoice PDF: " + error);
    }
    if (publish_staged_file(receipt_staging.staged_path(), receipt_path, false, &error) != Publish_outcome::PUBLISHED) {
        return failure("PDF was saved, but its receipt could not be published. Keep the PDF and regenerate to new paths: " + error);
    }
    Generation_result result;
    result.ok          = true;
    result.code        = Generation_result_code::OK;
    result.output_path = pdf_path.toStdString();
    return result;
}

} // namespace briefutil
