#include "briefutil/markdown_parser.h"
#include "briefutil/document_model.h"

#include <climits>
#include <cstring>


// ============================================================================
// Inline parsing — bold, italic, bold+italic
// ============================================================================

static bool starts_with(const std::string& s, size_t pos, const char* prefix)
{
    size_t len = std::strlen(prefix);
    return pos + len <= s.size() && s.compare(pos, len, prefix) == 0;
}

// Find the closing delimiter that matches `delim` starting after `start`.
// Returns npos if not found before end of line.
static size_t find_closing(const std::string& s, size_t start, const char* delim)
{
    size_t dlen = std::strlen(delim);
    size_t pos = start;
    while (pos + dlen <= s.size()) {
        if (s[pos] == '\n') return std::string::npos;
        if (s.compare(pos, dlen, delim) == 0) return pos;
        pos++;
    }
    return std::string::npos;
}

// Word-character test for underscore flanking. We use this rather than
// CommonMark's full punctuation rules so identifiers like `snake_case` stay
// as plain text and aren't misread as italic.
static bool is_word_char(char c)
{
    return (c >= 'a' && c <= 'z')
        || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9');
}

static std::vector<text_run_t> parse_inline(const std::string& text)
{
    std::vector<text_run_t> runs;
    size_t i = 0;
    std::string current;

    auto flush = [&](Inline_style style = Inline_style::NORMAL) {
        if (!current.empty()) {
            runs.push_back({ current, style });
            current.clear();
        }
    };

    auto can_open_underscore = [&](size_t pos) -> bool {
        return pos == 0 || !is_word_char(text[pos - 1]);
    };

    auto find_closing_underscore = [&](size_t start, size_t delim_len) -> size_t {
        size_t pos = start;
        while (pos + delim_len <= text.size()) {
            if (text[pos] == '\n') return std::string::npos;
            bool all_underscore = true;
            for (size_t k = 0; k < delim_len; k++) {
                if (text[pos + k] != '_') { all_underscore = false; break; }
            }
            if (all_underscore) {
                bool extends = (pos + delim_len < text.size()
                                && text[pos + delim_len] == '_');
                char after = (pos + delim_len < text.size())
                    ? text[pos + delim_len] : ' ';
                if (!extends && !is_word_char(after)) {
                    return pos;
                }
            }
            pos++;
        }
        return std::string::npos;
    };

    while (i < text.size()) {
        // Bold+italic: ***text***
        if (starts_with(text, i, "***")) {
            size_t close = find_closing(text, i + 3, "***");
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 3, close - i - 3),
                                 Inline_style::BOLD_ITALIC });
                i = close + 3;
                continue;
            }
        }

        // Bold: **text**
        if (starts_with(text, i, "**")) {
            size_t close = find_closing(text, i + 2, "**");
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 2, close - i - 2),
                                 Inline_style::BOLD });
                i = close + 2;
                continue;
            }
        }

        // Italic: *text*
        if (text[i] == '*' && !starts_with(text, i, "**")) {
            size_t close = find_closing(text, i + 1, "*");
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 1, close - i - 1),
                                 Inline_style::ITALIC });
                i = close + 1;
                continue;
            }
        }

        // Bold+italic: ___text___
        if (starts_with(text, i, "___") && can_open_underscore(i)) {
            size_t close = find_closing_underscore(i + 3, 3);
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 3, close - i - 3),
                                 Inline_style::BOLD_ITALIC });
                i = close + 3;
                continue;
            }
        }

        // Bold: __text__
        if (starts_with(text, i, "__") && can_open_underscore(i)) {
            size_t close = find_closing_underscore(i + 2, 2);
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 2, close - i - 2),
                                 Inline_style::BOLD });
                i = close + 2;
                continue;
            }
        }

        // Italic: _text_
        if (text[i] == '_' && !starts_with(text, i, "__")
            && can_open_underscore(i)) {
            size_t close = find_closing_underscore(i + 1, 1);
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 1, close - i - 1),
                                 Inline_style::ITALIC });
                i = close + 1;
                continue;
            }
        }

        // Inline code: `text`
        if (text[i] == '`') {
            size_t close = text.find('`', i + 1);
            if (close != std::string::npos) {
                flush();
                runs.push_back({ text.substr(i + 1, close - i - 1),
                                 Inline_style::CODE });
                i = close + 1;
                continue;
            }
        }

        // Link: [display text](url) — print "display text (url)" so the
        // URL is not silently lost. If the display text already equals the
        // URL (autolinks like [http://x](http://x)) we don't duplicate it.
        if (text[i] == '[') {
            size_t bracket_close = text.find("](", i + 1);
            if (bracket_close != std::string::npos) {
                size_t paren_close = text.find(')', bracket_close + 2);
                if (paren_close != std::string::npos) {
                    auto display = text.substr(i + 1, bracket_close - i - 1);
                    auto url = text.substr(bracket_close + 2,
                                           paren_close - bracket_close - 2);
                    if (display.empty()) {
                        current += url;
                    }
                    else
                    if (display == url) {
                        current += display;
                    }
                    else {
                        current += display;
                        current += " (";
                        current += url;
                        current += ")";
                    }
                    i = paren_close + 1;
                    continue;
                }
            }
        }

        current += text[i];
        i++;
    }

    flush();
    return runs;
}


