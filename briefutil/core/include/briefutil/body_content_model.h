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

struct Text_run
{
    std::string  text;
    Inline_style style = Inline_style::NORMAL;
};

struct Paragraph_block
{
    std::vector<Text_run> runs;
};

struct Heading_block
{
    int level = 1;
    std::vector<Text_run> runs;
};

struct List_item
{
    std::vector<Text_run> runs;
};

struct List_block
{
    bool ordered = false;
    int  start_number = 1;
    std::vector<List_item> items;
};

struct Image_content_block
{
    std::string path;
    std::string alt_text;
};

struct Table_cell
{
    std::vector<Text_run> runs;
};

struct Table_row
{
    std::vector<Table_cell> cells;
};

struct Table_block
{
    std::vector<Table_row> rows;     // first row is the header
    bool has_header = false;
};

struct Code_block
{
    std::string text;      // raw code content (newlines preserved)
    std::string language;  // optional language hint (from ``` tag)
};

using Body_block = std::variant<
    Paragraph_block,
    Heading_block,
    List_block,
    Image_content_block,
    Table_block,
    Code_block
>;
