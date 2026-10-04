#include "invoice_document.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace briefutil {
namespace {

constexpr qint64 k_max_json_integer = 9007199254740991LL;

QString text(const QJsonObject& object, const char* name)
{
    return object.value(name).toString();
}

bool valid_text(const QJsonObject& object, const char* name, bool empty = false)
{
    const auto value = object.value(name);
    if (!value.isString() || (!empty && value.toString().trimmed().isEmpty())) {
        return false;
    }
    for (const QChar character : value.toString()) {
        if (character.isNull() || (character.category() == QChar::Other_Control &&
            character != '\n' && character != '\r' && character != '\t'))
        {
            return false;
        }
    }
    return true;
}

bool integer(const QJsonObject& object, const char* name, qint64& out)
{
    const auto value = object.value(name);
    const double number = value.toDouble(-1);
    if (!value.isDouble() || !std::isfinite(number) || number < 0 ||
        number > k_max_json_integer || number != std::floor(number))
    {
        return false;
    }
    out = static_cast<qint64>(number);
    return true;
}

bool add(qint64& total, qint64 amount)
{
    if (amount > k_max_json_integer - total) {
        return false;
    }
    total += amount;
    return true;
}

bool iso_date(const QString& value)
{
    const auto parsed = QDate::fromString(value, Qt::ISODate);
    return parsed.isValid() && parsed.toString(Qt::ISODate) == value;
}

bool identifier(const QJsonObject& object, const char* name)
{
    const auto value = text(object, name);
    return valid_text(object, name) && value.size() <= 120 &&
        !value.contains('\n') && !value.contains('\r') && !value.contains('\t');
}

bool totals(const QJsonObject& object, qint64& net, qint64& tax, qint64& gross)
{
    if (!integer(object, "net_minor", net) || !integer(object, "tax_minor", tax) ||
        !integer(object, "gross_minor", gross))
    {
        return false;
    }
    qint64 sum = net;
    return add(sum, tax) && sum == gross;
}

bool currency(const QJsonObject& object, QString& code, int& digits)
{
    qint64 value = 0;
    static const QRegularExpression pattern("^[A-Z]{3}$");
    if (!pattern.match(text(object, "currency")).hasMatch() ||
        !integer(object, "currency_minor_digits", value) || value > 3)
    {
        return false;
    }
    code = text(object, "currency");
    digits = static_cast<int>(value);
    return true;
}

QString rate_percent(qint64 ppm)
{
    QString value = QString::number(ppm / 10000);
    if (ppm % 10000) {
        QString fraction = QString::number(ppm % 10000).rightJustified(4, '0');
        while (fraction.endsWith('0')) {
            fraction.chop(1);
        }
        value += "." + fraction;
    }
    return value + "%";
}

bool read_v1(
    const QJsonObject& input, const QJsonObject& seller,
    const Invoice_request& request, Invoice_document& doc)
{
    for (const char* key : { "order_id", "product_name", "recipient_email", "company_name",
        "billing_address", "country_code", "vat_number", "tax_note", "payment_due_date" })
    {
        if (!valid_text(input, key)) {
            return false;
        }
    }
    doc.number = QString::fromStdString(request.invoice_number);
    doc.date = QString::fromStdString(request.invoice_date);
    qint64 updates = 0, installations = 0;
    if (!integer(input, "unit_amount_minor", doc.net_minor) ||
        !integer(input, "tax_amount_minor", doc.tax_minor) ||
        !integer(input, "total_amount_minor", doc.gross_minor) ||
        !integer(input, "update_term_months", updates) ||
        !integer(input, "desktop_slot_grant", installations) ||
        doc.net_minor == 0 || updates == 0 || installations == 0 ||
        text(input, "currency") != "EUR")
    {
        return false;
    }
    qint64 sum = doc.net_minor;
    const QString due = text(input, "payment_due_date");
    if (!add(sum, doc.tax_minor) || sum != doc.gross_minor ||
        !iso_date(due) || due < doc.date)
    {
        return false;
    }
    doc.buyer = text(input, "company_name") + "\n" + text(input, "billing_address") +
        "\n" + text(input, "country_code") + "\nVAT: " + text(input, "vat_number");
    doc.metadata = doc.number + "\n\nDATE\n" + doc.date + "\n\nPAYMENT DUE\n" + due;
    doc.items.push_back({ text(input, "product_name") + "\n1 licence\nNamed user: " +
        text(input, "recipient_email") + "\n" + QString::number(installations) +
        " installations; permanent use; " + QString::number(updates) + " months of updates.",
        doc.net_minor });
    doc.taxes.push_back({ "VAT", doc.tax_minor });
    doc.payment_heading = "PAYMENT DETAILS";
    doc.payment_details = text(seller, "payment_instructions") +
        "\n\nPayment reference: " + doc.number;
    doc.notes.push_back(text(input, "tax_note"));
    return true;
}

bool read_v2(const QJsonObject& input, const QJsonObject& seller, Invoice_document& doc)
{
    if (!identifier(input, "document_id") || !identifier(input, "order_id") ||
        !identifier(input, "number") || !iso_date(text(input, "date")) ||
        !iso_date(text(input, "supply_date")) || input.value("seller") != seller ||
        !currency(input, doc.currency, doc.currency_minor_digits) ||
        !valid_text(input, "correction_reason", true))
    {
        return false;
    }
    const QString kind = text(input, "kind");
    if (kind != "invoice" && kind != "credit_note") {
        return false;
    }
    doc.credit = kind == "credit_note";
    doc.number = text(input, "number");
    doc.date = text(input, "date");
    const auto buyer = input.value("buyer").toObject();
    for (const char* key : { "name", "billing_address", "country_code" }) {
        if (!valid_text(buyer, key)) {
            return false;
        }
    }
    if (!valid_text(buyer, "tax_id", true)) {
        return false;
    }
    doc.buyer = text(buyer, "name") + "\n" + text(buyer, "billing_address") +
        "\n" + text(buyer, "country_code");
    if (!text(buyer, "tax_id").isEmpty()) {
        doc.buyer += "\nTax ID: " + text(buyer, "tax_id");
    }
    doc.metadata = doc.number + "\n\nDATE\n" + doc.date +
        "\n\nSUPPLY DATE\n" + text(input, "supply_date");
    if (doc.credit) {
        const auto original = input.value("original_document").toObject();
        if (!identifier(original, "document_id") || !identifier(original, "number") ||
            !iso_date(text(original, "date")) || text(original, "date") > doc.date ||
            original.value("document_id") == input.value("document_id") ||
            !valid_text(input, "correction_reason"))
        {
            return false;
        }
        doc.notes.push_back("Original invoice: " + text(original, "number") +
            " (" + text(original, "date") + ")");
        doc.notes.push_back("Reason: " + text(input, "correction_reason"));
    }
    else if (!input.value("original_document").isNull() ||
        !text(input, "correction_reason").isEmpty())
    {
        return false;
    }
    const auto payment = input.value("payment").toObject();
    const QString status = text(payment, "status");
    if (!valid_text(payment, "reference", true) ||
        (doc.credit ? status != "credited" : status != "unpaid" && status != "paid"))
    {
        return false;
    }
    if (!payment.value("due_date").isNull() &&
        (!iso_date(text(payment, "due_date")) || text(payment, "due_date") < doc.date))
    {
        return false;
    }
    if (status == "unpaid") {
        if (!iso_date(text(payment, "due_date"))) {
            return false;
        }
        doc.metadata += "\n\nPAYMENT DUE\n" + text(payment, "due_date");
        doc.payment_heading = "PAYMENT DETAILS";
        doc.payment_details = text(seller, "payment_instructions");
    }
    else {
        doc.payment_heading = doc.credit ? "CREDIT NOTE" : "PAID";
    }
    if (!text(payment, "reference").isEmpty()) {
        if (!doc.payment_details.isEmpty()) {
            doc.payment_details += "\n\n";
        }
        doc.payment_details += "Reference: " + text(payment, "reference");
    }
    if (!input.value("lines").isArray() || input.value("lines").toArray().isEmpty()) {
        return false;
    }
    QSet<QString> line_ids;
    for (const auto& value : input.value("lines").toArray()) {
        if (!value.isObject()) {
            return false;
        }
        const auto line = value.toObject();
        if (!identifier(line, "line_id") || line_ids.contains(text(line, "line_id")) ||
            !valid_text(line, "sku") || !valid_text(line, "description") ||
            !line.value("taxes").isArray() || line.value("taxes").toArray().isEmpty() ||
            (doc.credit ? !identifier(line, "original_line_id") :
                !line.value("original_line_id").isNull()))
        {
            return false;
        }
        line_ids.insert(text(line, "line_id"));
        qint64 quantity = 0, unit = 0, discount = 0, net = 0, tax = 0, gross = 0;
        if (!integer(line, "quantity", quantity) || quantity == 0 ||
            !integer(line, "unit_net_minor", unit) || !integer(line, "discount_minor", discount) ||
            !totals(line, net, tax, gross) || unit > k_max_json_integer / quantity ||
            discount > unit * quantity || unit * quantity - discount != net)
        {
            return false;
        }
        QString description = text(line, "description") + "\nQuantity: " + QString::number(quantity) +
            "; unit price: " + invoice_money(unit, doc.currency, doc.currency_minor_digits);
        if (discount) {
            description += "\nDiscount: " + invoice_money(discount, doc.currency, doc.currency_minor_digits);
        }
        qint64 tax_sum = 0;
        for (const auto& component : line.value("taxes").toArray()) {
            if (!component.isObject()) {
                return false;
            }
            const auto part = component.toObject();
            for (const char* key : { "tax_name", "jurisdiction", "treatment" }) {
                if (!valid_text(part, key)) {
                    return false;
                }
            }
            qint64 amount = 0, rate = 0, base = 0;
            if (!valid_text(part, "legal_basis", true) || !integer(part, "amount_minor", amount) ||
                !integer(part, "taxable_base_minor", base) ||
                !add(tax_sum, amount))
            {
                return false;
            }
            QString label = text(part, "tax_name") + " (" + text(part, "jurisdiction") + ")";
            if (!part.value("rate_ppm").isNull()) {
                if (!integer(part, "rate_ppm", rate) || rate > 1000000) {
                    return false;
                }
                label += " " + rate_percent(rate);
            }
            description += "\n" + label + ": " +
                invoice_money(base, doc.currency, doc.currency_minor_digits) + " taxable; " +
                invoice_money(amount, doc.currency, doc.currency_minor_digits) + " tax.\n" +
                text(part, "treatment");
            if (!text(part, "legal_basis").isEmpty()) {
                description += " — " + text(part, "legal_basis");
            }
            auto found = std::find_if(doc.taxes.begin(), doc.taxes.end(),
                [&](const auto& existing) { return existing.label == label; });
            if (found == doc.taxes.end()) {
                doc.taxes.push_back({ label, amount });
            }
            else if (!add(found->amount_minor, amount)) {
                return false;
            }
        }
        if (tax_sum != tax || !add(doc.net_minor, net) || !add(doc.tax_minor, tax) ||
            !add(doc.gross_minor, gross))
        {
            return false;
        }
        doc.items.push_back({ description, net });
    }
    qint64 net = 0, tax = 0, gross = 0;
    if (!totals(input.value("totals").toObject(), net, tax, gross) || gross == 0 ||
        net != doc.net_minor || tax != doc.tax_minor || gross != doc.gross_minor)
    {
        return false;
    }
    if (input.contains("tax_reporting")) {
        const auto reporting = input.value("tax_reporting").toObject();
        QString code;
        int digits = 0;
        qint64 amount = 0;
        if (!currency(reporting, code, digits) || !integer(reporting, "tax_minor", amount) ||
            !identifier(reporting, "fx_reference"))
        {
            return false;
        }
        doc.notes.push_back("Tax in reporting currency: " + invoice_money(amount, code, digits) +
            " (conversion reference: " + text(reporting, "fx_reference") + ")");
    }
    return true;
}

} // namespace