// ============================================================================
// Line classification
// ============================================================================

enum class Line_type
{
    EMPTY,
    HEADING,
    BULLET_ITEM,
    ORDERED_ITEM,
    IMAGE,
    TABLE_ROW,
    TABLE_SEPARATOR,
    TEXT,
};

struct classified_line_t
{
    Line_type   type;
    std::string content;      // trimmed/extracted content
    int         heading_level = 0;
    int         list_number = 0;
};

static std::string trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r");
    return s.substr(start, end - start + 1);
}

static bool is_table_separator_line(const std::string& line)
{
    auto trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] != '|') return false;
    for (char c : trimmed) {
        if (c != '|' && c != '-' && c != ':' && c != ' ') return false;
    }
    return true;
}

static classified_line_t classify_line(const std::string& line)
{
    auto trimmed = trim(line);

    if (trimmed.empty()) {
        return { Line_type::EMPTY, "", 0, 0 };
    }

    // ATX headings: # Heading
    if (trimmed[0] == '#') {
        int level = 0;
        size_t i = 0;
        while (i < trimmed.size() && trimmed[i] == '#' && level < 6) {
            level++;
            i++;
        }
        if (i < trimmed.size() && trimmed[i] == ' ') {
            return { Line_type::HEADING, trim(trimmed.substr(i + 1)), level, 0 };
        }
    }

    // Bullet list: - item, + item, or * item
    // "* " is a bullet. "**" (no space) is bold. "* **bold**" is a bullet
    // whose content starts with bold — the space after * is the key
    // distinguisher.
    if ((trimmed[0] == '-' || trimmed[0] == '+' || trimmed[0] == '*') &&
        trimmed.size() > 1 && trimmed[1] == ' ') {
        return { Line_type::BULLET_ITEM, trim(trimmed.substr(2)), 0, 0 };
    }

    // Ordered list: 1. item
    if (trimmed[0] >= '0' && trimmed[0] <= '9') {
        size_t i = 0;
        while (i < trimmed.size() && trimmed[i] >= '0' && trimmed[i] <= '9') i++;
        if (i < trimmed.size() && trimmed[i] == '.' &&
            i + 1 < trimmed.size() && trimmed[i + 1] == ' ') {
            // Parse the number ourselves instead of std::stoi() so that a
            // very long run of digits (pathological input) can't throw.
            // Values that overflow int are clamped to INT_MAX.
            int num = 0;
            for (size_t k = 0; k < i; k++) {
                int digit = trimmed[k] - '0';
                if (num > (INT_MAX - digit) / 10) {
                    num = INT_MAX;
                    break;
                }
                num = num * 10 + digit;
            }
            return { Line_type::ORDERED_ITEM, trim(trimmed.substr(i + 2)), 0, num };
        }
    }

    // Image: ![alt](path)
    if (starts_with(trimmed, 0, "![")) {
        size_t alt_end = trimmed.find("](", 2);
        if (alt_end != std::string::npos) {
            size_t path_end = trimmed.find(')', alt_end + 2);
            if (path_end != std::string::npos) {
                return { Line_type::IMAGE, trimmed, 0, 0 };
            }
        }
    }

    // Table separator: |---|---|
    if (is_table_separator_line(trimmed)) {
        return { Line_type::TABLE_SEPARATOR, trimmed, 0, 0 };
    }

    // Table row: | cell | cell |
    if (trimmed[0] == '|' && trimmed.back() == '|') {
        return { Line_type::TABLE_ROW, trimmed, 0, 0 };
    }

    return { Line_type::TEXT, trimmed, 0, 0 };
}


// ============================================================================
// Block-level parsing helpers
// ============================================================================

// split_lines() from document_model.h is used instead of a local copy

static image_content_block_t parse_image_line(const std::string& line)
{
    // ![alt](path)
    size_t alt_start = 2;
    size_t alt_end = line.find("](", alt_start);
    std::string alt = (alt_end != std::string::npos)
        ? line.substr(alt_start, alt_end - alt_start) : "";
    size_t path_start = alt_end + 2;
    size_t path_end = line.find(')', path_start);
    std::string path = (path_end != std::string::npos)
        ? line.substr(path_start, path_end - path_start) : "";
    return { path, alt };
}

static std::vector<std::string> split_table_cells(const std::string& row)
{
    std::vector<std::string> cells;
    // Skip leading |
    size_t i = 0;
    if (i < row.size() && row[i] == '|') i++;

    while (i < row.size()) {
        size_t pipe = row.find('|', i);
        if (pipe == std::string::npos) break;
        cells.push_back(trim(row.substr(i, pipe - i)));
        i = pipe + 1;
    }
    return cells;
}


