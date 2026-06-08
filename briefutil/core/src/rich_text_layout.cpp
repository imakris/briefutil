#include "rich_text_layout.h"
#include "briefutil/pdf_measurement.h"
#include "pdf_mark2haru_support.h"

#include <mark2haru/table_layout.h>

#include <algorithm>
#include <climits>
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

static int saturating_list_marker_number(int start_number, size_t item_index)
{
    if (start_number >= INT_MAX) {
        return INT_MAX;
    }

    const size_t max_offset = static_cast<size_t>(INT_MAX - start_number);
    if (item_index > max_offset) {
        return INT_MAX;
    }

    return start_number + static_cast<int>(item_index);
}


// ============================================================================
// Inline style to Font_id mapping
// ============================================================================

static Font_id font_for_style(mark2haru::Inline_style style)
{
    switch (style) {
        case mark2haru::Inline_style::BOLD:        return Font_id::SANS_BOLD;
        case mark2haru::Inline_style::ITALIC:      return Font_id::SANS_ITALIC;
        case mark2haru::Inline_style::BOLD_ITALIC: return Font_id::SANS_BOLD_ITALIC;
        case mark2haru::Inline_style::CODE:        return Font_id::MONO;
        default:                                   return Font_id::SANS;
    }
}


// ============================================================================
// Positioned line - intermediate result from inline layout
// ============================================================================

struct Positioned_span
{
    float          x_mm;
    std::string    text;
    Font_id        font;
    float          size_pt;
    color_t        color;
};

struct Laid_out_line
{
    std::vector<Positioned_span>
                   spans;
    float          height_mm; // line height
};


// ============================================================================
// Inline layout - break text runs into positioned lines
//
// Operates on a flat list of inline runs. Wraps greedily within max_width_mm.
// Each run's text may contain newlines (hard line breaks).
// ============================================================================