QString invoice_money(qint64 amount, const QString& code, int minor_digits)
{
    qint64 divisor = 1;
    for (int index = 0; index < minor_digits; ++index) {
        divisor *= 10;
    }
    QString result = QString::number(amount / divisor);
    if (minor_digits) {
        result += "." + QString::number(amount % divisor).rightJustified(minor_digits, '0');
    }
    return result + " " + code;
}

bool read_invoice_document(
    const QJsonObject& input, const QJsonObject& seller,
    const Invoice_request& request, Invoice_document& doc, std::string& error)
{
    for (const char* key : { "company_name", "registered_address", "vat_number",
        "company_number", "payment_instructions" })
    {
        if (!valid_text(seller, key)) {
            error = std::string("Missing or invalid template seller field: ") + key;
            return false;
        }
    }
    bool valid = false;
    if (input.value("version") == QJsonValue(1)) {
        valid = read_v1(input, seller, request, doc);
    }
    else if (input.value("version") == QJsonValue(2)) {
        doc.version = 2;
        valid = read_v2(input, seller, doc);
        if ((!request.invoice_number.empty() && request.invoice_number != doc.number.toStdString()) ||
            (!request.invoice_date.empty() && request.invoice_date != doc.date.toStdString()))
        {
            error = "The reserved document number and date cannot be overridden.";
            return false;
        }
    }
    const QJsonObject identity{ { "number", doc.number } };
    if (!valid || !identifier(identity, "number") || !iso_date(doc.date)) {
        error = "Invalid invoice document: check version, identity, dates, seller and exact monetary sums.";
        return false;
    }
    return true;
}

} // namespace briefutil
