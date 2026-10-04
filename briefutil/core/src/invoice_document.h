#pragma once

#include "briefutil/invoice_service.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <string>
#include <vector>

namespace briefutil {

struct Invoice_item
{
    QString description;
    qint64  net_minor = 0;
};

struct Invoice_tax
{
    QString label;
    qint64  amount_minor = 0;
};

struct Invoice_document
{
    int                       version = 1;
    bool                      credit = false;
    QString                   number;
    QString                   date;
    QString                   buyer;
    QString                   metadata;
    QString                   payment_heading;
    QString                   payment_details;
    QString                   currency = "EUR";
    int                       currency_minor_digits = 2;
    qint64                    net_minor = 0;
    qint64                    tax_minor = 0;
    qint64                    gross_minor = 0;
    std::vector<Invoice_item> items;
    std::vector<Invoice_tax>  taxes;
    QStringList               notes;
};

QString invoice_money(qint64 amount, const QString& currency, int minor_digits);

bool read_invoice_document(
    const QJsonObject&      input,
    const QJsonObject&      seller,
    const Invoice_request& request,
    Invoice_document&      document,
    std::string&           error);

} // namespace briefutil
