#pragma once

#include "document_model.h"
#include "sender_profile.h"
#include <string>


// ============================================================================
// Letter builder — converts sender profile + user input into a Document
// ============================================================================

struct Letter_input
{
    std::string recipient;     // multi-line
    std::string subject;
    std::string body;          // multi-line, explicit newlines preserved
    std::string date;          // pre-formatted date string
};

Document build_letter(const Sender_profile& profile,
                      const Letter_input& input,
                      const std::string& profile_dir);
