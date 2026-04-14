#include "rich_text_layout.h"
#include "briefutil/pdf_measurement.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>


// ============================================================================
// Constants
// ============================================================================

static float heading_size(const typography_config_t& typo, float body_pt, int level)
{
    switch (level) {
        case 1:  return body_pt * typo.heading1_scale;
        case 2:  return body_pt * typo.heading2_scale;
        case 3:  return body_pt * typo.heading3_scale;
        default: return body_pt;
    }
}

static float heading_space_before(int level)
{
    switch (level) {
        case 1:  return 6.0f;
        case 2:  return 4.0f;
        default: return 3.0f;
    }
}

static constexpr float k_bullet_offset_mm      = 4.0f;
static constexpr float k_table_border_width_pt = 0.5f;


// ============================================================================
// Inline style to Font_id mapping
// ============================================================================

static Font_id font_for_style(Inline_style style)
{
    switch (style) {
        case Inline_style::BOLD:        return Font_id::SANS_BOLD;
        case Inline_style::ITALIC:      return Font_id::SANS_ITALIC;
        case Inline_style::BOLD_ITALIC: return Font_id::SANS_BOLD_ITALIC;
        case Inline_style::CODE:        return Font_id::MONO;
        default:                        return Font_id::SANS;
    }
}


// ============================================================================
// Positioned line — intermediate result from inline layout
// ============================================================================

struct positioned_span_t
{
    float       x_mm;
    std::string text;
    Font_id     font;
    float       size_pt;
    color_t     color;
};

struct laid_out_line_t
{
    std::vector<positioned_span_t> spans;
    float height_mm;   // line height
};


// ============================================================================
// Inline layout — break text runs into positioned lines
//
// Operates on a flat list of text_run_t values. Wraps greedily within max_width_mm.
// Each run's text may contain newlines (hard line breaks).
// ============================================================================