// ============================================================================
// Main parser
// ============================================================================

std::vector<body_block_t> parse_markdown(const std::string& input)
{
    std::vector<body_block_t> blocks;
    auto lines = split_lines(input);
    size_t i = 0;

    // Accumulate consecutive text lines into a paragraph
    std::string para_accum;

    auto flush_paragraph = [&]() {
        if (para_accum.empty()) return;
        paragraph_block_t pb;
        pb.runs = parse_inline(para_accum);
        blocks.push_back(std::move(pb));
        para_accum.clear();
    };

    while (i < lines.size()) {
        // Fenced code blocks: ```language ... ```
        auto trimmed_line = trim(lines[i]);
        if (starts_with(trimmed_line, 0, "```")) {
            flush_paragraph();
            std::string lang = trim(trimmed_line.substr(3));
            std::string code;
            i++;
            while (i < lines.size()) {
                auto tl = trim(lines[i]);
                if (tl == "```") {
                    i++;
                    break;
                }
                if (!code.empty()) code += '\n';
                code += lines[i];
                i++;
            }
            blocks.push_back(code_block_t{ code, lang });
            continue;
        }

        auto cl = classify_line(lines[i]);

        switch (cl.type) {
            case Line_type::EMPTY:
                flush_paragraph();
                i++;
                break;

            case Line_type::HEADING:
                flush_paragraph();
                {
                    heading_block_t hb;
                    hb.level = cl.heading_level;
                    hb.runs = parse_inline(cl.content);
                    blocks.push_back(std::move(hb));
                }
                i++;
                break;

            case Line_type::BULLET_ITEM:
            case Line_type::ORDERED_ITEM:
                flush_paragraph();
                {
                    bool ordered = (cl.type == Line_type::ORDERED_ITEM);
                    list_block_t lb;
                    lb.ordered = ordered;
                    lb.start_number = cl.list_number;

                    while (i < lines.size()) {
                        auto lcl = classify_line(lines[i]);
                        bool is_matching_item =
                            (ordered && lcl.type == Line_type::ORDERED_ITEM) ||
                            (!ordered && lcl.type == Line_type::BULLET_ITEM);

                        if (is_matching_item) {
                            list_item_t item;
                            item.runs = parse_inline(lcl.content);
                            lb.items.push_back(std::move(item));
                            i++;
                        }
                        else
                        if (!lb.items.empty() && lcl.type == Line_type::TEXT
                            && lines[i].size() >= 2
                            && (lines[i][0] == ' ' || lines[i][0] == '\t')) {
                            // Continuation line: indented text appended to
                            // the last list item.
                            auto& last = lb.items.back();
                            // Add a space before the continuation text
                            std::string cont = " " + lcl.content;
                            auto cont_runs = parse_inline(cont);
                            for (auto& r : cont_runs) {
                                last.runs.push_back(std::move(r));
                            }
                            i++;
                        }
                        else {
                            break;
                        }
                    }

                    blocks.push_back(std::move(lb));
                }
                break;

            case Line_type::IMAGE:
                flush_paragraph();
                blocks.push_back(parse_image_line(trim(lines[i])));
                i++;
                break;

            case Line_type::TABLE_ROW:
                // Only start a table if the next line is a separator.
                // Otherwise treat this as plain text.
                if (i + 1 < lines.size() &&
                    classify_line(lines[i + 1]).type == Line_type::TABLE_SEPARATOR) {
                    flush_paragraph();
                    table_block_t tb;

                    // Collect all table rows
                    while (i < lines.size()) {
                        auto tcl = classify_line(lines[i]);
                        if (tcl.type == Line_type::TABLE_SEPARATOR) {
                            tb.has_header = true;
                            i++;
                            continue;
                        }
                        if (tcl.type != Line_type::TABLE_ROW) break;

                        table_row_t row;
                        auto cell_texts = split_table_cells(tcl.content);
                        for (const auto& ct : cell_texts) {
                            table_cell_t cell;
                            cell.runs = parse_inline(ct);
                            row.cells.push_back(std::move(cell));
                        }
                        tb.rows.push_back(std::move(row));
                        i++;
                    }

                    blocks.push_back(std::move(tb));
                }
                else {
                    // Not a real table — treat as text
                    if (!para_accum.empty()) para_accum += '\n';
                    para_accum += cl.content;
                    i++;
                }
                break;

            case Line_type::TABLE_SEPARATOR:
                // Stray separator outside a table context, treat as text
                if (!para_accum.empty()) para_accum += '\n';
                para_accum += cl.content;
                i++;
                break;

            case Line_type::TEXT:
                // Preserve explicit line breaks within a paragraph.
                // Single newlines become hard breaks, matching the current
                // plain-text body path.
                if (!para_accum.empty()) para_accum += '\n';
                para_accum += cl.content;
                i++;
                break;

            default:
                break;
        }
    }

    flush_paragraph();
    return blocks;
}
