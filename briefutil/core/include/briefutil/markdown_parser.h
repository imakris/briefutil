#pragma once

#include "briefutil/body_content_model.h"
#include <string>
#include <vector>


// ============================================================================
// Markdown parser - converts a markdown string into body blocks
//
// Supports a constrained subset: paragraphs, bold, italic, bold+italic,
// ATX headings, bullet/ordered lists, images, and pipe tables.
// ============================================================================

std::vector<Body_block> parse_markdown(const std::string& input);
