#include "rich_text_layout.h"
#include "pdf_renderer_haru.h"

#include <algorithm>
#include <cmath>


// ============================================================================
// Constants
// ============================================================================

static constexpr float k_pts_per_mm = 72.0f / 25.4f;

static float pt_to_mm(float pt) { return pt / k_pts_per_mm; }

// Heading sizes relative to body size
static float heading_size(float body_pt, int level)
{
    switch (level) {
    case 1:  return body_pt * 1.6f;
    case 2:  return body_pt * 1.3f;
    case 3:  return body_pt * 1.1f;
    default: return body_pt;
    }
}

// Spacing before/after headings (mm)
static float heading_space_before(int level)
{
    switch (level) {
    case 1:  return 6.0f;
    case 2:  return 4.0f;
    default: return 3.0f;
    }
}

static constexpr float k_heading_space_after_mm = 2.0f;
static constexpr float k_paragraph_space_mm     = 3.0f;
static constexpr float k_list_indent_mm         = 8.0f;
static constexpr float k_list_item_space_mm     = 1.0f;
static constexpr float k_bullet_offset_mm       = 4.0f;
static constexpr float k_table_cell_pad_mm      = 2.0f;
static constexpr float k_table_border_width_pt  = 0.5f;
static constexpr float k_table_space_mm         = 3.0f;


// ============================================================================
// Inline style to Font_id mapping
// ============================================================================

static Font_id font_for_style(Inline_style style)
{
    switch (style) {
    case Inline_style::bold:        return Font_id::sans_bold;
    case Inline_style::italic:      return Font_id::sans_italic;
    case Inline_style::bold_italic: return Font_id::sans_bold_italic;
    case Inline_style::code:        return Font_id::mono;
    default:                        return Font_id::sans;
    }
}


// ============================================================================
// Positioned line — intermediate result from inline layout
// ============================================================================

struct Positioned_span
{
    float       x_mm;
    std::string text;
    Font_id     font;
    float       size_pt;
    Color       color;
};

struct Laid_out_line
{
    std::vector<Positioned_span> spans;
    float height_mm;   // line height
};


// ============================================================================
// Inline layout — break text runs into positioned lines
//
// Operates on a flat list of Text_runs. Wraps greedily within max_width_mm.
// Each run's text may contain newlines (hard line breaks).
// ============================================================================

