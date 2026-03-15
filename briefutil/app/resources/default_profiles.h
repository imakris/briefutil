#pragma once

// Default sender profile JSON strings embedded at compile time.

inline const char* k_default_profile_simple_json = R"({
    "id": "Max Mustermann",
    "style": "simple",
    "sender_lines": [
        "Max Mustermann",
        "Musterstr. 6",
        "12345 Musterstadt"
    ],
    "email": "max.mustermann@example.org",
    "return_address_line": "Max Mustermann \u2022 Musterstr. 6 \u2022 12345 Musterstadt",
    "signer_name": "Max Mustermann",
    "signature_image": "mustermann_signature.png"
})";

inline const char* k_default_profile_commercial_json = R"({
    "id": "Max Mustermann, Mustermann AG",
    "style": "commercial",
    "sender_lines": [
        "Musterstr. 6",
        "12345 Musterstadt"
    ],
    "email": "kontakt@muster-ag.de",
    "return_address_line": "Muster AG \u2022 Musterstr. 6 \u2022 12345 Musterstadt",
    "signer_name": "Max Mustermann",
    "signer_title": "Gesch\u00e4ftsleitung der Muster AG",
    "signature_image": "mustermann_signature.png",
    "logo_image": "",
    "top_rule_color": [200, 200, 200],
    "footer_lines": [
        "Muster AG \u2022 Musterstr. 6 \u2022 12345 Musterstadt \u2022 kontakt@muster-ag.de",
        "Sitz der Gesellschaft: Musterstadt \u2022 Gesch\u00e4ftsleitung: Max Mustermann \u2022 Registergericht: Musterstadt HRB 54321"
    ]
})";