static std::vector<laid_out_line_t> layout_runs(
    const std::vector<text_run_t>& runs,
    float left_mm, float max_width_mm,
    float size_pt, float lead_pt, color_t color,
    Pdf_backend backend,
    const font_family_config_t& fonts = default_font_family())
{
    std::vector<laid_out_line_t> lines;
    float line_h_mm = pt_to_mm(lead_pt);

    // Cache space widths per Font_id — measure_text(" ", ...) is otherwise
    // called once per word, and each call walks the libHaru font width
    // table from scratch.
    float space_widths_mm[5] = { -1, -1, -1, -1, -1 };
    auto space_width_mm_for = [&](Font_id fid) -> float {
        int idx = (int)fid;
        if (space_widths_mm[idx] < 0) {
            auto sp_m = measure_text(backend, " ", fid, size_pt, 0, 1000, false, fonts);
            space_widths_mm[idx] = pt_to_mm(sp_m.width_pt);
        }
        return space_widths_mm[idx];
    };

    // Cache word widths per (font, word). Paragraphs often repeat common
    // words ("the", "a", "and", plus all punctuation-only tokens), and each
    // measure_text call walks the full libHaru width table.
    std::unordered_map<std::string, float> word_width_cache;
    auto word_width_mm = [&](Font_id fid, const std::string& word) -> float {
        // Prefix the key with the font id so different faces don't collide.
        std::string key;
        key.reserve(word.size() + 2);
        key.push_back((char)('0' + (int)fid));
        key.push_back(':');
        key.append(word);

        auto it = word_width_cache.find(key);
        if (it != word_width_cache.end()) {
            return it->second;
        }
        auto m = measure_text(backend, word, fid, size_pt, 0, 1000, false, fonts);
        float w_mm = pt_to_mm(m.width_pt);
        word_width_cache.emplace(std::move(key), w_mm);
        return w_mm;
    };

    // Current line being built.
    // We accumulate consecutive words of the same style into one span
    // so that spaces are real characters in the PDF, not positional gaps.
    std::vector<positioned_span_t> current_spans;
    float cursor_x_mm = left_mm;

    // The span currently being accumulated (same style, same line)
    positioned_span_t building_span = { left_mm, "", Font_id::SANS, size_pt, color };
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
        }
        else {
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

                    float word_w_mm = word_width_mm(fid, word);

                    float space_w_mm = (cursor_x_mm > left_mm)
                        ? space_width_mm_for(fid) : 0.0f;

                    // Line break if word doesn't fit
                    if (cursor_x_mm + space_w_mm + word_w_mm > left_mm + max_width_mm
                        && cursor_x_mm > left_mm)
                    {
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
                        }
                        else {
                            building_span.text = word;
                        }
                    }
                    else {
                        // Same style — append to current span
                        if (cursor_x_mm > left_mm) {
                            building_span.text += " " + word;
                        }
                        else {
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
// Page cursor - tracks vertical position and handles page breaks
// ============================================================================

struct Page_cursor
{
    float y_mm;
    float bottom_mm;
    int   page_index = 0;
    std::vector<std::vector<page_element_t>>* pages;

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

    std::vector<page_element_t>& current_elements()
    {
        return (*pages)[page_index];
    }

    void emit_lines(const std::vector<laid_out_line_t>& lines)
    {
        for (const auto& line : lines) {
            if (!fits(line.height_mm)) {
                new_page();
            }
            for (const auto& span : line.spans) {
                current_elements().push_back(text_span_t{
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
static float cell_min_width(
    const std::vector<text_run_t>& runs,
    float size_pt,
    Pdf_backend backend,
    const font_family_config_t& fonts)
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
            auto m = measure_text(backend, word, fid, size_pt, 0, 1000, false, fonts);
            max_word = std::max(max_word, pt_to_mm(m.width_pt));
            pos = end;
        }
    }
    return max_word;
}

static float cell_preferred_width(
    const std::vector<text_run_t>& runs,
    float size_pt,
    Pdf_backend backend,
    const font_family_config_t& fonts)
{
    float total = 0;
    for (const auto& run : runs) {
        Font_id fid = font_for_style(run.style);
        auto m = measure_text(backend, run.text, fid, size_pt, 0, 1000, false, fonts);
        total += pt_to_mm(m.width_pt);
    }
    return total;
}

struct table_layout_info_t
{
    int num_cols = 0;
    std::vector<float> col_widths_mm;
    bool valid = false;
};

static table_layout_info_t compute_table_columns(
    const table_block_t& tb,
    float available_mm,
    float size_pt,
    float cell_pad_mm,
    Pdf_backend backend,
    const font_family_config_t& fonts)
{
    if (tb.rows.empty()) return {};

    int num_cols = 0;
    for (const auto& row : tb.rows) {
        num_cols = std::max(num_cols, (int)row.cells.size());
    }
    if (num_cols == 0) return {};

    float pad = 2 * cell_pad_mm;
    std::vector<float> min_widths(num_cols, 0);
    std::vector<float> pref_widths(num_cols, 0);

    for (const auto& row : tb.rows) {
        for (int c = 0; c < (int)row.cells.size() && c < num_cols; c++) {
            float cmin = cell_min_width(row.cells[c].runs, size_pt, backend, fonts) + pad;
            float cpref = cell_preferred_width(row.cells[c].runs, size_pt, backend, fonts) + pad;
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

    table_layout_info_t info;
    info.num_cols = num_cols;
    info.valid = true;

    if (total_pref <= available_mm) {
        info.col_widths_mm = pref_widths;
    }
    else {
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
static float layout_table_row(const table_row_t& row,
                              const table_layout_info_t& tl,
                              float left_mm, float y_mm,
                              float size_pt, float lead_pt, color_t color,
                              float cell_pad_mm, bool is_header,
                              Pdf_backend backend,
                              const font_family_config_t& fonts,
                              std::vector<page_element_t>& elements)
{
    float row_height = pt_to_mm(lead_pt);
    float x = left_mm;

    // First pass: wrap cell contents and find tallest cell
    struct cell_layout_t
    {
        std::vector<laid_out_line_t> lines;
        float height_mm;
    };
    std::vector<cell_layout_t> cell_layouts;

    for (int c = 0; c < tl.num_cols; c++) {
        cell_layout_t cl;
        float cell_content_w = tl.col_widths_mm[c] - 2 * cell_pad_mm;

        std::vector<text_run_t> runs;
        if (c < (int)row.cells.size()) {
            runs = row.cells[c].runs;
        }

        // For header cells, force bold
        if (is_header) {
            for (auto& r : runs) {
                if (r.style == Inline_style::NORMAL)
                    r.style = Inline_style::BOLD;
                else
                if (r.style == Inline_style::ITALIC)
                    r.style = Inline_style::BOLD_ITALIC;
            }
        }

        cl.lines = layout_runs(
            runs,
            0,
            cell_content_w,
            size_pt,
            lead_pt,
            color,
            backend,
            fonts);
        cl.height_mm = 0;
        for (const auto& line : cl.lines) {
            cl.height_mm += line.height_mm;
        }
        row_height = std::max(row_height, cl.height_mm + 2 * cell_pad_mm);
        cell_layouts.push_back(std::move(cl));
    }

    // Second pass: emit cell content and borders
    x = left_mm;
    for (int c = 0; c < tl.num_cols; c++) {
        float cell_x = x + cell_pad_mm;
        float cell_y = y_mm + cell_pad_mm;

        // Emit cell text spans (offset by cell position)
        for (const auto& line : cell_layouts[c].lines) {
            for (const auto& span : line.spans) {
                elements.push_back(text_span_t{
                    cell_x + span.x_mm, cell_y,
                    span.text, span.font, span.size_pt, span.color
                });
            }
            cell_y += line.height_mm;
        }

        x += tl.col_widths_mm[c];
    }

    // Draw cell borders
    color_t border_color = { 0.5f, 0.5f, 0.5f };
    float table_right = left_mm;
    for (int c = 0; c < tl.num_cols; c++) {
        table_right += tl.col_widths_mm[c];
    }

    // Top border of row
    elements.push_back(line_segment_t{
        left_mm, y_mm, table_right, y_mm,
        k_table_border_width_pt, border_color
    });

    // Bottom border of row
    elements.push_back(line_segment_t{
        left_mm, y_mm + row_height, table_right, y_mm + row_height,
        k_table_border_width_pt, border_color
    });

    // Vertical borders
    x = left_mm;
    for (int c = 0; c <= tl.num_cols; c++) {
        elements.push_back(line_segment_t{
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

layout_result_t layout_body(
    const std::vector<body_block_t>& blocks,
    const layout_params_t& params,
    float first_page_top_mm,
    float first_page_bottom_mm,
    float cont_page_top_mm,
    float cont_page_bottom_mm)
{
    layout_result_t result;
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

            if constexpr (std::is_same_v<T, paragraph_block_t>) {
                cursor.ensure_space(pt_to_mm(params.typo.body_lead_pt));
                auto lines = layout_runs(
                    b.runs,
                    params.left_mm,
                    params.width_mm,
                    params.typo.body_size_pt,
                    params.typo.body_lead_pt,
                    params.body_color,
                    params.pdf_backend,
                    params.fonts);
                cursor.emit_lines(lines);
                cursor.y_mm += params.typo.paragraph_space_mm;

            }
            else
            if constexpr (std::is_same_v<T, heading_block_t>) {
                float hsize = heading_size(params.typo, params.typo.body_size_pt, b.level);
                float hlead = hsize * 1.2f;
                float space_before = heading_space_before(b.level);

                cursor.ensure_space(space_before + pt_to_mm(hlead));
                cursor.y_mm += space_before;

                auto lines = layout_runs(
                    b.runs,
                    params.left_mm,
                    params.width_mm,
                    hsize,
                    hlead,
                    params.body_color,
                    params.pdf_backend,
                    params.fonts);

                // Make heading runs bold
                for (auto& line : lines) {
                    for (auto& span : line.spans) {
                        if (span.font == Font_id::SANS)
                            span.font = Font_id::SANS_BOLD;
                        else
                        if (span.font == Font_id::SANS_ITALIC)
                            span.font = Font_id::SANS_BOLD_ITALIC;
                    }
                }

                cursor.emit_lines(lines);
                cursor.y_mm += params.typo.heading_space_after_mm;

            }
            else
            if constexpr (std::is_same_v<T, list_block_t>) {
                float item_left = params.left_mm + params.typo.list_indent_mm;
                float item_width = params.width_mm - params.typo.list_indent_mm;

                for (int idx = 0; idx < (int)b.items.size(); idx++) {
                    cursor.ensure_space(pt_to_mm(params.typo.body_lead_pt));

                    // Bullet or number
                    std::string marker;
                    if (b.ordered) {
                        marker = std::to_string(b.start_number + idx) + ".";
                    }
                    else {
                        marker = "\xe2\x80\xa2"; // UTF-8 bullet •
                    }

                    cursor.current_elements().push_back(text_span_t{
                        params.left_mm + k_bullet_offset_mm, cursor.y_mm,
                        marker, Font_id::SANS, params.typo.body_size_pt,
                        params.body_color
                    });

                    // Item content
                    auto lines = layout_runs(
                        b.items[idx].runs,
                        item_left,
                        item_width,
                        params.typo.body_size_pt,
                        params.typo.body_lead_pt,
                        params.body_color,
                        params.pdf_backend,
                        params.fonts);
                    cursor.emit_lines(lines);
                    cursor.y_mm += params.typo.list_item_space_mm;
                }
                cursor.y_mm += params.typo.paragraph_space_mm;

            }
            else
            if constexpr (std::is_same_v<T, image_content_block_t>) {
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
                }
                else {
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

                cursor.current_elements().push_back(image_block_t{
                    params.left_mm, cursor.y_mm, img_width_mm,
                    img_path
                });

                cursor.y_mm += img_height_mm + 2.0f;

            }
            else
            if constexpr (std::is_same_v<T, table_block_t>) {
                auto tl = compute_table_columns(
                    b,
                    params.width_mm,
                    params.typo.body_size_pt,
                    params.typo.table_cell_pad_mm,
                    params.pdf_backend,
                    params.fonts);
                if (!tl.valid) {
                    result.error = params.loc.error_table_too_wide;
                    return;
                }
                else {
                    int header_rows = b.has_header ? 1 : 0;

                    auto emit_row = [&](int row_index, bool is_header_row) {
                        std::vector<page_element_t> row_elements;
                        float row_h = layout_table_row(
                            b.rows[row_index], tl,
                            params.left_mm, cursor.y_mm,
                            params.typo.body_size_pt, params.typo.body_lead_pt,
                            params.body_color, params.typo.table_cell_pad_mm,
                            is_header_row, params.pdf_backend,
                            params.fonts, row_elements);
                        for (auto& elem : row_elements) {
                            cursor.current_elements().push_back(std::move(elem));
                        }
                        cursor.y_mm += row_h;
                        return row_h;
                    };

                    for (int ri = 0; ri < (int)b.rows.size(); ri++) {
                        bool is_header = (ri < header_rows);

                        // Lay out the row into a temporary buffer to get the
                        // real height before committing to the page.
                        std::vector<page_element_t> probe;
                        float row_h = layout_table_row(
                            b.rows[ri], tl,
                            params.left_mm, cursor.y_mm,
                            params.typo.body_size_pt, params.typo.body_lead_pt,
                            params.body_color, params.typo.table_cell_pad_mm, is_header,
                            params.pdf_backend, params.fonts, probe);

                        // If the row doesn't fit, move to the next page,
                        // re-emit the header rows on the new page, and then
                        // emit this row at the new position.
                        if (!cursor.fits(row_h)) {
                            cursor.new_page();
                            for (int hi = 0; hi < header_rows && hi < ri; hi++) {
                                emit_row(hi, true);
                            }
                            emit_row(ri, is_header);
                        }
                        else {
                            for (auto& elem : probe) {
                                cursor.current_elements().push_back(std::move(elem));
                            }
                            cursor.y_mm += row_h;
                        }
                    }
                }
                cursor.y_mm += params.typo.paragraph_space_mm;

            }
            else
            if constexpr (std::is_same_v<T, code_block_t>) {
                // Code block: monospace font, light grey background.
                // Split across pages line-by-line if needed.
                static constexpr float k_code_pad_mm = 3.0f;
                static constexpr color_t k_code_bg = { 0.94f, 0.94f, 0.94f };
                float code_size_pt = params.typo.body_size_pt * params.typo.code_scale;
                float code_lead_pt = code_size_pt * 1.3f;

                // Split code into lines (preserve all whitespace)
                auto code_lines = split_lines(b.text);

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
                    cursor.current_elements().push_back(filled_rect_t{
                        params.left_mm, cursor.y_mm,
                        params.width_mm, chunk_h,
                        k_code_bg
                    });

                    // Code text lines
                    float code_y = cursor.y_mm + k_code_pad_mm;
                    for (int ci = 0; ci < chunk; ci++) {
                        cursor.current_elements().push_back(text_span_t{
                            params.left_mm + k_code_pad_mm, code_y,
                            code_lines[li + ci], Font_id::MONO,
                            code_size_pt, params.body_color
                        });
                        code_y += line_h_mm;
                    }

                    cursor.y_mm += chunk_h;
                    li += chunk;
                }
                cursor.y_mm += params.typo.paragraph_space_mm;
            }
        }, block);
    }

    result.last_page_used_mm = cursor.y_mm;
    return result;
}