static std::vector<Laid_out_line> layout_runs(
    const std::vector<Text_run>& runs,
    float left_mm, float max_width_mm,
    float size_pt, float lead_pt, Color color)
{
    std::vector<Laid_out_line> lines;
    float line_h_mm = pt_to_mm(lead_pt);

    // Current line being built.
    // We accumulate consecutive words of the same style into one span
    // so that spaces are real characters in the PDF, not positional gaps.
    std::vector<Positioned_span> current_spans;
    float cursor_x_mm = left_mm;

    // The span currently being accumulated (same style, same line)
    Positioned_span building_span = { left_mm, "", Font_id::sans, size_pt, color };
    bool has_building_span = false;

    auto commit_building_span = [&]() {
        if (has_building_span && !building_span.text.empty()) {
            current_spans.push_back(building_span);
        }
        has_building_span = false;
        building_span.text.clear();
    };

    auto flush_line = [&]() {
        commit_building_span();
        if (!current_spans.empty()) {
            lines.push_back({ std::move(current_spans), line_h_mm });
            current_spans.clear();
        } else {
            lines.push_back({ {}, line_h_mm });
        }
        cursor_x_mm = left_mm;
    };

    for (const auto& run : runs) {
        Font_id fid = font_for_style(run.style);

        // Split run text on explicit newlines
        size_t pos = 0;
        while (pos <= run.text.size()) {
            size_t nl = run.text.find('\n', pos);
            std::string segment = (nl == std::string::npos)
                ? run.text.substr(pos)
                : run.text.substr(pos, nl - pos);

            if (!segment.empty()) {
                size_t wi = 0;
                while (wi < segment.size()) {
                    while (wi < segment.size() && segment[wi] == ' ') wi++;
                    if (wi >= segment.size()) break;
                    size_t wend = segment.find(' ', wi);
                    if (wend == std::string::npos) wend = segment.size();
                    std::string word = segment.substr(wi, wend - wi);

                    auto word_m = measure_text(word, fid, size_pt, 0, 1000, false);
                    float word_w_mm = pt_to_mm(word_m.width_pt);

                    float space_w_mm = 0;
                    if (cursor_x_mm > left_mm) {
                        auto sp_m = measure_text(" ", fid, size_pt, 0, 1000, false);
                        space_w_mm = pt_to_mm(sp_m.width_pt);
                    }

                    // Line break if word doesn't fit
                    if (cursor_x_mm + space_w_mm + word_w_mm > left_mm + max_width_mm
                        && cursor_x_mm > left_mm) {
                        flush_line();
                        space_w_mm = 0;
                    }

                    // If style changed or no span is being built, start a new one
                    if (!has_building_span || building_span.font != fid) {
                        commit_building_span();
                        building_span.x_mm = cursor_x_mm;
                        building_span.font = fid;
                        building_span.size_pt = size_pt;
                        building_span.color = color;
                        building_span.text.clear();
                        has_building_span = true;

                        if (cursor_x_mm > left_mm) {
                            building_span.text = " " + word;
                        } else {
                            building_span.text = word;
                        }
                    } else {
                        // Same style — append to current span
                        if (cursor_x_mm > left_mm) {
                            building_span.text += " " + word;
                        } else {
                            building_span.text += word;
                        }
                    }

                    cursor_x_mm += space_w_mm + word_w_mm;
                    wi = wend;
                }
            }

            if (nl == std::string::npos) break;
            flush_line();
            pos = nl + 1;
        }
    }

    // Flush remaining content
    commit_building_span();
    if (!current_spans.empty()) {
        flush_line();
    }

    return lines;
}


// ============================================================================
// Page cursor — tracks vertical position and handles page breaks
// ============================================================================

struct Page_cursor
{
    float y_mm;
    float bottom_mm;
    int   page_index = 0;
    std::vector<std::vector<Page_element>>* pages;

    float cont_top_mm;
    float cont_bottom_mm;

    bool fits(float height_mm) const
    {
        return y_mm + height_mm <= bottom_mm;
    }

    void new_page()
    {
        page_index++;
        if (page_index >= (int)pages->size()) {
            pages->push_back({});
        }
        y_mm = cont_top_mm;
        bottom_mm = cont_bottom_mm;
    }

    void ensure_space(float height_mm)
    {
        if (!fits(height_mm)) {
            new_page();
        }
    }

    std::vector<Page_element>& current_elements()
    {
        return (*pages)[page_index];
    }

    void emit_lines(const std::vector<Laid_out_line>& lines)
    {
        for (const auto& line : lines) {
            if (!fits(line.height_mm)) {
                new_page();
            }
            for (const auto& span : line.spans) {
                current_elements().push_back(Text_span{
                    span.x_mm, y_mm, span.text,
                    span.font, span.size_pt, span.color
                });
            }
            y_mm += line.height_mm;
        }
    }
};


// ============================================================================
// Table layout helpers
// ============================================================================

// Measure the minimum width of a cell: the widest single unbreakable token
static float cell_min_width(const std::vector<Text_run>& runs,
                            float size_pt)
{
    float max_word = 0;
    for (const auto& run : runs) {
        Font_id fid = font_for_style(run.style);
        size_t pos = 0;
        while (pos < run.text.size()) {
            while (pos < run.text.size() && run.text[pos] == ' ') pos++;
            if (pos >= run.text.size()) break;
            size_t end = run.text.find(' ', pos);
            if (end == std::string::npos) end = run.text.size();
            std::string word = run.text.substr(pos, end - pos);
            auto m = measure_text(word, fid, size_pt, 0, 1000, false);
            max_word = std::max(max_word, pt_to_mm(m.width_pt));
            pos = end;
        }
    }
    return max_word;
}

