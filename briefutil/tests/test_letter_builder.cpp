// ============================================================================
// Full pipeline test - profile loading + letter builder + renderer
// ============================================================================

#include "briefutil/default_profiles.h"
#include "briefutil/letter_builder.h"
#include "briefutil/pdf_measurement.h"
#include "briefutil/pdf_renderer.h"
#include "briefutil/sender_profile.h"
#include "briefutil/typography_config.h"

#include "test_helpers.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>

#include <climits>
#include <cmath>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>


static bool nearly_equal(float a, float b, float eps = 0.01f)
{
    return std::fabs(a - b) < eps;
}

static bool write_test_signature_png(const QString& path)
{
    static const QByteArray k_png_bytes = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQImWP4z8DwHwAFAAH/e+m+7wAAAABJRU5ErkJggg==");
    if (k_png_bytes.isEmpty()) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    return file.write(k_png_bytes) == k_png_bytes.size();
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    const char* output = argc > 1 ? argv[1] : "test_letter_output.pdf";

    // -- Test 1: parse embedded simple profile JSON --
    {
        Profile_fixture fx("briefutil_test_simple", k_default_profile_simple_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: profile load: %s\n", fx.error.c_str());
            return 1;
        }

        auto& p = fx.profile;
        if (p.id != "Max Mustermann") {
            std::fprintf(stderr, "FAIL: expected id 'Max Mustermann', got '%s'\n",
                p.id.c_str());
            return 1;
        }
        if (p.style != Profile_style::SIMPLE) {
            std::fprintf(stderr, "FAIL: expected simple style\n");
            return 1;
        }
        if (p.sender_lines.size() != 3) {
            std::fprintf(stderr, "FAIL: expected 3 sender_lines, got %zu\n",
                p.sender_lines.size());
            return 1;
        }
        if (p.return_address_line
            != "Max Mustermann \xE2\x80\xA2 Musterstr. 6 \xE2\x80\xA2 12345 Musterstadt")
        {
            std::fprintf(stderr, "FAIL: return_address_line is incorrectly decoded: '%s'\n",
                p.return_address_line.c_str());
            return 1;
        }
        std::printf("[OK] Simple profile loaded correctly\n");
    }

    // -- Test 2: build and render a simple letter --
    {
        Profile_fixture fx("briefutil_test", k_default_profile_simple_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: profile load for letter test: %s\n",
                fx.error.c_str());
            return 1;
        }
        // Clear signature_image since the test dir has no PNG
        fx.profile.signature_image.clear();

        Letter_input input;
        input.recipient = "Firma Beispiel GmbH\nHerrn Erich Beispiel\n"
                          "Beispielweg 42\n54321 Beispielstadt";
        // All text must be UTF-8 so TrueType rendering preserves non-CP1252 text.
        // UTF-8: ae=\xc3\xa4 oe=\xc3\xb6 ue=\xc3\xbc Ue=\xc3\x9c ss=\xc3\x9f
        input.subject = "Angebot f\xc3\xbcr Dienstleistungen";
        input.date = "14. M\xc3\xa4rz 2026";
        input.body =
            "Sehr geehrter Herr Beispiel,\n"
            "\n"
            "vielen Dank f\xc3\xbcr Ihre Anfrage vom 10. M\xc3\xa4rz 2026. "
            "Gerne \xc3" "\xbc" "bermitteln wir Ihnen unser Angebot f\xc3\xbcr die "
            "gew\xc3\xbcnschten Dienstleistungen.\n"
            "\n"
            "Bei R\xc3" "\xbc" "ckfragen stehen wir Ihnen jederzeit zur Verf\xc3\xbcgung.";

        auto br = build_letter(fx.profile, input, qs(fx.tmp_dir));
        if (!br.error.empty()) {
            std::fprintf(stderr, "FAIL: build_letter: %s\n", br.error.c_str());
            return 1;
        }
        auto& doc = br.doc;

        if (doc.pages.empty()) {
            std::fprintf(stderr, "FAIL: build_letter produced no pages\n");
            return 1;
        }
        std::printf("[OK] Letter built: %zu page(s)\n", doc.pages.size());

        auto rr = render_pdf(doc, output, *br.measurement);
        if (!rr.ok) {
            std::fprintf(stderr, "FAIL: render_pdf: %s (%s)\n",
                rr.message.c_str(), rr.detail.c_str());
            return 1;
        }
        std::printf("[OK] PDF rendered to: %s\n", output);
    }

    // -- Test 3: multi-page letter (long body) --
    {
        Profile_fixture fx("briefutil_test2", k_default_profile_simple_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: multi-page test setup: %s\n", fx.error.c_str());
            return 1;
        }
        fx.profile.signature_image.clear();

        Letter_input input;
        input.recipient = "Firma Beispiel GmbH\n54321 Beispielstadt";
        input.subject   = "Langer Brief";
        input.date      = "14. M\xc3\xa4rz 2026";

        // Generate a long body (UTF-8)
        std::string body;
        for (int i = 0; i < 15; i++) {
            if (i > 0) {
                body += "\n\n";
            }
            body += "Dies ist Absatz " + std::to_string(i + 1) +
                ". Der Text ist absichtlich lang, um die Paginierung"
                " zu testen und sicherzustellen, dass der \xc3""\x9c" "berlauf "
                "korrekt auf Folgeseiten umgebrochen wird.";
        }
        input.body = body;

        auto br = build_letter(fx.profile, input, qs(fx.tmp_dir));
        if (!br.error.empty()) {
            std::fprintf(stderr, "FAIL: multi-page build_letter: %s\n", br.error.c_str());
            return 1;
        }
        auto& doc = br.doc;

        std::printf("[OK] Multi-page letter: %zu page(s)\n", doc.pages.size());
        if (doc.pages.size() < 2) {
            std::fprintf(stderr, "FAIL: expected multi-page output\n");
            return 1;
        }

        std::string mp_output = std::string(output) + ".multipage.pdf";
        auto        rr        = render_pdf(doc, mp_output, *br.measurement);
        if (!rr.ok) {
            std::fprintf(stderr, "FAIL: render multi-page: %s\n", rr.detail.c_str());
            return 1;
        }
        std::printf("[OK] Multi-page PDF: %s\n", mp_output.c_str());
    }

    // -- Test 4: commercial profile load + render --
    {
        Profile_fixture fx("briefutil_test3", k_default_profile_commercial_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: commercial profile load: %s\n", fx.error.c_str());
            return 1;
        }
        if (fx.profile.style != Profile_style::COMMERCIAL) {
            std::fprintf(stderr, "FAIL: expected commercial style\n");
            return 1;
        }
        if (fx.profile.footer_lines.size() != 2) {
            std::fprintf(stderr, "FAIL: expected 2 commercial footer lines, got %zu\n",
                fx.profile.footer_lines.size());
            return 1;
        }
        if (fx.profile.return_address_line
            != "Muster AG \xE2\x80\xA2 Musterstr. 6 \xE2\x80\xA2 12345 Musterstadt")
        {
            std::fprintf(stderr, "FAIL: commercial return_address_line is incorrect: '%s'\n",
                fx.profile.return_address_line.c_str());
            return 1;
        }

        fx.profile.signature_image.clear();

        Letter_input input;
        input.recipient = "Firma Beispiel GmbH\n54321 Beispielstadt";
        input.subject   = "Kommerzieller Brief";
        input.date      = "14. M\xc3\xa4rz 2026";
        input.body =
            "Sehr geehrte Damen und Herren,\n\n"
            "anbei erhalten Sie unser aktualisiertes Angebot.";

        auto br = build_letter(fx.profile, input, qs(fx.tmp_dir));
        if (!br.error.empty()) {
            std::fprintf(stderr, "FAIL: commercial build_letter: %s\n", br.error.c_str());
            return 1;
        }
        auto& doc = br.doc;
        if (doc.pages.empty()) {
            std::fprintf(stderr, "FAIL: commercial build_letter produced no pages\n");
            return 1;
        }

        bool       found_sender             = false;
        bool       found_return_line        = false;
        bool       found_return_underline   = false;
        bool       found_recipient          = false;
        bool       found_subject            = false;
        bool       found_fold1              = false;
        bool       found_fold2              = false;
        bool       found_punch              = false;
        bool       found_first_footer_line  = false;
        bool       found_second_footer_line = false;
        const auto default_layout           = din_5008_form_b();
        const auto default_typo             = default_typography();
        const float default_body_width_mm = default_layout.page_width_mm
            - default_layout.margin_left_mm
            - default_layout.margin_right_mm;
        const auto first_footer_metrics = br.measurement->measure_text(
            fx.profile.footer_lines[0],
            Font_id::SANS,
            default_typo.footer_text_size_pt,
            default_typo.footer_text_size_pt,
            default_body_width_mm,
            true);
        const auto second_footer_metrics = br.measurement->measure_text(
            fx.profile.footer_lines[1],
            Font_id::SANS,
            default_typo.footer_text_size_pt,
            default_typo.footer_text_size_pt,
            default_body_width_mm,
            true);
        const float first_footer_height_mm  = pt_to_mm(first_footer_metrics.height_pt);
        const float second_footer_height_mm = pt_to_mm(second_footer_metrics.height_pt);
        const float expected_footer_y = default_layout.page_height_mm
            - default_layout.footer_margin_mm
            - (first_footer_height_mm + 1.0f + second_footer_height_mm);
        for (const auto& element : doc.pages[0].elements) {
            if (const auto* text = std::get_if<Text_block>(&element)) {
                if (text->text == "Musterstr. 6\n12345 Musterstadt\n\nkontakt@muster-ag.de") {
                    found_sender = true;
                    if (!nearly_equal(text->x_mm, 125.0f) || !nearly_equal(text->size_pt, 10.0f)) {
                        std::fprintf(stderr, "FAIL: commercial sender block layout is incorrect: x=%.2f size=%.2f\n",
                            text->x_mm, text->size_pt);
                        return 1;
                    }
                }

                if (text->text == "Muster AG \xE2\x80\xA2 Musterstr. 6 \xE2\x80\xA2 12345 Musterstadt") {
                    found_return_line = true;
                    if (!nearly_equal(text->x_mm, 25.0f) || !nearly_equal(text->width_mm, 80.0f)) {
                        std::fprintf(stderr, "FAIL: return-address line is not in the "
                            "DIN address text area: x=%.2f width=%.2f\n",
                            text->x_mm, text->width_mm);
                        return 1;
                    }
                }

                if (text->text == "Firma Beispiel GmbH\n54321 Beispielstadt") {
                    found_recipient = true;
                    if (!nearly_equal(text->x_mm,     25.0f) ||
                        !nearly_equal(text->y_mm,     63.5f) ||
                        !nearly_equal(text->width_mm, 80.0f))
                    {
                        std::fprintf(stderr, "FAIL: recipient block is not in the DIN "
                            "address text area: x=%.2f y=%.2f width=%.2f\n",
                            text->x_mm, text->y_mm, text->width_mm);
                        return 1;
                    }
                }

                if (text->text == "Kommerzieller Brief") {
                    found_subject = true;
                    if (!nearly_equal(text->x_mm, 25.0f) || !nearly_equal(text->y_mm, 104.0f)) {
                        std::fprintf(stderr, "FAIL: subject line is not at the DIN content start: x=%.2f y=%.2f\n",
                            text->x_mm, text->y_mm);
                        return 1;
                    }
                }

                if (text->text == fx.profile.footer_lines[0]) {
                    found_first_footer_line = true;
                    if (!nearly_equal(text->x_mm, default_layout.margin_left_mm) ||
                        !nearly_equal(text->y_mm, expected_footer_y)             ||
                        !nearly_equal(text->width_mm, default_body_width_mm)     ||
                        !text->wrap)
                    {
                        std::fprintf(
                            stderr,
                            "FAIL: first commercial footer line is not within the A4 page margins: "
                            "x=%.2f y=%.2f width=%.2f\n",
                            text->x_mm,
                            text->y_mm,
                            text->width_mm);
                        return 1;
                    }
                }

                if (text->text == fx.profile.footer_lines[1]) {
                    found_second_footer_line = true;
                    if (!text->wrap) {
                        std::fprintf(
                            stderr,
                            "FAIL: second commercial footer line is not wrapped to the A4 content width\n");
                        return 1;
                    }
                    const float footer_bottom = text->y_mm + second_footer_height_mm;
                    const float expected_footer_bottom = default_layout.page_height_mm
                        - default_layout.footer_margin_mm;
                    if (!nearly_equal(footer_bottom, expected_footer_bottom)) {
                        std::fprintf(
                            stderr,
                            "FAIL: commercial footer bottom margin is incorrect: "
                            "bottom=%.2f expected=%.2f\n",
                            footer_bottom,
                            expected_footer_bottom);
                        return 1;
                    }
                }
            }

            if (const auto* line = std::get_if<line_segment_t>(&element)) {
                if (nearly_equal(line->y1_mm, 62.3f) && nearly_equal(line->y2_mm, 62.3f)) {
                    found_return_underline = true;
                    if (!(line->x2_mm > 25.0f && line->x2_mm < 110.0f)) {
                        std::fprintf(stderr, "FAIL: return-address underline length is incorrect: x2=%.2f\n",
                            line->x2_mm);
                        return 1;
                    }
                }

                if (nearly_equal(line->y1_mm, 105.0f) && nearly_equal(line->y2_mm, 105.0f)) {
                    found_fold1 = true;
                    if (!nearly_equal(line->x1_mm, 3.0f) || !nearly_equal(line->x2_mm, 8.0f)) {
                        std::fprintf(stderr, "FAIL: first fold mark is incorrect: x1=%.2f x2=%.2f\n",
                            line->x1_mm, line->x2_mm);
                        return 1;
                    }
                }
                if (nearly_equal(line->y1_mm, 210.0f) && nearly_equal(line->y2_mm, 210.0f)) {
                    found_fold2 = true;
                    if (!nearly_equal(line->x1_mm, 3.0f) || !nearly_equal(line->x2_mm, 8.0f)) {
                        std::fprintf(stderr, "FAIL: second fold mark is incorrect: x1=%.2f x2=%.2f\n",
                            line->x1_mm, line->x2_mm);
                        return 1;
                    }
                }
                if (nearly_equal(line->y1_mm, 148.5f) && nearly_equal(line->y2_mm, 148.5f)) {
                    found_punch = true;
                    if (!nearly_equal(line->x1_mm, 3.0f) || !nearly_equal(line->x2_mm, 11.0f)) {
                        std::fprintf(stderr, "FAIL: punch mark is incorrect: x1=%.2f x2=%.2f\n",
                            line->x1_mm, line->x2_mm);
                        return 1;
                    }
                }
            }
        }
        if (!found_sender) {
            std::fprintf(stderr, "FAIL: commercial sender block not found\n");
            return 1;
        }
        if (!found_return_line) {
            std::fprintf(stderr, "FAIL: return-address line not found\n");
            return 1;
        }
        if (!found_recipient) {
            std::fprintf(stderr, "FAIL: recipient block not found\n");
            return 1;
        }
        if (!found_return_underline) {
            std::fprintf(stderr, "FAIL: return-address underline not found\n");
            return 1;
        }
        if (!found_subject) {
            std::fprintf(stderr, "FAIL: subject line not found\n");
            return 1;
        }
        if (!found_fold1 || !found_fold2 || !found_punch) {
            std::fprintf(stderr, "FAIL: DIN fold/punch marks not found\n");
            return 1;
        }
        if (!found_first_footer_line || !found_second_footer_line) {
            std::fprintf(stderr, "FAIL: commercial footer lines not found\n");
            return 1;
        }

        std::string commercial_output = std::string(output) + ".commercial.pdf";
        auto        rr                = render_pdf(doc, commercial_output, *br.measurement);
        if (!rr.ok) {
            std::fprintf(stderr, "FAIL: render commercial: %s\n", rr.detail.c_str());
            return 1;
        }
        std::printf("[OK] Commercial PDF: %s\n", commercial_output.c_str());
    }

    // -- Test 5: empty subject does not create placeholder or extra gap --
    {
        Profile_fixture fx("briefutil_test4", k_default_profile_simple_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: empty-subject profile load: %s\n",
                fx.error.c_str());
            return 1;
        }
        fx.profile.signature_image.clear();

        Letter_input input;
        input.recipient = "Firma Beispiel GmbH\n54321 Beispielstadt";
        input.subject.clear();
        input.date = "14. M\xc3\xa4rz 2026";
        input.body = "Erste Zeile ohne Betreff";

        auto br = build_letter(fx.profile, input, qs(fx.tmp_dir));
        if (!br.error.empty()) {
            std::fprintf(stderr, "FAIL: empty-subject build_letter: %s\n", br.error.c_str());
            return 1;
        }
        auto& doc = br.doc;
        if (doc.pages.empty()) {
            std::fprintf(stderr, "FAIL: empty-subject build_letter produced no pages\n");
            return 1;
        }

        bool found_placeholder = false;
        bool found_body        = false;
        for (const auto& element : doc.pages[0].elements) {
            if (const auto* text = std::get_if<Text_block>(&element)) {
                if (text->text == "[no subject]") {
                    found_placeholder = true;
                }
            }
            // Body is now rendered as Text_span values via the layout engine
            if (const auto* span = std::get_if<Text_span>(&element)) {
                if (span->text.find("Erste") != std::string::npos) {
                    found_body = true;
                }
            }
        }
        if (found_placeholder) {
            std::fprintf(stderr, "FAIL: empty subject generated a placeholder text block\n");
            return 1;
        }
        if (!found_body) {
            std::fprintf(stderr, "FAIL: empty-subject body block not found\n");
            return 1;
        }
    }

    // -- Test 6: profile closing overrides localization; page numbers still localize --
    {
        Profile_fixture fx("briefutil_test_loc", k_default_profile_simple_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: loc profile load: %s\n", fx.error.c_str());
            return 1;
        }
        fx.profile.signature_image.clear();
        fx.profile.closing_phrase = "Warm regards,";

        Letter_input input;
        input.recipient = "Firma Beispiel GmbH\n54321 Beispielstadt";
        input.subject   = "Brief mit Localization";
        input.date      = "13. April 2026";

        // Generate a long body to force multi-page output (so the page
        // number footer is emitted).
        std::string body;
        for (int i = 0; i < 15; i++) {
            if (i > 0) {
                body += "\n\n";
            }
            body += "Paragraph " + std::to_string(i + 1) +
                " is intentionally long enough to force pagination across multiple pages so"
                " that the page number footer is emitted somewhere in the document.";
        }
        input.body = body;

        Localization custom;
        custom.closing                      = "Yours truly,";
        custom.page_number_format           = "Sheet {current}/{total}";
        custom.error_pdf_open_failed_format = "Open failed for {path}";

        auto open_failed = format_pdf_open_failed(
            custom.error_pdf_open_failed_format,
            "C:/tmp/out.pdf");
        if (open_failed != "Open failed for C:/tmp/out.pdf") {
            std::fprintf(
                stderr,
                "FAIL: localized open-failure format was not expanded correctly\n");
            return 1;
        }

        auto br = build_letter(
            fx.profile,
            input,
            qs(fx.tmp_dir),
            default_theme(),
            din_5008_form_b(),
            custom);
        if (!br.error.empty()) {
            std::fprintf(stderr, "FAIL: loc build_letter: %s\n", br.error.c_str());
            return 1;
        }
        if (br.doc.pages.size() < 2) {
            std::fprintf(stderr, "FAIL: loc test expected multi-page output\n");
            return 1;
        }

        bool       found_profile_closing   = false;
        bool       found_localized_closing = false;
        bool       found_page_number       = false;
        const auto default_layout          = din_5008_form_b();
        const auto default_typo            = default_typography();
        const float expected_page_number_y = default_layout.page_height_mm
            - default_layout.footer_margin_mm
            - pt_to_mm(default_typo.footer_size_pt);
        for (const auto& page : br.doc.pages) {
            for (const auto& element : page.elements) {
                if (const auto* text = std::get_if<Text_block>(&element)) {
                    if (text->text == "Warm regards,") { found_profile_closing   = true; }
                    if (text->text == "Yours truly,")  { found_localized_closing = true; }
                    if (text->text.find("Sheet ") == 0 &&
                        text->text.find("/")      != std::string::npos)
                    {
                        found_page_number = true;
                        if (!nearly_equal(text->y_mm, expected_page_number_y)) {
                            std::fprintf(
                                stderr,
                                "FAIL: page number footer does not respect the A4 bottom margin: "
                                "y=%.2f expected=%.2f\n",
                                text->y_mm,
                                expected_page_number_y);
                            return 1;
                        }
                    }
                }
            }
        }
        if (!found_profile_closing) {
            std::fprintf(stderr,
                "FAIL: profile closing 'Warm regards,' not found in document\n");
            return 1;
        }
        if (found_localized_closing) {
            std::fprintf(stderr,
                "FAIL: localization closing should not override profile closing\n");
            return 1;
        }
        if (!found_page_number) {
            std::fprintf(stderr,
                "FAIL: localized page number 'Sheet X/Y' not found in document\n");
            return 1;
        }
        std::printf("[OK] Profile closing override + localized page number applied\n");

    }

    // -- Test 7: image-bearing corpus renders --
    {
        Profile_fixture fx("briefutil_test_parity", k_default_profile_simple_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: parity profile load: %s\n", fx.error.c_str());
            return 1;
        }

        Letter_input input;
        input.recipient = "Firma Beispiel GmbH\nHerrn Erich Beispiel\n"
                          "Beispielweg 42\n54321 Beispielstadt";
        input.subject   = "Image rendering check";
        input.date      = "14. M\xc3\xa4rz 2026";
        input.body =
            "Erster Absatz f\xc3\xbcr den Bildtest.\n\n"
            "Zweiter Absatz mit mehr Text, damit die Layout- und "
            "Umbruchlogik zusammen mit einer Signaturgrafik ausgef\xc3\xbchrt wird.";

        const QString signature_path = fx.tmp_dir + "/mustermann_signature.png";
        if (!write_test_signature_png(signature_path)) {
            std::fprintf(stderr, "FAIL: could not write image test signature PNG\n");
            return 1;
        }

        auto br = build_letter(fx.profile, input, qs(fx.tmp_dir));
        if (!br.error.empty()) {
            std::fprintf(stderr, "FAIL: image build_letter: %s\n", br.error.c_str());
            return 1;
        }

        auto rr = render_pdf(
            br.doc,
            std::string(output) + ".image.pdf",
            *br.measurement,
            default_localization());
        if (!rr.ok) {
            std::fprintf(
                stderr,
                "FAIL: image render: %s (%s)\n",
                rr.message.c_str(),
                rr.detail.c_str());
            return 1;
        }
        std::printf("[OK] Image-bearing corpus rendered\n");
    }

    // -- Test 7: ordered list markers saturate when start number overflowed --
    {
        Profile_fixture fx("briefutil_test_overflow_list", k_default_profile_simple_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: overflow-list profile load: %s\n",
                fx.error.c_str());
            return 1;
        }
        fx.profile.signature_image.clear();
        fx.profile.closing_phrase = "Warm regards,";

        Letter_input input;
        input.recipient = "Firma Beispiel GmbH\n54321 Beispielstadt";
        input.subject   = "Ordered list overflow";
        input.date      = "14. M\xc3\xa4rz 2026";
        input.body =
            "999999999999999999999999999999. First\n"
            "1000000000000000000000000000000. Second";

        auto br = build_letter(fx.profile, input, qs(fx.tmp_dir));
        if (!br.error.empty()) {
            std::fprintf(stderr, "FAIL: overflow-list build_letter: %s\n", br.error.c_str());
            return 1;
        }

        int saturated_marker_count = 0;
        for (const auto& page : br.doc.pages) {
            for (const auto& element : page.elements) {
                if (const auto* span = std::get_if<Text_span>(&element)) {
                    if (span->text == std::to_string(INT_MAX) + ".") {
                        saturated_marker_count++;
                    }
                }
            }
        }

        if (saturated_marker_count != 2) {
            std::fprintf(stderr,
                "FAIL: expected 2 saturated ordered-list markers, got %d\n",
                saturated_marker_count);
            return 1;
        }
        std::printf("[OK] Ordered-list markers saturate safely after parser clamp\n");
    }

    // -- Test 8: profile save/load preserves blank sender and footer lines --
    {
        QString tmp_path = QDir::tempPath() + "/briefutil_test_profile_blank_lines.json";

        Sender_profile profile;
        profile.id                  = "Blank line profile";
        profile.style               = Profile_style::COMMERCIAL;
        profile.sender_lines        = { "Line 1", "", "Line 3" };
        profile.email               = "blank.lines@example.org";
        profile.language            = "de";
        profile.return_address_line = "Line 1";
        profile.closing_phrase      = "Kind regards,";
        profile.signer_name         = "Signer";
        profile.footer_lines        = { "Footer 1", "", "Footer 3" };
        profile.signer_title        = "Role";

        std::string save_error;
        if (!save_sender_profile(profile, qs(tmp_path), &save_error)) {
            std::fprintf(stderr, "FAIL: blank-line profile save: %s\n", save_error.c_str());
            return 1;
        }

        auto loaded = load_sender_profile(qs(tmp_path));
        QFile::remove(tmp_path);

        if (!loaded.ok) {
            std::fprintf(stderr, "FAIL: blank-line profile load: %s\n", loaded.error.c_str());
            return 1;
        }
        if (loaded.profile.sender_lines != profile.sender_lines) {
            std::fprintf(stderr, "FAIL: sender_lines blank-line round-trip changed\n");
            return 1;
        }
        if (loaded.profile.closing_phrase != profile.closing_phrase) {
            std::fprintf(stderr, "FAIL: closing_phrase round-trip changed\n");
            return 1;
        }
        if (loaded.profile.language != profile.language) {
            std::fprintf(stderr, "FAIL: language round-trip changed\n");
            return 1;
        }
        if (loaded.profile.footer_lines != profile.footer_lines) {
            std::fprintf(stderr, "FAIL: footer_lines blank-line round-trip changed\n");
            return 1;
        }
        std::printf("[OK] Profile save/load preserves blank sender/footer lines\n");
    }

    // -- Test 9: profile save/load preserves empty sender/footer vectors --
    {
        QString tmp_path = QDir::tempPath() + "/briefutil_test_profile_empty_lines.json";

        Sender_profile profile;
        profile.id                  = "Empty line profile";
        profile.style               = Profile_style::COMMERCIAL;
        profile.email               = "empty.lines@example.org";
        profile.return_address_line = "Return line";
        profile.signer_name         = "Signer";
        profile.signer_title        = "Role";

        std::string save_error;
        if (!save_sender_profile(profile, qs(tmp_path), &save_error)) {
            std::fprintf(stderr, "FAIL: empty-line profile save: %s\n", save_error.c_str());
            return 1;
        }

        auto loaded = load_sender_profile(qs(tmp_path));
        QFile::remove(tmp_path);

        if (!loaded.ok) {
            std::fprintf(stderr, "FAIL: empty-line profile load: %s\n", loaded.error.c_str());
            return 1;
        }
        if (!loaded.profile.sender_lines.empty()) {
            std::fprintf(stderr, "FAIL: sender_lines empty-vector round-trip changed\n");
            return 1;
        }
        if (!loaded.profile.footer_lines.empty()) {
            std::fprintf(stderr, "FAIL: footer_lines empty-vector round-trip changed\n");
            return 1;
        }
        std::printf("[OK] Profile save/load preserves empty sender/footer vectors\n");
    }

    // -- Test 10: section font scaling affects header, body, and footer text --
    {
        Sender_profile profile;
        profile.id                  = "Scale Test";
        profile.style               = Profile_style::COMMERCIAL;
        profile.sender_lines        = { "Scaled Header" };
        profile.return_address_line = "Scaled Header";
        profile.signer_name         = "Scaled Signer";
        profile.footer_lines        = { "Scaled Footer" };

        Letter_input input;
        input.recipient = "Scaled Recipient";
        input.subject   = "Scaled Subject";
        input.date      = "27. April 2026";
        input.body      = "Scaled body.";

        auto theme = default_theme();
        theme.typo.header_scale = 1.2f;
        theme.typo.body_scale   = 1.3f;
        theme.typo.footer_scale = 1.4f;

        auto br = build_letter(
            profile,
            input,
            "",
            theme,
            din_5008_form_b(),
            default_localization());
        if (!br.error.empty()) {
            std::fprintf(stderr, "FAIL: scaled typography build_letter: %s\n", br.error.c_str());
            return 1;
        }

        const auto default_typo        = default_typography();
        bool       found_scaled_header = false;
        bool       found_scaled_body   = false;
        bool       found_scaled_footer = false;
        for (const auto& element : br.doc.pages[0].elements) {
            if (const auto* text = std::get_if<Text_block>(&element)) {
                if (text->text == "Scaled Header" &&
                    nearly_equal(text->size_pt, default_typo.sender_size_pt * 1.2f))
                {
                    found_scaled_header = true;
                }
                if (text->text == "Scaled Subject" &&
                    nearly_equal(text->size_pt, default_typo.body_size_pt * 1.3f))
                {
                    found_scaled_body = true;
                }
                if (text->text == "Scaled Footer" &&
                    nearly_equal(text->size_pt, default_typo.footer_text_size_pt * 1.4f))
                {
                    found_scaled_footer = true;
                }
            }
        }

        if (!found_scaled_header || !found_scaled_body || !found_scaled_footer) {
            std::fprintf(stderr, "FAIL: section font scaling was not applied\n");
            return 1;
        }
        std::printf("[OK] Section font scaling applied\n");
    }

    // -- Test 11: table columns expand only when that reduces table height --
    {
        Profile_fixture fx("briefutil_test_table_height", k_default_profile_simple_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: table-height profile load: %s\n",
                fx.error.c_str());
            return 1;
        }
        fx.profile.signature_image.clear();

        Letter_input input;
        input.recipient = "Gartenbau Lindenhof\nFrau Clara Berg\n"
                          "Amselweg 17\n50672 K\xc3\xb6ln";
        input.subject   = "Bepflanzung der K\xc3\xbc" "bel";
        input.date      = "3. Mai 2026";
        input.body =
            "## \xc3\x9c" "bersicht\n\n"
            "| Pflanze | Standort | Hinweis |\n"
            "| --- | --- | --- |\n"
            "| Lavendel | sonnig | sparsam gie\xc3\x9f" "en |\n"
            "| Thymian | sonnig | gut f\xc3\xbc" "r Insekten |\n"
            "| Erdbeeren | halbschattig | regelm\xc3\xa4\xc3\x9f" "ig ernten |\n";

        auto br = build_letter(fx.profile, input, qs(fx.tmp_dir));
        if (!br.error.empty()) {
            std::fprintf(stderr, "FAIL: table-height build_letter: %s\n", br.error.c_str());
            return 1;
        }

        bool found_unwrapped_hint = false;
        bool found_split_regular  = false;
        bool found_split_harvest  = false;
        bool found_header_fill    = false;
        for (const auto& page : br.doc.pages) {
            for (const auto& element : page.elements) {
                if (const auto* span = std::get_if<Text_span>(&element)) {
                    if (span->text == "regelm\xc3\xa4\xc3\x9f" "ig ernten") { found_unwrapped_hint = true; }
                    if (span->text == "regelm\xc3\xa4\xc3\x9f" "ig")        { found_split_regular  = true; }
                    if (span->text == "ernten")                             { found_split_harvest  = true; }
                }
                if (const auto* rect = std::get_if<filled_rect_t>(&element)) {
                    const bool mildly_grey =
                        nearly_equal(rect->color.r, 0.94f)
                        && nearly_equal(rect->color.g, 0.94f)
                        && nearly_equal(rect->color.b, 0.94f);
                    if (mildly_grey && rect->width_mm > 0.0f && rect->height_mm > 0.0f) {
                        found_header_fill = true;
                    }
                }
            }
        }

        if (!found_unwrapped_hint || (found_split_regular && found_split_harvest)) {
            std::fprintf(
                stderr,
                "FAIL: table column expansion did not keep 'regelmaessig ernten' on one line\n");
            return 1;
        }
        if (!found_header_fill) {
            std::fprintf(stderr, "FAIL: table header fill was not emitted\n");
            return 1;
        }
        std::printf("[OK] Table columns expand when doing so reduces row height\n");
    }

    // -- Test 12: table column rebalancing
    //
    // When one column carries long text that has to wrap regardless, the
    // column should be tightened to the width of its widest wrapped line so
    // the freed slack lets narrow columns reach their no-wrap preferred
    // width. Without this, narrow columns get squeezed below their natural
    // width and wrap unnecessarily (e.g. "Betrag (\xe2\x82\xac)" splitting
    // into two lines).
    {
        Profile_fixture fx("briefutil_test_table_rebalance", k_default_profile_simple_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: rebalance profile load: %s\n", fx.error.c_str());
            return 1;
        }
        fx.profile.signature_image.clear();

        Letter_input input;
        input.recipient = "Beispiel GmbH\nMusterstra\xc3\x9f""e 1\n12345 Beispielstadt";
        input.subject   = "Angebot";
        input.date      = "13. Mai 2026";
        input.body =
            "| Leistung | Grundlage | Betrag (\xe2\x82\xac) |\n"
            "| --- | --- | --- |\n"
            "| Bodenfliesen verlegen in K\xc3\xbc" "che, zwei Badezimmern und Balkon"
                " | ca. 23,63 m\xc2\xb2 | 1.654,00 |\n"
            "| Korkboden in K\xc3\xbc" "che und Badezimmern entfernen, Untergrund"
                " reinigen und vorbereiten | pauschal | 500,00 |\n";

        auto br = build_letter(fx.profile, input, qs(fx.tmp_dir));
        if (!br.error.empty()) {
            std::fprintf(stderr, "FAIL: rebalance build_letter: %s\n", br.error.c_str());
            return 1;
        }

        bool found_header_full      = false;
        bool found_header_split_lhs = false;
        bool found_header_split_rhs = false;
        bool found_amount_full      = false;
        bool found_amount_split_lhs = false;
        bool found_amount_split_rhs = false;
        for (const auto& page : br.doc.pages) {
            for (const auto& element : page.elements) {
                if (const auto* span = std::get_if<Text_span>(&element)) {
                    if (span->text == "Betrag (\xe2\x82\xac)") { found_header_full      = true; }
                    if (span->text == "Betrag")                { found_header_split_lhs = true; }
                    if (span->text == "(\xe2\x82\xac)")        { found_header_split_rhs = true; }
                    if (span->text == "ca. 23,63 m\xc2\xb2")   { found_amount_full      = true; }
                    if (span->text == "ca. 23,63")             { found_amount_split_lhs = true; }
                    if (span->text == "m\xc2\xb2")             { found_amount_split_rhs = true; }
                }
            }
        }

        const bool header_wrapped = found_header_split_lhs && found_header_split_rhs;
        const bool amount_wrapped = found_amount_split_lhs && found_amount_split_rhs;
        if (!found_header_full || header_wrapped) {
            std::fprintf(
                stderr,
                "FAIL: 'Betrag (\xe2\x82\xac)' header should fit on one line after rebalance\n");
            return 1;
        }
        if (!found_amount_full || amount_wrapped) {
            std::fprintf(
                stderr,
                "FAIL: 'ca. 23,63 m\xc2\xb2' should fit on one line after rebalance\n");
            return 1;
        }
        std::printf("[OK] Table column widths rebalance to avoid narrow-column wraps\n");
    }

    // -- Test 13: a table row taller than a page is rejected, not overflowed --
    {
        Profile_fixture fx("briefutil_test_table_overflow", k_default_profile_simple_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: table-overflow profile load: %s\n", fx.error.c_str());
            return 1;
        }
        fx.profile.signature_image.clear();

        std::string cell;
        for (int i = 0; i < 1500; ++i) {
            cell += "lorem ";
        }

        Letter_input input;
        input.recipient = "Beispiel GmbH\nMusterstr. 1\n12345 Beispielstadt";
        input.subject   = "Tall table";
        input.body      = "| Col |\n| --- |\n| " + cell + " |\n";

        auto br = build_letter(fx.profile, input, qs(fx.tmp_dir));
        if (br.error.empty()) {
            std::fprintf(stderr, "FAIL: a table row taller than a page should fail, not overflow\n");
            return 1;
        }
        std::printf("[OK] Over-tall table row is rejected instead of overflowing\n");
    }

    // -- Test 14: a closing block taller than a page is rejected --
    {
        Profile_fixture fx("briefutil_test_closing_overflow", k_default_profile_simple_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: closing-overflow profile load: %s\n", fx.error.c_str());
            return 1;
        }
        // Keep signature_image non-empty (the PNG is absent, so the renderer's
        // fallback aspect applies) and make the signature absurdly wide so the
        // closing block cannot fit on any page.
        letter_layout_spec_t layout = din_5008_form_b();
        layout.sig_width_mm = 1000.0f;

        Letter_input input;
        input.recipient = "Beispiel GmbH\nMusterstr. 1\n12345 Beispielstadt";
        input.subject   = "Overflow";
        input.body      = "Short body.";

        auto br = build_letter(fx.profile, input, qs(fx.tmp_dir), default_theme(), layout);
        if (br.error.empty()) {
            std::fprintf(stderr,
                "FAIL: an oversized closing block should fail to fit, not overflow\n");
            return 1;
        }
        std::printf("[OK] Over-tall closing block is rejected instead of overflowing\n");
    }

    // -- Test 15: inline styles preserve source whitespace --
    //
    // mark2haru keeps boundary whitespace in run text, so the layout must not
    // synthesize a space between styled runs that were adjacent in the source.
    {
        Profile_fixture fx("briefutil_test_inline_ws", k_default_profile_simple_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: inline-whitespace profile load: %s\n", fx.error.c_str());
            return 1;
        }
        fx.profile.signature_image.clear();

        auto collect_body_spans = [&](const char* body) -> std::vector<std::string> {
            Letter_input input;
            input.recipient = "Beispiel GmbH\nMusterstr. 1\n12345 Beispielstadt";
            input.subject   = "Whitespace";
            input.body      = body;
            auto br = build_letter(fx.profile, input, qs(fx.tmp_dir));
            if (!br.error.empty()) {
                return { std::string("<error: ") + br.error + ">" };
            }
            std::vector<std::string> spans;
            for (const auto& page : br.doc.pages) {
                for (const auto& element : page.elements) {
                    if (const auto* span = std::get_if<Text_span>(&element)) {
                        spans.push_back(span->text);
                    }
                }
            }
            return spans;
        };

        auto join_spans = [](const std::vector<std::string>& spans) -> std::string {
            std::string out;
            for (const auto& span : spans) {
                out += span;
            }
            return out;
        };

        auto body_spans = [&](const char* body) -> std::string {
            return join_spans(collect_body_spans(body));
        };

        struct whitespace_case_t
        {
            const char* body;
            const char* expected;
        };
        const whitespace_case_t cases[] = {
            { "foo**bar**",       "foobar"      },
            { "**bold**_italic_", "bolditalic"  },
            { "hello,**world**",  "hello,world" },
            { "foo **bar**",      "foo bar"     },
            { "foo `bar`baz",     "foo barbaz"  },
            { "**a** **b**",      "a b"         },
            { "foo\tbar",         "foo bar"     },
            { "foo\t\tbar",       "foo bar"     },
            { "foo\t**bar**",     "foo bar"     },
            { "**foo**\tbar",     "foo bar"     },
            { "foo\t**bar**baz",  "foo barbaz"  },
        };
        for (const auto& c : cases) {
            const std::string actual = body_spans(c.body);
            if (actual != c.expected) {
                std::fprintf(stderr,
                    "FAIL: body '%s' rendered as '%s', expected '%s'\n",
                    c.body, actual.c_str(), c.expected);
                return 1;
            }
        }

        std::vector<std::string> wrap_tokens;
        std::string              wrap_body;
        for (int i = 0; i < 32; ++i) {
            const std::string token = "tabwrap" + std::to_string(1000 + i);
            if (!wrap_body.empty()) {
                wrap_body += '\t';
            }
            wrap_body += token;
            wrap_tokens.push_back(token);
        }

        const std::vector<std::string> wrapped_spans =
            collect_body_spans(wrap_body.c_str());
        if (wrapped_spans.size() < 2) {
            std::fprintf(stderr,
                "FAIL: tab-separated text did not wrap across body spans\n");
            return 1;
        }

        size_t next_token = 0;
        for (const auto& span : wrapped_spans) {
            if (span.find('\t') != std::string::npos) {
                std::fprintf(stderr, "FAIL: rendered span still contains a tab\n");
                return 1;
            }

            std::string expected_span;
            while (next_token < wrap_tokens.size()) {
                if (!expected_span.empty()) {
                    expected_span += ' ';
                }
                expected_span += wrap_tokens[next_token];
                ++next_token;
                if (expected_span == span) {
                    break;
                }
                if (expected_span.size() > span.size()) {
                    break;
                }
            }

            if (expected_span != span) {
                std::fprintf(stderr,
                    "FAIL: tab wrap split inside a token: '%s'\n",
                    span.c_str());
                return 1;
            }
        }
        if (next_token != wrap_tokens.size()) {
            std::fprintf(stderr, "FAIL: tab wrap did not render every token\n");
            return 1;
        }

        std::printf("[OK] Inline styles preserve source whitespace and normalize tabs\n");
    }

    // -- Test 16: an over-long token wraps instead of overrunning the margin --
    {
        Profile_fixture fx("briefutil_test_long_token", k_default_profile_simple_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: long-token profile load: %s\n", fx.error.c_str());
            return 1;
        }
        fx.profile.signature_image.clear();

        const std::string url = "https://example.com/" + std::string(180, 'a');

        Letter_input input;
        input.recipient = "Beispiel GmbH\nMusterstr. 1\n12345 Beispielstadt";
        input.subject   = "Long URL";
        input.body      = url;

        auto br = build_letter(fx.profile, input, qs(fx.tmp_dir));
        if (!br.error.empty()) {
            std::fprintf(stderr, "FAIL: long-token body should build, got error: %s\n",
                br.error.c_str());
            return 1;
        }

        int         span_count = 0;
        std::string joined;
        for (const auto& page : br.doc.pages) {
            for (const auto& element : page.elements) {
                if (const auto* span = std::get_if<Text_span>(&element)) {
                    joined += span->text;
                    ++span_count;
                }
            }
        }
        if (joined != url) {
            std::fprintf(stderr,
                "FAIL: long token lost characters on wrap (got %zu chars, expected %zu)\n",
                joined.size(), url.size());
            return 1;
        }
        if (span_count < 2) {
            std::fprintf(stderr, "FAIL: long token did not wrap (%d span)\n", span_count);
            return 1;
        }
        std::printf("[OK] Over-long token wraps across lines without overrunning the margin\n");
    }

    // -- Test 17: print layout wraps subjects, keeps leading, and deduplicates signer lines --
    {
        Profile_fixture fx("briefutil_test_print_layout", k_default_profile_simple_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: print-layout profile load: %s\n", fx.error.c_str());
            return 1;
        }
        fx.profile.signature_image.clear();
        fx.profile.signer_name  = "Same Signer";
        fx.profile.signer_title = "Same Signer";

        Letter_input input;
        input.recipient = "Recipient";
        input.subject   = "Antrag auf erg\xc3\xa4nzende F\xc3\xb6rderung und Betreuung f\xc3\xbcr ein Kind mit einem sehr langen Betreff";
        input.body      = "First body line.";

        auto theme = default_theme();
        theme.typo.body_size_pt = 9.0f;
        theme.typo.body_lead_pt = 9.0f;
        auto br = build_letter(fx.profile, input, qs(fx.tmp_dir), theme);
        if (!br.error.empty()) {
            std::fprintf(stderr, "FAIL: print-layout build: %s\n", br.error.c_str());
            return 1;
        }

        const Text_block* subject = nullptr;
        const Text_span*  body    = nullptr;
        int               signer_count = 0;
        for (const auto& element : br.doc.pages[0].elements) {
            if (const auto* text = std::get_if<Text_block>(&element)) {
                if (text->text == input.subject) {
                    subject = text;
                }
                if (text->text == fx.profile.signer_name) {
                    signer_count++;
                }
            }
            if (const auto* span = std::get_if<Text_span>(&element);
                span && span->text == input.body)
            {
                body = span;
            }
        }

        const auto typo = scaled_typography(theme.typo);
        const auto subject_lines = br.measurement->wrap_text(
            input.subject,
            Font_id::SANS_BOLD,
            typo.body_size_pt,
            din_5008_form_b().page_width_mm - din_5008_form_b().margin_left_mm
                - din_5008_form_b().margin_right_mm);
        if (!subject || !subject->wrap || subject_lines.size() < 2 || !body ||
            body->y_mm < subject->y_mm + din_5008_form_b().subject_to_body_mm
                + pt_to_mm((subject_lines.size() - 1) * typo.body_lead_pt) ||
            typo.body_lead_pt < typo.body_size_pt * 1.2f || signer_count != 1)
        {
            std::fprintf(stderr, "FAIL: print layout regression\n");
            return 1;
        }
        std::printf("[OK] Print layout wraps subjects, keeps leading, and deduplicates signer lines\n");
    }

    // -- Test: an unbreakable subject token is broken, not run off the page --
    {
        Profile_fixture fx("briefutil_test_long_subject", k_default_profile_simple_json);
        if (!fx.ok) {
            std::fprintf(stderr, "FAIL: long-subject profile load: %s\n", fx.error.c_str());
            return 1;
        }

        const auto  layout         = din_5008_form_b();
        const float body_width_mm  = layout.page_width_mm
            - layout.margin_left_mm - layout.margin_right_mm;
        const std::string long_token(400, 'W');

        Letter_input input;
        input.recipient = "Beispiel GmbH\nMusterstr. 1\n12345 Beispielstadt";
        input.subject   = long_token;
        input.body      = "Body.";
        auto br = build_letter(fx.profile, input, qs(fx.tmp_dir));
        if (!br.error.empty()) {
            std::fprintf(stderr, "FAIL: long-subject build: %s\n", br.error.c_str());
            return 1;
        }

        const auto  typo    = scaled_typography(default_theme().typo);
        const auto  lines   = br.measurement->wrap_text(
            long_token, Font_id::SANS_BOLD, typo.body_size_pt, body_width_mm);
        if (lines.size() < 2) {
            std::fprintf(stderr,
                "FAIL: a subject token wider than the column must be broken, got %zu line(s)\n",
                lines.size());
            return 1;
        }
        for (const auto& line : lines) {
            const auto metrics = br.measurement->measure_text(
                line, Font_id::SANS_BOLD, typo.body_size_pt, 0, body_width_mm, false);
            if (pt_to_mm(metrics.width_pt) > body_width_mm) {
                std::fprintf(stderr,
                    "FAIL: subject line is %.1fmm wide in a %.1fmm column\n",
                    pt_to_mm(metrics.width_pt), body_width_mm);
                return 1;
            }
        }
        std::printf("[OK] Over-long subject token is broken to fit the column\n");
    }

    std::printf("\nAll letter-builder tests passed.\n");
    return 0;
}
