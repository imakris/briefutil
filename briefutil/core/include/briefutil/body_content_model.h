#pragma once

#include <string>
#include <vector>
#include <variant>


// ============================================================================
// Body content model — semantic representation of letter body content
//
// Produced by the markdown parser, consumed by the layout engine.
// Represents meaning (paragraphs, headings, lists, etc.), not coordinates.
// ============================================================================

enum class Inline_style
{
    NORMAL,
    BOLD,
    ITALIC,
    BOLD_ITALIC,
    CODE,       // inline code (monospace)
};

struct text_run_t
{
    std::string  text;
    Inline_style style = Inline_style::NORMAL;
};

struct paragraph_block_t
{
    std::vector<text_run_t> runs;
};

struct heading_block_t
{
    int level = 1;
    std::vector<text_run_t> runs;
};

struct list_item_t
{
    std::vector<text_run_t> runs;
};

struct list_block_t
{
    bool ordered = false;
    int  start_number = 1;
    std::vector<list_item_t> items;
};

struct image_content_block_t
{
    std::string path;
    std::string alt_text;
};

struct table_cell_t
{
    std::vector<text_run_t> runs;
};

struct table_row_t
{
    std::vector<table_cell_t> cells;
};

struct table_block_t
{
    std::vector<table_row_t> rows;     // first row is the header
    bool has_header = false;
};

struct code_block_t
{
    std::string text;      // raw code content (newlines preserved)
    std::string language;  // optional language hint (from ``` tag)
};

using body_block_t = std::variant<
    paragraph_block_t,
    heading_block_t,
    list_block_t,
    image_content_block_t,
    table_block_t,
    code_block_t
>;