// Measure the preferred width of a cell: unwrapped content width
static float cell_preferred_width(const std::vector<Text_run>& runs,
                                  float size_pt)
{
    float total = 0;
    for (const auto& run : runs) {
        Font_id fid = font_for_style(run.style);
        auto m = measure_text(run.text, fid, size_pt, 0, 1000, false);
        total += pt_to_mm(m.width_pt);
    }
    return total;
}

struct Table_layout_info
{
    int num_cols = 0;
    std::vector<float> col_widths_mm;
    bool valid = false;
};

static Table_layout_info compute_table_columns(const Table_block& tb,
                                               float available_mm,
                                               float size_pt)
{
    if (tb.rows.empty()) return {};

    int num_cols = 0;
    for (const auto& row : tb.rows) {
        num_cols = std::max(num_cols, (int)row.cells.size());
    }
    if (num_cols == 0) return {};

    float pad = 2 * k_table_cell_pad_mm;
    std::vector<float> min_widths(num_cols, 0);
    std::vector<float> pref_widths(num_cols, 0);

    for (const auto& row : tb.rows) {
        for (int c = 0; c < (int)row.cells.size() && c < num_cols; c++) {
            float cmin = cell_min_width(row.cells[c].runs, size_pt) + pad;
            float cpref = cell_preferred_width(row.cells[c].runs, size_pt) + pad;
            min_widths[c] = std::max(min_widths[c], cmin);
            pref_widths[c] = std::max(pref_widths[c], cpref);
        }
    }

    // Check if minimum widths fit
    float total_min = 0;
    for (float w : min_widths) total_min += w;
    if (total_min > available_mm) {
        return {};  // table too wide
    }

    // Check if preferred widths fit
    float total_pref = 0;
    for (float w : pref_widths) total_pref += w;

    Table_layout_info info;
    info.num_cols = num_cols;
    info.valid = true;

    if (total_pref <= available_mm) {
        info.col_widths_mm = pref_widths;
    } else {
        // Shrink proportionally, but never below minimum
        float excess = total_pref - available_mm;
        float shrinkable = total_pref - total_min;

        info.col_widths_mm.resize(num_cols);
        for (int c = 0; c < num_cols; c++) {
            float room = pref_widths[c] - min_widths[c];
            float reduction = (shrinkable > 0)
                ? excess * (room / shrinkable) : 0;
            info.col_widths_mm[c] = pref_widths[c] - reduction;
        }
    }

    return info;
}

