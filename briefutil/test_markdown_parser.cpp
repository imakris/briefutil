// ============================================================================
// Markdown parser tests
// ============================================================================

#include "markdown_parser.h"

#include <cstdio>
#include <cstring>

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
        return 1; \
    } \
} while (0)

template<typename T>
static const T* get_block(const Body_block& b)
{
    return std::get_if<T>(&b);
}


int main()
{
    // -- Plain text (no markdown) --
    {
        auto blocks = parse_markdown("Hello world");
        ASSERT(blocks.size() == 1, "plain text should produce 1 block");
        auto* p = get_block<Paragraph_block>(blocks[0]);
        ASSERT(p, "plain text should be a Paragraph_block");
        ASSERT(p->runs.size() == 1, "plain text should have 1 run");
        ASSERT(p->runs[0].text == "Hello world", "text content mismatch");
        ASSERT(p->runs[0].style == Inline_style::normal, "should be normal style");
        std::printf("[OK] Plain text\n");
    }

    // -- Multi-line plain text preserves line breaks --
    {
        auto blocks = parse_markdown("Line one\nLine two");
        ASSERT(blocks.size() == 1, "consecutive lines = 1 paragraph");
        auto* p = get_block<Paragraph_block>(blocks[0]);
        ASSERT(p, "should be paragraph");
        ASSERT(p->runs.size() == 1, "single run with newline");
        ASSERT(p->runs[0].text == "Line one\nLine two",
               "newline preserved within paragraph");
        std::printf("[OK] Multi-line paragraph (newlines preserved)\n");
    }

    // -- Two paragraphs separated by blank line --
    {
        auto blocks = parse_markdown("Para one\n\nPara two");
        ASSERT(blocks.size() == 2, "blank line should separate paragraphs");
        ASSERT(get_block<Paragraph_block>(blocks[0]), "first is paragraph");
        ASSERT(get_block<Paragraph_block>(blocks[1]), "second is paragraph");
        std::printf("[OK] Paragraph separation\n");
    }

    // -- Bold --
    {
        auto blocks = parse_markdown("Hello **bold** world");
        auto* p = get_block<Paragraph_block>(blocks[0]);
        ASSERT(p && p->runs.size() == 3, "bold should split into 3 runs");
        ASSERT(p->runs[0].text == "Hello " && p->runs[0].style == Inline_style::normal,
               "pre-bold run");
        ASSERT(p->runs[1].text == "bold" && p->runs[1].style == Inline_style::bold,
               "bold run");
        ASSERT(p->runs[2].text == " world" && p->runs[2].style == Inline_style::normal,
               "post-bold run");
        std::printf("[OK] Bold\n");
    }

    // -- Italic --
    {
        auto blocks = parse_markdown("Hello *italic* world");
        auto* p = get_block<Paragraph_block>(blocks[0]);
        ASSERT(p && p->runs.size() == 3, "italic should split into 3 runs");
        ASSERT(p->runs[1].text == "italic" && p->runs[1].style == Inline_style::italic,
               "italic run");
        std::printf("[OK] Italic\n");
    }

    // -- Bold+italic --
    {
        auto blocks = parse_markdown("Hello ***both*** world");
        auto* p = get_block<Paragraph_block>(blocks[0]);
        ASSERT(p && p->runs.size() == 3, "bold+italic should split into 3 runs");
        ASSERT(p->runs[1].text == "both" &&
               p->runs[1].style == Inline_style::bold_italic, "bold_italic run");
        std::printf("[OK] Bold+italic\n");
    }

    // -- ATX headings --
    {
        auto blocks = parse_markdown("# Heading 1\n## Heading 2\n### Heading 3");
        ASSERT(blocks.size() == 3, "3 headings");
        auto* h1 = get_block<Heading_block>(blocks[0]);
        auto* h2 = get_block<Heading_block>(blocks[1]);
        auto* h3 = get_block<Heading_block>(blocks[2]);
        ASSERT(h1 && h1->level == 1, "h1 level");
        ASSERT(h2 && h2->level == 2, "h2 level");
        ASSERT(h3 && h3->level == 3, "h3 level");
        ASSERT(h1->runs.size() == 1 && h1->runs[0].text == "Heading 1", "h1 text");
        std::printf("[OK] ATX headings\n");
    }

    // -- Heading with inline formatting --
    {
        auto blocks = parse_markdown("# Hello **bold** heading");
        auto* h = get_block<Heading_block>(blocks[0]);
        ASSERT(h && h->runs.size() == 3, "heading with bold = 3 runs");
        ASSERT(h->runs[1].style == Inline_style::bold, "bold in heading");
        std::printf("[OK] Heading with inline formatting\n");
    }

    // -- Bullet list --
    {
        auto blocks = parse_markdown("- Item one\n- Item two\n- Item three");
        ASSERT(blocks.size() == 1, "bullet list = 1 block");
        auto* lb = get_block<List_block>(blocks[0]);
        ASSERT(lb && !lb->ordered, "should be unordered");
        ASSERT(lb->items.size() == 3, "3 items");
        ASSERT(lb->items[0].runs[0].text == "Item one", "first item text");
        std::printf("[OK] Bullet list\n");
    }

    // -- Ordered list --
    {
        auto blocks = parse_markdown("1. First\n2. Second\n3. Third");
        ASSERT(blocks.size() == 1, "ordered list = 1 block");
        auto* lb = get_block<List_block>(blocks[0]);
        ASSERT(lb && lb->ordered, "should be ordered");
        ASSERT(lb->items.size() == 3, "3 items");
        ASSERT(lb->start_number == 1, "starts at 1");
        std::printf("[OK] Ordered list\n");
    }

    // -- Image --
    {
        auto blocks = parse_markdown("![Alt text](image.png)");
        ASSERT(blocks.size() == 1, "image = 1 block");
        auto* img = get_block<Image_content_block>(blocks[0]);
        ASSERT(img, "should be image block");
        ASSERT(img->path == "image.png", "image path");
        ASSERT(img->alt_text == "Alt text", "alt text");
        std::printf("[OK] Image\n");
    }

    // -- Table --
    {
        auto blocks = parse_markdown(
            "| Name | Value |\n"
            "|------|-------|\n"
            "| A    | 1     |\n"
            "| B    | 2     |"
        );
        ASSERT(blocks.size() == 1, "table = 1 block");
        auto* tb = get_block<Table_block>(blocks[0]);
        ASSERT(tb, "should be table block");
        ASSERT(tb->has_header, "should have header");
        ASSERT(tb->rows.size() == 3, "3 rows (header + 2 data)");
        ASSERT(tb->rows[0].cells.size() == 2, "2 columns");
        ASSERT(tb->rows[0].cells[0].runs[0].text == "Name", "header cell 0");
        ASSERT(tb->rows[0].cells[1].runs[0].text == "Value", "header cell 1");
        ASSERT(tb->rows[1].cells[0].runs[0].text == "A", "data cell");
        std::printf("[OK] Table\n");
    }

    // -- Table with inline formatting --
    {
        auto blocks = parse_markdown(
            "| Col |\n"
            "|-----|\n"
            "| **bold** |"
        );
        auto* tb = get_block<Table_block>(blocks[0]);
        ASSERT(tb && tb->rows.size() == 2, "header + 1 data row");
        ASSERT(tb->rows[1].cells[0].runs[0].style == Inline_style::bold,
               "bold in table cell");
        std::printf("[OK] Table with inline formatting\n");
    }

    // -- Mixed content --
    {
        auto blocks = parse_markdown(
            "# Title\n"
            "\n"
            "Some **bold** text.\n"
            "\n"
            "- item a\n"
            "- item b\n"
            "\n"
            "![logo](logo.png)\n"
            "\n"
            "| X | Y |\n"
            "|---|---|\n"
            "| 1 | 2 |"
        );
        ASSERT(blocks.size() == 5, "heading + para + list + image + table");
        ASSERT(get_block<Heading_block>(blocks[0]), "first is heading");
        ASSERT(get_block<Paragraph_block>(blocks[1]), "second is paragraph");
        ASSERT(get_block<List_block>(blocks[2]), "third is list");
        ASSERT(get_block<Image_content_block>(blocks[3]), "fourth is image");
        ASSERT(get_block<Table_block>(blocks[4]), "fifth is table");
        std::printf("[OK] Mixed content\n");
    }

    // -- Unclosed bold treated as plain text --
    {
        auto blocks = parse_markdown("Hello **unclosed");
        auto* p = get_block<Paragraph_block>(blocks[0]);
        ASSERT(p && p->runs.size() == 1, "unclosed bold = plain text");
        ASSERT(p->runs[0].text == "Hello **unclosed", "preserved as-is");
        std::printf("[OK] Unclosed bold\n");
    }

    // -- Pipe text without separator is not a table --
    {
        auto blocks = parse_markdown("|foo|bar|");
        ASSERT(blocks.size() == 1, "pipe text without separator = 1 block");
        auto* p = get_block<Paragraph_block>(blocks[0]);
        ASSERT(p, "should be paragraph, not table");
        ASSERT(p->runs[0].text == "|foo|bar|", "pipe text preserved");
        std::printf("[OK] Pipe text without separator is not a table\n");
    }

    // -- Pipe text followed by non-separator is not a table --
    {
        auto blocks = parse_markdown("| a | b |\nSome text");
        ASSERT(blocks.size() == 1, "pipe + text = 1 paragraph");
        auto* p = get_block<Paragraph_block>(blocks[0]);
        ASSERT(p, "should be paragraph");
        std::printf("[OK] Pipe text followed by non-separator is plain text\n");
    }

    // -- Empty input --
    {
        auto blocks = parse_markdown("");
        ASSERT(blocks.empty(), "empty input = no blocks");
        std::printf("[OK] Empty input\n");
    }

    std::printf("\nAll markdown parser tests passed.\n");
    return 0;
}
