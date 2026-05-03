// ============================================================================
// Renderer smoke test — validates document model + renderer + pagination
//
// Builds a multi-page document by hand and renders it to PDF.
// Run: test_renderer [output.pdf]
// ============================================================================

#include "briefutil/document_model.h"
#include "briefutil/pdf_measurement.h"
#include "briefutil/pdf_renderer.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>


static const color_t k_black       = { 0, 0, 0 };
static const color_t k_footer_col  = { 9.0f/255, 92.0f/255, 105.0f/255 };

int main(int argc, char* argv[])
{
    const char* output = argc > 1 ? argv[1] : "test_renderer_output.pdf";

    // -- Test 1: text measurement --
    {
        auto m = measure_text("Hello World", Font_id::SANS, 10, 12, 100, false);
        std::printf(
            "measure_text: width=%.1fpt height=%.1fpt lines=%d\n",
            m.width_pt,
            m.height_pt,
            m.line_count);
        if (m.line_count != 1 || m.width_pt < 1) {
            std::fprintf(stderr, "FAIL: unexpected measurement\n");
            return 1;
        }
    }

    // -- Test 2: text wrapping --
    {
        auto lines = wrap_text(
            "This is a fairly long line of text that should wrap across "
            "multiple lines when constrained to a narrow column width.",
            Font_id::SANS, 10, 50);
        std::printf("wrap_text: %zu lines for 50mm column\n", lines.size());
        if (lines.size() < 2) {
            std::fprintf(stderr, "FAIL: text should wrap to multiple lines\n");
            return 1;
        }
    }

    // -- Test 3: explicit newline handling --
    {
        auto lines = wrap_text(
            "Line one\nLine two\n\nLine four",
            Font_id::SANS,
            10,
            200);
        std::printf("wrap_text newlines: %zu lines\n", lines.size());
        if (lines.size() != 4) {
            std::fprintf(stderr, "FAIL: expected 4 lines, got %zu\n",
                         lines.size());
            return 1;
        }
    }

    // -- Test 4: multi-page document rendering --
    document_t doc;

    // Page 1: header elements + start of body
    {
        page_t p;

        // Fold marks
        p.elements.push_back(line_segment_t{ 0, 105, 10, 105, 0.5f, k_black });
        p.elements.push_back(line_segment_t{ 0, 210, 10, 210, 0.5f, k_black });
        p.elements.push_back(line_segment_t{ 0, 148.5f, 15, 148.5f, 0.5f, k_black });

        // Sender block
        p.elements.push_back(text_block_t{
            139.7f, 27.94f, 40.64f,
            "Max Mustermann\nMusterstr. 6\n12345 Musterstadt\n\nmax.mustermann@example.org",
            Font_id::SANS, 8, 10, k_black, false
        });

        // Return-address line
        const char* return_addr = "Max Mustermann \xb7 Musterstr. 6 \xb7 12345 Musterstadt";
        p.elements.push_back(text_block_t{
            25.4f, 51.05f, 85.09f,
            return_addr,
            Font_id::SANS, 8, 0, k_footer_col, false
        });

        // Separator — length matched to return-address text
        auto ret_m = measure_text(return_addr, Font_id::SANS, 8, 0, 200, false);
        float sep_end_mm = 25.4f + ret_m.width_pt / (72.0f / 25.4f);
        p.elements.push_back(line_segment_t{
            25.4f, 54.61f, sep_end_mm, 54.61f, 0.5f, k_black
        });

        // Recipient
        p.elements.push_back(text_block_t{
            25.4f, 55.88f, 111.76f,
            "Firma Beispiel GmbH\nHerrn Erich Beispiel\nBeispielweg 42\n54321 Beispielstadt",
            Font_id::SANS, 10, 12, k_black, false
        });

        // Date
        p.elements.push_back(text_block_t{
            139.7f, 76.2f, 40.64f,
            "14. M\xe4rz 2026",
            Font_id::SANS, 10, 0, k_black, false
        });

        // Subject
        p.elements.push_back(text_block_t{
            25.4f, 88.0f, 159.2f,
            "Betreff: Mehrseitiges Testdokument",
            Font_id::SANS_BOLD, 10, 12, k_black, false
        });

        // Body text (long enough to need page 2)
        std::string body;
        for (int i = 0; i < 8; i++) {
            if (i > 0) body += "\n\n";
            body += "Dies ist Absatz " + std::to_string(i + 1)
                + ". Der Text ist lang genug, um mehrere Seiten zu f"
                "\xfc" "llen und die Paginierung zu testen. "
                "Wir pr" "\xfc" "fen, ob der " "\xdc" "berlauf korrekt "
                "auf die n" "\xe4" "chste Seite umgebrochen wird.";
        }

        p.elements.push_back(text_block_t{
            25.4f, 100.0f, 159.2f,
            body,
            Font_id::SANS, 10, 12, k_black, true
        });

        // Footer (page 1)
        p.elements.push_back(text_block_t{
            25.4f, 279.0f, 159.2f,
            "Seite 1 von 2",
            Font_id::SANS, 9, 0, k_black, false
        });

        doc.pages.push_back(std::move(p));
    }

    // Page 2: continuation body + closing + footer
    {
        page_t p;

        // Fold marks on continuation page
        p.elements.push_back(line_segment_t{ 0, 105, 10, 105, 0.5f, k_black });
        p.elements.push_back(line_segment_t{ 0, 210, 10, 210, 0.5f, k_black });
        p.elements.push_back(line_segment_t{ 0, 148.5f, 15, 148.5f, 0.5f, k_black });

        // Continuation body
        std::string cont_body;
        for (int i = 0; i < 3; i++) {
            if (i > 0) cont_body += "\n\n";
            cont_body += "Fortsetzung Absatz " + std::to_string(i + 9)
                + ". Weitere Informationen auf der zweiten Seite.";
        }

        p.elements.push_back(text_block_t{
            25.4f, 25.4f, 159.2f,
            cont_body,
            Font_id::SANS, 10, 12, k_black, true
        });

        // Closing
        p.elements.push_back(text_block_t{
            25.4f, 100.0f, 159.2f,
            "Mit freundlichen Gr" "\xfc\xdf" "en",
            Font_id::SANS, 10, 12, k_black, false
        });

        // Signer name
        p.elements.push_back(text_block_t{
            25.4f, 130.0f, 159.2f,
            "Max Mustermann",
            Font_id::SANS, 10, 12, k_black, false
        });

        // Footer (page 2)
        p.elements.push_back(text_block_t{
            25.4f, 279.0f, 159.2f,
            "Seite 2 von 2",
            Font_id::SANS, 9, 0, k_black, false
        });

        doc.pages.push_back(std::move(p));
    }

    // Render
    auto result = render_pdf(
        doc,
        output,
        default_font_family(),
        default_localization());

    if (!result.ok) {
        std::fprintf(stderr, "FAIL: %s (%s)\n",
                     result.message.c_str(), result.detail.c_str());
        return 1;
    }

    // Verify file starts with %PDF-
    FILE* f = std::fopen(output, "rb");
    if (!f) {
        std::fprintf(stderr, "FAIL: cannot open output file\n");
        return 1;
    }
    char header[6] = {};
    std::fread(header, 1, 5, f);
    std::fclose(f);

    if (std::strncmp(header, "%PDF-", 5) != 0) {
        std::fprintf(stderr, "FAIL: output does not start with %%PDF-\n");
        return 1;
    }

    std::printf("\nAll tests passed.\n");
    std::printf("Multi-page PDF saved to: %s\n", output);

    return 0;
}
