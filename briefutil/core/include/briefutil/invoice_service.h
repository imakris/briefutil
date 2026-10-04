#pragma once

#include "briefutil/brief_service.h"

#include <string>

namespace briefutil {

// Invoice data and branding have their own JSON contracts. They do not change
// sender profiles or the interactive letter editor's document model.
struct Invoice_request
{
    std::string input_path;
    std::string template_path;
    std::string invoice_number;
    std::string invoice_date;
    std::string output_path;
    std::string receipt_path;
};

// Refuses existing PDF or receipt paths. The receipt binds the exact input
// bytes, template bytes and rendered PDF; it does not issue an order or licence.
Generation_result generate_invoice_pdf(const Invoice_request& request);

} // namespace briefutil