static std::vector<Laid_out_line> layout_runs(
    const std::vector<mark2haru::Inline_run>&  runs,
    float                                      left_mm,
    float                                      max_width_mm,
    float                                      size_pt,
    float                                      lead_pt,
    color_t                                    color,
    const Font_family_config&                  fonts = default_font_family())
{
    std::vector<Laid_out_line> lines;
    float line_h_mm = pt_to_mm(lead_pt);

    // Cache space widths per Font_id so repeated words do not re-walk the
    // PDF font metrics.
    float space_widths_mm[5] = { -1, -1, -1, -1, -1 };
    auto space_width_mm_for = [&](Font_id fid) -> float {
        int idx = (int)fid;
        if (space_widths_mm[idx] < 0) {
            auto sp_m = measure_text(" ", fid, size_pt, 0, 1000, false, fonts);
            space_widths_mm[idx] = pt_to_mm(sp_m.width_pt);
        }
        return space_widths_mm[idx];
    };

    // Cache repeated word measurements per (font, word) pair so dense prose
    // and tables do not re-walk the font metrics for the same token.
    std::unordered_map<std::string, float> word_width_cache;
    auto word_width_mm = [&](Font_id fid, const std::string& word) -> float {
        std::string key = std::to_string((int)fid);
        key.push_back(':');
        key.append(word);

        auto it = word_width_cache.find(key);
        if (it != word_width_cache.end()) {
            return it->second;
        }
        auto  m        = measure_text(word, fid, size_pt, 0, 1000, false, fonts);
        float width_mm = pt_to_mm(m.width_pt);
        word_width_cache.emplace(std::move(key), width_mm);
        return width_mm;
    };

    // Current line being built.
    // We accumulate consecutive words of the same style into one span
    // so that spaces are real characters in the PDF, not positional gaps.
    std::vector<Positioned_span> current_spans;
    float cursor_x_mm = left_mm;

    // The span currently being accumulated (same style, same line)
    Positioned_span building_span     = { left_mm, "", Font_id::SANS, size_pt, color };
    bool            has_building_span = false;

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

    // Place one word, preceded by a single space only when the source had
    // whitespace before it and we are not at the start of a line. Spaces are
    // never invented between styled runs that were adjacent in the source, so
    // "foo**bar**" stays "foobar" rather than becoming "foo bar".
    auto emit_word = [&](Font_id fid, const std::string& word, bool space_before) {
        const float word_w_mm  = word_width_mm(fid, word);
        float       space_w_mm = (space_before && cursor_x_mm > left_mm)
            ? space_width_mm_for(fid) : 0.0f;

        // Line break if the word (plus any separating space) doesn't fit.
        if (cursor_x_mm + space_w_mm + word_w_mm > left_mm + max_width_mm &&
            cursor_x_mm                          > left_mm)
        {
            flush_line();
            space_w_mm = 0.0f;
        }

        const bool with_space = space_w_mm > 0.0f;
        if (!has_building_span || building_span.font != fid) {
            commit_building_span();
            building_span.x_mm    = cursor_x_mm;
            building_span.font    = fid;
            building_span.size_pt = size_pt;
            building_span.color   = color;
            building_span.text    = with_space ? " " + word : word;
            has_building_span     = true;
        }
        else {
            building_span.text += with_space ? " " + word : word;
        }

        cursor_x_mm += space_w_mm + word_w_mm;
    };

    // Tracks whether source whitespace has been seen since the last placed
    // word. It carries across run boundaries so the rendered spacing follows
    // the original Markdown instead of being synthesized between every word.
    bool pending_space = false;

    for (const auto& run : runs) {
        const Font_id fid = font_for_style(run.style);

        // Split run text on explicit newlines; everything else is words
        // separated by spaces, with whitespace recorded as break points.
        size_t pos = 0;
        while (pos <= run.text.size()) {
            const size_t nl      = run.text.find('\n', pos);
            const size_t seg_end = (nl == std::string::npos) ? run.text.size() : nl;

            size_t p = pos;
            while (p < seg_end) {
                if (run.text[p] == ' ') {
                    pending_space = true;
                    ++p;
                    continue;
                }
                size_t word_end = p;
                while (word_end < seg_end && run.text[word_end] != ' ') {
                    ++word_end;
                }
                emit_word(fid, run.text.substr(p, word_end - p), pending_space);
                pending_space = false;
                p = word_end;
            }

            if (nl == std::string::npos) {
                break;
            }
            flush_line();
            pending_space = false;
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
    float  m_y_mm;
    float  m_bottom_mm;
    int    m_page_index = 0;
    std::vector<std::vector<Page_element>>*
           m_pages;

    float  m_cont_top_mm;
    float  m_cont_bottom_mm;

    bool fits(float height_mm) const
    {
        return m_y_mm + height_mm <= m_bottom_mm;
    }

    void new_page()
    {
        m_page_index++;
        if (m_page_index >= (int)m_pages->size()) {
            m_pages->push_back({});
        }
        m_y_mm = m_cont_top_mm;
        m_bottom_mm = m_cont_bottom_mm;
    }

    void ensure_space(float height_mm)
    {
        if (!fits(height_mm)) {
            new_page();
        }
    }

    std::vector<Page_element>& current_elements()
    {
        return (*m_pages)[m_page_index];
    }

    void emit_lines(const std::vector<Laid_out_line>& lines)
    {
        for (const auto& line : lines) {
            if (!fits(line.height_mm)) {
                new_page();
            }
            for (const auto& span : line.spans) {
                current_elements().push_back(Text_span{
                    span.x_mm, m_y_mm, span.text,
                    span.font, span.size_pt, span.color
                });
            }
            m_y_mm += line.height_mm;
        }
    }
};


static Font_id font_from_mark2haru(mark2haru::Pdf_font font)
{
    switch (font) {
        case mark2haru::Pdf_font::BOLD:        return Font_id::SANS_BOLD;
        case mark2haru::Pdf_font::ITALIC:      return Font_id::SANS_ITALIC;
        case mark2haru::Pdf_font::BOLD_ITALIC: return Font_id::SANS_BOLD_ITALIC;
        case mark2haru::Pdf_font::MONO:        return Font_id::MONO;
        case mark2haru::Pdf_font::REGULAR:
        default:                               return Font_id::SANS;
    }
}

static color_t color_from_mark2haru(const mark2haru::color_t& color)
{
    return {
        static_cast<float>(color.r),
        static_cast<float>(color.g),
        static_cast<float>(color.b)
    };
}

static void append_mark2haru_table_elements(
    const mark2haru::Table_row_layout& row_layout,
    std::vector<Page_element>&         elements)
{
    for (const auto& element : row_layout.elements) {
        std::visit([&](const auto& value) {
            using Element_type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Element_type, mark2haru::Table_text_span>) {
                elements.push_back(Text_span{
                    pt_to_mm(value.x_pt),
                    pt_to_mm(value.y_pt),
                    value.text,
                    font_from_mark2haru(value.font),
                    static_cast<float>(value.size_pt),
                    color_from_mark2haru(value.color)
                });
            }
            else
            if constexpr (std::is_same_v<Element_type, mark2haru::table_line_t>) {
                elements.push_back(line_segment_t{
                    pt_to_mm(value.x1_pt),
                    pt_to_mm(value.y1_pt),
                    pt_to_mm(value.x2_pt),
                    pt_to_mm(value.y2_pt),
                    static_cast<float>(value.width_pt),
                    color_from_mark2haru(value.color)
                });
            }
            else
            if constexpr (std::is_same_v<Element_type, mark2haru::table_fill_rect_t>) {
                elements.push_back(filled_rect_t{
                    pt_to_mm(value.x_pt),
                    pt_to_mm(value.y_pt),
                    pt_to_mm(value.width_pt),
                    pt_to_mm(value.height_pt),
                    color_from_mark2haru(value.color)
                });
            }
        }, element);
    }
}


// ============================================================================
// Public layout entry point
// ============================================================================

Layout_result layout_body(
    const std::vector<mark2haru::Block>&   blocks,
    const Layout_params&                   params,
    float                                  first_page_top_mm,
    float                                  first_page_bottom_mm,
    float                                  cont_page_top_mm,
    float                                  cont_page_bottom_mm)
{
    Layout_result result;
    result.pages.push_back({});   // first page

    Page_cursor cursor;
    cursor.m_y_mm           = first_page_top_mm;
    cursor.m_bottom_mm      = first_page_bottom_mm;
    cursor.m_page_index     = 0;
    cursor.m_pages          = &result.pages;
    cursor.m_cont_top_mm    = cont_page_top_mm;
    cursor.m_cont_bottom_mm = cont_page_bottom_mm;

    for (const auto& block : blocks) {
        if (!result.error.empty()) {
            break;
        }

        std::visit([&](const auto& b) {
            using Block_type = std::decay_t<decltype(b)>;

            if constexpr (std::is_same_v<Block_type, mark2haru::Paragraph_block>) {
                cursor.ensure_space(pt_to_mm(params.typo.body_lead_pt));
                auto lines = layout_runs(
                    b.runs,
                    params.left_mm,
                    params.width_mm,
                    params.typo.body_size_pt,
                    params.typo.body_lead_pt,
                    params.body_color,
                    params.fonts);
                cursor.emit_lines(lines);
                cursor.m_y_mm += params.typo.paragraph_space_mm;

            }
            else
            if constexpr (std::is_same_v<Block_type, mark2haru::Heading_block>) {
                float hsize        = heading_size(params.typo, params.typo.body_size_pt, b.level);
                float hlead        = hsize * 1.2f;
                float space_before = heading_space_before(b.level);

                cursor.ensure_space(space_before + pt_to_mm(hlead));
                cursor.m_y_mm += space_before;

                auto lines = layout_runs(
                    b.runs,
                    params.left_mm,
                    params.width_mm,
                    hsize,
                    hlead,
                    params.body_color,
                    params.fonts);

                // Make heading runs bold
                for (auto& line : lines) {
                    for (auto& span : line.spans) {
                        if (span.font == Font_id::SANS) {
                            span.font = Font_id::SANS_BOLD;
                        }
                        else
                        if (span.font == Font_id::SANS_ITALIC) {
                            span.font = Font_id::SANS_BOLD_ITALIC;
                        }
                    }
                }

                cursor.emit_lines(lines);
                cursor.m_y_mm += params.typo.heading_space_after_mm;

            }
            else
            if constexpr (std::is_same_v<Block_type, mark2haru::List_block>) {
                float item_left  = params.left_mm + params.typo.list_indent_mm;
                float item_width = params.width_mm - params.typo.list_indent_mm;

                for (int idx = 0; idx < (int)b.items.size(); idx++) {
                    cursor.ensure_space(pt_to_mm(params.typo.body_lead_pt));

                    // Bullet or number
                    std::string marker;
                    if (b.ordered) {
                        marker = std::to_string(
                            saturating_list_marker_number(b.start_number, static_cast<size_t>(idx))) + ".";
                    }
                    else {
                        marker = "\xe2\x80\xa2"; // UTF-8 bullet •
                    }

                    cursor.current_elements().push_back(Text_span{
                        params.left_mm + k_bullet_offset_mm, cursor.m_y_mm,
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
                        params.fonts);
                    cursor.emit_lines(lines);
                    cursor.m_y_mm += params.typo.list_item_space_mm;
                }
                cursor.m_y_mm += params.typo.paragraph_space_mm;

            }
            else
            if constexpr (std::is_same_v<Block_type, mark2haru::Image_content_block>) {
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

                cursor.m_y_mm += 2.0f;

                // If still too tall for a full fresh page, scale to fit
                float avail = cursor.m_bottom_mm - cursor.m_y_mm - 2.0f;
                if (img_height_mm > avail && avail > 0) {
                    float scale = avail / img_height_mm;
                    img_height_mm *= scale;
                    img_width_mm *= scale;
                }

                cursor.current_elements().push_back(Image_block{
                    params.left_mm, cursor.m_y_mm, img_width_mm,
                    img_path
                });

                cursor.m_y_mm += img_height_mm + 2.0f;

            }
            else
            if constexpr (std::is_same_v<Block_type, mark2haru::Table_block>) {
                std::string detail;
                const auto metrics = make_mark2haru_measurement_context(params.fonts, &detail);
                if (!metrics) {
                    result.error = detail.empty()
                        ? "PDF measurement is unavailable."
                        : detail;
                    return;
                }

                mark2haru::table_style_t table_style;
                table_style.text_size_pt    = params.typo.body_size_pt;
                table_style.text_leading_pt = params.typo.body_lead_pt;
                table_style.cell_padding_pt = mm_to_pt(params.typo.table_cell_pad_mm);
                table_style.border_width_pt = k_table_border_width_pt;
                table_style.text_color      = {
                    params.body_color.r,
                    params.body_color.g,
                    params.body_color.b
                };

                auto table_columns = mark2haru::compute_table_columns(
                    b,
                    mm_to_pt(params.width_mm),
                    table_style,
                    *metrics);
                if (!table_columns.valid) {
                    result.error = params.loc.error_table_too_wide;
                    return;
                }
                else {
                    int header_rows = b.has_header ? 1 : 0;

                    auto emit_row = [&](int row_index) {
                        const auto row_layout = mark2haru::layout_table_row(
                            b,
                            row_index,
                            table_columns,
                            mm_to_pt(params.left_mm),
                            mm_to_pt(cursor.m_y_mm),
                            table_style,
                            *metrics);
                        append_mark2haru_table_elements(
                            row_layout,
                            cursor.current_elements());
                        const float row_h = pt_to_mm(row_layout.height_pt);
                        cursor.m_y_mm += row_h;
                        return row_h;
                    };

                    for (int ri = 0; ri < static_cast<int>(b.rows.size()); ri++) {
                        // Lay out the row into a temporary buffer to get the
                        // real height before committing to the page.
                        const auto probe = mark2haru::layout_table_row(
                            b,
                            ri,
                            table_columns,
                            mm_to_pt(params.left_mm),
                            mm_to_pt(cursor.m_y_mm),
                            table_style,
                            *metrics);
                        const float row_h = pt_to_mm(probe.height_pt);

                        // If the row doesn't fit, move to the next page,
                        // re-emit the header rows on the new page, and then
                        // emit this row at the new position.
                        if (!cursor.fits(row_h)) {
                            cursor.new_page();
                            for (int hi = 0; hi < header_rows && hi < ri; hi++) {
                                emit_row(hi);
                            }
                            // After re-emitting the header on a fresh page the
                            // row must still fit; a row taller than the page
                            // body cannot be placed without overflowing.
                            if (!cursor.fits(row_h)) {
                                result.error = params.loc.error_table_row_too_tall;
                                return;
                            }
                            emit_row(ri);
                        }
                        else {
                            append_mark2haru_table_elements(
                                probe,
                                cursor.current_elements());
                            cursor.m_y_mm += row_h;
                        }
                    }
                }
                cursor.m_y_mm += params.typo.paragraph_space_mm;

            }
            else
            if constexpr (std::is_same_v<Block_type, mark2haru::Code_block>) {
                // Code block: monospace font, light grey background.
                // Split across pages line-by-line if needed.
                static constexpr float   k_code_pad_mm = 3.0f;
                static constexpr color_t k_code_bg     = { 0.94f, 0.94f, 0.94f };
                float                    code_size_pt  = params.typo.body_size_pt * params.typo.code_scale;
                float                    code_lead_pt  = code_size_pt * 1.3f;

                // Split code into lines (preserve all whitespace)
                auto code_lines = split_lines(b.text);

                float line_h_mm = pt_to_mm(code_lead_pt);

                // Emit line by line, starting a new code region on each page
                size_t li = 0;
                while (li < code_lines.size()) {
                    // How many lines fit on the current page?
                    float avail = cursor.m_bottom_mm - cursor.m_y_mm
                        - 2 * k_code_pad_mm;
                    int lines_fit = std::max(1, (int)(avail / line_h_mm));

                    // If nothing fits, move to next page
                    if (avail < line_h_mm + 2 * k_code_pad_mm) {
                        cursor.new_page();
                        avail = cursor.m_bottom_mm - cursor.m_y_mm
                            - 2 * k_code_pad_mm;
                        lines_fit = std::max(1, (int)(avail / line_h_mm));
                    }

                    int chunk = std::min(lines_fit,
                        (int)(code_lines.size() - li));

                    float chunk_h = (float)chunk * line_h_mm
                        + 2 * k_code_pad_mm;

                    // Background rectangle for this chunk
                    cursor.current_elements().push_back(filled_rect_t{
                        params.left_mm, cursor.m_y_mm,
                        params.width_mm, chunk_h,
                        k_code_bg
                    });

                    // Code text lines
                    float code_y = cursor.m_y_mm + k_code_pad_mm;
                    for (int ci = 0; ci < chunk; ci++) {
                        cursor.current_elements().push_back(Text_span{
                            params.left_mm + k_code_pad_mm, code_y,
                            code_lines[li + ci], Font_id::MONO,
                            code_size_pt, params.body_color
                        });
                        code_y += line_h_mm;
                    }

                    cursor.m_y_mm += chunk_h;
                    li += chunk;
                }
                cursor.m_y_mm += params.typo.paragraph_space_mm;
            }
            else
            if constexpr (std::is_same_v<Block_type, mark2haru::Page_break_block>) {
                cursor.new_page();
            }
            else
            if constexpr (std::is_same_v<Block_type, mark2haru::Thematic_break_block>) {
                const float space_before_mm = pt_to_mm(params.typo.body_size_pt * 0.5f);
                const float space_after_mm  = pt_to_mm(params.typo.body_size_pt * 0.5f);
                const float rule_width_pt   = 0.5f;
                const float rule_height_mm  = pt_to_mm(rule_width_pt);

                cursor.ensure_space(space_before_mm + rule_height_mm + space_after_mm);
                cursor.m_y_mm += space_before_mm;

                const color_t rule_color = { 0.5f, 0.5f, 0.5f };
                cursor.current_elements().push_back(line_segment_t{
                    params.left_mm,
                    cursor.m_y_mm,
                    params.left_mm + params.width_mm,
                    cursor.m_y_mm,
                    rule_width_pt,
                    rule_color
                });
                cursor.m_y_mm += rule_height_mm + space_after_mm;
            }
        }, block);
    }

    result.last_page_used_mm = cursor.m_y_mm;
    return result;
}
