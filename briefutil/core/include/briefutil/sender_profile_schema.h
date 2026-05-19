#pragma once

#include "briefutil/sender_profile.h"

#include <string>
#include <vector>


// ============================================================================
// Sender profile field schema
//
// Single source of truth for the mapping between Sender_profile members,
// JSON keys (snake_case, used on disk), and QML keys (camelCase, used in
// the Qt Quick UI's QVariantMap representation).
//
// The descriptor tables below are iterated by:
//   - load_sender_profile / save_sender_profile (snake_case keys)
//   - Proxy::get_sender_profile                (camelCase keys, read side)
//
// Fields that need bespoke conversion (style enum, top_rule_color, and the
// language code's "en" fallback / language normalization) stay open-coded at
// their respective call sites.
// ============================================================================

struct Sender_string_field
{
    const char*                   json_key;
    const char*                   qml_key;
    std::string Sender_profile::* member;
};

struct Sender_string_array_field
{
    const char*                                json_key;
    const char*                                qml_key;
    std::vector<std::string> Sender_profile::* member;
};

inline constexpr Sender_string_field k_sender_string_fields[] = {
    { "id",                  "id",                &Sender_profile::id                  },
    { "email",               "email",             &Sender_profile::email               },
    { "return_address_line", "returnAddressLine", &Sender_profile::return_address_line },
    { "closing_phrase",      "closingPhrase",     &Sender_profile::closing_phrase      },
    { "signer_name",         "signerName",        &Sender_profile::signer_name         },
    { "signature_image",     "signatureImage",    &Sender_profile::signature_image     },
    { "logo_image",          "logoImage",         &Sender_profile::logo_image          },
    { "signer_title",        "signerTitle",       &Sender_profile::signer_title        },
};

inline constexpr Sender_string_array_field k_sender_string_array_fields[] = {
    { "sender_lines", "senderLines", &Sender_profile::sender_lines },
    { "footer_lines", "footerLines", &Sender_profile::footer_lines },
};