// Lay out a single table row and return its height
static float layout_table_row(const Table_row& row,
                              const Table_layout_info& tl,
                              float left_mm, float y_mm,
                              float size_pt, float lead_pt, Color color,
                              bool is_header,
                              std::vector<Page_element>& elements)
{
    float row_height = pt_to_mm(lead_pt);
    float x = left_mm;

    // First pass: wrap cell contents and find tallest cell
    struct Cell_layout
    {
        std::vector<Laid_out_line> lines;
        float height_mm;
    };
    std::vector<Cell_layout> cell_layouts;

    for (int c = 0; c < tl.num_cols; c++) {
        Cell_layout cl;
        float cell_content_w = tl.col_widths_mm[c] - 2 * k_table_cell_pad_mm;

        std::vector<Text_run> runs;
        if (c < (int)row.cells.size()) {
            runs = row.cells[c].runs;
        }

        // For header cells, force bold
        if (is_header) {
            for (auto& r : runs) {
                if (r.style == Inline_style::normal)
                    r.style = Inline_style::bold;
                else if (r.style == Inline_style::italic)
                    r.style = Inline_style::bold_italic;
            }
        }

        cl.lines = layout_runs(runs, 0, cell_content_w,
                               size_pt, lead_pt, color);
        cl.height_mm = 0;
        for (const auto& line : cl.lines) {
            cl.height_mm += line.height_mm;
        }
        row_height = std::max(row_height, cl.height_mm + 2 * k_table_cell_pad_mm);
        cell_layouts.push_back(std::move(cl));
    }

    // Second pass: emit cell content and borders
    x = left_mm;
    for (int c = 0; c < tl.num_cols; c++) {
        float cell_x = x + k_table_cell_pad_mm;
        float cell_y = y_mm + k_table_cell_pad_mm;

        // Emit cell text spans (offset by cell position)
        for (const auto& line : cell_layouts[c].lines) {
            for (const auto& span : line.spans) {
                elements.push_back(Text_span{
                    cell_x + span.x_mm, cell_y,
                    span.text, span.font, span.size_pt, span.color
                });
            }
            cell_y += line.height_mm;
        }

        x += tl.col_widths_mm[c];
    }

    // Draw cell borders
    Color border_color = { 0.5f, 0.5f, 0.5f };
    float table_right = left_mm;
    for (int c = 0; c < tl.num_cols; c++) {
        table_right += tl.col_widths_mm[c];
    }

    // Top border of row
    elements.push_back(Line_segment{
        left_mm, y_mm, table_right, y_mm,
        k_table_border_width_pt, border_color
    });

    // Bottom border of row
    elements.push_back(Line_segment{
        left_mm, y_mm + row_height, table_right, y_mm + row_height,
        k_table_border_width_pt, border_color
    });

    // Vertical borders
    x = left_mm;
    for (int c = 0; c <= tl.num_cols; c++) {
        elements.push_back(Line_segment{
            x, y_mm, x, y_mm + row_height,
            k_table_border_width_pt, border_color
        });
        if (c < tl.num_cols) x += tl.col_widths_mm[c];
    }

    return row_height;
}


// ============================================================================
// Public layout entry point
// ============================================================================

