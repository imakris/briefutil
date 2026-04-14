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

struct sender_profile_t
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
    std::string              logo_image;          // PNG filename relative to profile directory
    color_t                  top_rule_color     = { 200/255.0f, 200/255.0f, 200/255.0f };
    std::vector<std::string> footer_lines;
    std::string              signer_title;
};


// ============================================================================
// JSON loading
// ============================================================================

struct profile_load_result_t
{
    bool           ok = false;
    sender_profile_t profile;
    std::string    error;
};

profile_load_result_t load_sender_profile(const std::string& json_path);
bool save_sender_profile(
    const sender_profile_t& profile,
    const std::string& json_path,
    std::string* error = nullptr);
