#pragma once

#include "briefutil/document_model.h"
#include <string>
#include <vector>


// ============================================================================
// Sender profile — data-driven letter configuration loaded from JSON
// ============================================================================

enum class Profile_style
{
    SIMPLE,
    COMMERCIAL,
};

struct Sender_profile
{
    std::string              id;
    Profile_style            style = Profile_style::SIMPLE;

    // Sender block (right side of header)
    std::vector<std::string> sender_lines;       // e.g. {"Max Mustermann", "Musterstr. 6", ...}
    std::string              email;

    // Return-address summary (small text above recipient)
    std::string              return_address_line; // e.g. "Max Mustermann · Musterstr. 6 · 12345 Musterstadt"

    // Closing
    std::string              signer_name;
    std::string              signature_image;     // filename relative to profile directory

    // Commercial-only fields
    std::string              company_name;
    color_t                  company_name_color = {  49/255.0f, 132/255.0f, 155/255.0f };
    color_t                  top_rule_color     = { 200/255.0f, 200/255.0f, 200/255.0f };
    std::vector<std::string> footer_lines;
    std::string              signer_title;
};


// ============================================================================
// JSON loading
// ============================================================================

struct Profile_load_result
{
    bool           ok = false;
    Sender_profile profile;
    std::string    error;
};

Profile_load_result load_sender_profile(const std::string& json_path);