Layout_result layout_body(const std::vector<Body_block>& blocks,
                          const Layout_params& params,
                          float first_page_top_mm,
                          float first_page_bottom_mm,
                          float cont_page_top_mm,
                          float cont_page_bottom_mm)
{
    Layout_result result;
    result.pages.push_back({});   // first page

    Page_cursor cursor;
    cursor.y_mm = first_page_top_mm;
    cursor.bottom_mm = first_page_bottom_mm;
    cursor.page_index = 0;
    cursor.pages = &result.pages;
    cursor.cont_top_mm = cont_page_top_mm;
    cursor.cont_bottom_mm = cont_page_bottom_mm;

    for (const auto& block : blocks) {
        if (!result.error.empty()) break;

        std::visit([&](const auto& b) {
            using T = std::decay_t<decltype(b)>;

            if constexpr (std::is_same_v<T, Paragraph_block>) {
                cursor.ensure_space(pt_to_mm(params.body_lead_pt));
                auto lines = layout_runs(b.runs,
                    params.left_mm, params.width_mm,
                    params.body_size_pt, params.body_lead_pt,
                    params.body_color);
                cursor.emit_lines(lines);
                cursor.y_mm += k_paragraph_space_mm;

            } else if constexpr (std::is_same_v<T, Heading_block>) {
                float hsize = heading_size(params.body_size_pt, b.level);
                float hlead = hsize * 1.2f;
                float space_before = heading_space_before(b.level);

                cursor.ensure_space(space_before + pt_to_mm(hlead));
                cursor.y_mm += space_before;

                auto lines = layout_runs(b.runs,
                    params.left_mm, params.width_mm,
                    hsize, hlead, params.body_color);

                // Make heading runs bold
                for (auto& line : lines) {
                    for (auto& span : line.spans) {
                        if (span.font == Font_id::sans)
                            span.font = Font_id::sans_bold;
                        else if (span.font == Font_id::sans_italic)
                            span.font = Font_id::sans_bold_italic;
                    }
                }

                cursor.emit_lines(lines);
                cursor.y_mm += k_heading_space_after_mm;

            } else if constexpr (std::is_same_v<T, List_block>) {
                float item_left = params.left_mm + k_list_indent_mm;
                float item_width = params.width_mm - k_list_indent_mm;

                for (int idx = 0; idx < (int)b.items.size(); idx++) {
                    cursor.ensure_space(pt_to_mm(params.body_lead_pt));

                    // Bullet or number
                    std::string marker;
                    if (b.ordered) {
                        marker = std::to_string(b.start_number + idx) + ".";
                    } else {
                        marker = "\xe2\x80\xa2"; // UTF-8 bullet •
                    }

                    cursor.current_elements().push_back(Text_span{
                        params.left_mm + k_bullet_offset_mm, cursor.y_mm,
                        marker, Font_id::sans, params.body_size_pt,
                        params.body_color
                    });

                    // Item content
                    auto lines = layout_runs(b.items[idx].runs,
                        item_left, item_width,
                        params.body_size_pt, params.body_lead_pt,
                        params.body_color);
                    cursor.emit_lines(lines);
                    cursor.y_mm += k_list_item_space_mm;
                }
                cursor.y_mm += k_paragraph_space_mm;

            } else if constexpr (std::is_same_v<T, Image_content_block>) {
                std::string img_path = b.path;
                bool is_absolute = (!b.path.empty() &&
                    (b.path[0] == '/' || b.path[0] == '\\' ||
                     (b.path.size() >= 2 && b.path[1] == ':')));
                if (!is_absolute && !params.profile_dir.empty() && !b.path.empty()) {
                    img_path = params.profile_dir + "/" + b.path;
                }

                // Read actual PNG dimensions for correct layout
                auto dims = measure_png(img_path);
                float img_width_mm, img_height_mm;
                if (dims.valid && dims.width_px > 0) {
                    // Convert pixels to mm at 96 DPI, then clamp to body width
                    float natural_w_mm = dims.width_px * 25.4f / 96.0f;
                    float natural_h_mm = dims.height_px * 25.4f / 96.0f;
                    img_width_mm = std::min(natural_w_mm, params.width_mm);
                    img_height_mm = img_width_mm * (natural_h_mm / natural_w_mm);
                } else {
                    // Fallback if PNG can't be read
                    img_width_mm = params.width_mm * 0.5f;
                    img_height_mm = img_width_mm * 0.5f;
                }

                float needed = img_height_mm + 4.0f;

                // If it doesn't fit on the current page, try a new page
                if (!cursor.fits(needed)) {
                    cursor.new_page();
                }

                cursor.y_mm += 2.0f;

                // If still too tall for a full fresh page, scale to fit
                float avail = cursor.bottom_mm - cursor.y_mm - 2.0f;
                if (img_height_mm > avail && avail > 0) {
                    float scale = avail / img_height_mm;
                    img_height_mm *= scale;
                    img_width_mm *= scale;
                }

                cursor.current_elements().push_back(Image_block{
                    params.left_mm, cursor.y_mm, img_width_mm,
                    img_path
                });

                cursor.y_mm += img_height_mm + 2.0f;

            } else if constexpr (std::is_same_v<T, Table_block>) {
                auto tl = compute_table_columns(b, params.width_mm,
                                                params.body_size_pt);
                if (!tl.valid) {
                    result.error = "Eine Tabelle ist zu breit f\xc3\xbcr "
                                   "den verf\xc3\xbcgbaren Seitenbereich.";
                    return;
                } else {
                    int header_rows = b.has_header ? 1 : 0;

                    for (int ri = 0; ri < (int)b.rows.size(); ri++) {
                        bool is_header = (ri < header_rows);

                        // Lay out the row into a temporary buffer to get
                        // the real height before committing to the page.
                        std::vector<Page_element> row_elements;
                        float row_h = layout_table_row(
                            b.rows[ri], tl,
                            params.left_mm, cursor.y_mm,
                            params.body_size_pt, params.body_lead_pt,
                            params.body_color, is_header,
                            row_elements);

                        // If the row doesn't fit, move to the next page
                        // and re-layout at the new position.
                        if (!cursor.fits(row_h)) {
                            cursor.new_page();
                            row_elements.clear();
                            row_h = layout_table_row(
                                b.rows[ri], tl,
                                params.left_mm, cursor.y_mm,
                                params.body_size_pt, params.body_lead_pt,
                                params.body_color, is_header,
                                row_elements);
                        }

                        // If the row still doesn't fit (taller than
                        // a full page), emit it anyway — truncation is
                        // preferable to an infinite loop.
                        for (auto& elem : row_elements) {
                            cursor.current_elements().push_back(std::move(elem));
                        }
                        cursor.y_mm += row_h;
                    }
                }
                cursor.y_mm += k_table_space_mm;

            } else if constexpr (std::is_same_v<T, Code_block>) {
                // Code block: monospace font, light grey background.
                // Split across pages line-by-line if needed.
                static constexpr float k_code_pad_mm = 3.0f;
                static constexpr Color k_code_bg = { 0.94f, 0.94f, 0.94f };
                float code_size_pt = params.body_size_pt * 0.85f;
                float code_lead_pt = code_size_pt * 1.3f;

                // Split code into lines (preserve all whitespace)
                std::vector<std::string> code_lines;
                {
                    size_t pos = 0;
                    while (pos <= b.text.size()) {
                        size_t nl = b.text.find('\n', pos);
                        if (nl == std::string::npos) {
                            code_lines.push_back(b.text.substr(pos));
                            break;
                        }
                        code_lines.push_back(b.text.substr(pos, nl - pos));
                        pos = nl + 1;
                    }
                }

                float line_h_mm = pt_to_mm(code_lead_pt);

                // Emit line by line, starting a new code region on each page
                size_t li = 0;
                while (li < code_lines.size()) {
                    // How many lines fit on the current page?
                    float avail = cursor.bottom_mm - cursor.y_mm
                        - 2 * k_code_pad_mm;
                    int lines_fit = std::max(1, (int)(avail / line_h_mm));

                    // If nothing fits, move to next page
                    if (avail < line_h_mm + 2 * k_code_pad_mm) {
                        cursor.new_page();
                        avail = cursor.bottom_mm - cursor.y_mm
                            - 2 * k_code_pad_mm;
                        lines_fit = std::max(1, (int)(avail / line_h_mm));
                    }

                    int chunk = std::min(lines_fit,
                                         (int)(code_lines.size() - li));

                    float chunk_h = (float)chunk * line_h_mm
                        + 2 * k_code_pad_mm;

                    // Background rectangle for this chunk
                    cursor.current_elements().push_back(Filled_rect{
                        params.left_mm, cursor.y_mm,
                        params.width_mm, chunk_h,
                        k_code_bg
                    });

                    // Code text lines
                    float code_y = cursor.y_mm + k_code_pad_mm;
                    for (int ci = 0; ci < chunk; ci++) {
                        cursor.current_elements().push_back(Text_span{
                            params.left_mm + k_code_pad_mm, code_y,
                            code_lines[li + ci], Font_id::mono,
                            code_size_pt, params.body_color
                        });
                        code_y += line_h_mm;
                    }

                    cursor.y_mm += chunk_h;
                    li += chunk;
                }
                cursor.y_mm += k_paragraph_space_mm;
            }
        }, block);
    }

    result.last_page_used_mm = cursor.y_mm;
    return result;
}
