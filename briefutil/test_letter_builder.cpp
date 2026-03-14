// ============================================================================
// Full pipeline test — profile loading + letter builder + renderer
// ============================================================================

#include "sender_profile.h"
#include "letter_builder.h"
#include "pdf_renderer_haru.h"
#include "default_profiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <variant>


static std::string qs(const QString& s) { return s.toStdString(); }

static bool nearly_equal(float a, float b) { return std::fabs(a - b) < 0.01f; }

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    const char* output = argc > 1 ? argv[1] : "test_letter_output.pdf";

    // -- Test 1: parse embedded simple profile JSON --
    {
        QString tmp_path = QDir::tempPath() + "/briefutil_test_simple.json";
        QFile f(tmp_path);
        if (!f.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "FAIL: cannot write temp profile: %s\n",
                         qs(tmp_path).c_str());
            return 1;
        }
        f.write(k_default_profile_simple_json);
        f.close();

        auto result = load_sender_profile(qs(tmp_path));
        QFile::remove(tmp_path);

        if (!result.ok) {
            std::fprintf(stderr, "FAIL: profile load: %s\n", result.error.c_str());
            return 1;
        }

        auto& p = result.profile;
        if (p.id != "Max Mustermann") {
            std::fprintf(stderr, "FAIL: expected id 'Max Mustermann', got '%s'\n",
                         p.id.c_str());
            return 1;
        }
        if (p.style != Profile_style::simple) {
            std::fprintf(stderr, "FAIL: expected simple style\n");
            return 1;
        }
        if (p.sender_lines.size() != 3) {
            std::fprintf(stderr, "FAIL: expected 3 sender_lines, got %zu\n",
                         p.sender_lines.size());
            return 1;
        }
        if (p.return_address_line
            != "Max Mustermann \xE2\x80\xA2 Musterstr. 6 \xE2\x80\xA2 12345 Musterstadt") {
            std::fprintf(stderr, "FAIL: return_address_line is incorrectly decoded: '%s'\n",
                         p.return_address_line.c_str());
            return 1;
        }
        std::printf("[OK] Simple profile loaded correctly\n");
    }

    // -- Test 2: build and render a simple letter --
    {
        // Write profile to temp
        QString tmp_dir = QDir::tempPath() + "/briefutil_test";
        QDir().mkpath(tmp_dir);

        QString profile_path = tmp_dir + "/test_profile.json";
        QFile f(profile_path);
        if (!f.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "FAIL: cannot write test profile: %s\n",
                         qs(profile_path).c_str());
            return 1;
        }
        f.write(k_default_profile_simple_json);
        f.close();

        auto lr = load_sender_profile(qs(profile_path));
        if (!lr.ok) {
            std::fprintf(stderr, "FAIL: profile load for letter test: %s\n",
                         lr.error.c_str());
            return 1;
        }

        // Clear signature_image since the test dir has no PNG
        lr.profile.signature_image.clear();

        Letter_input input;
        input.recipient = "Firma Beispiel GmbH\nHerrn Erich Beispiel\n"
                          "Beispielweg 42\n54321 Beispielstadt";
        // All text must be UTF-8 — the renderer converts to Latin-1 for libHaru.
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

        auto br = build_letter(lr.profile, input, qs(tmp_dir));
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

        auto rr = render_pdf(doc, output);
        if (!rr.ok) {
            std::fprintf(stderr, "FAIL: render_pdf: %s (%s)\n",
                         rr.message.c_str(), rr.detail.c_str());
            return 1;
        }
        std::printf("[OK] PDF rendered to: %s\n", output);

        // Verify %PDF- header
        FILE* pf = std::fopen(output, "rb");
        if (!pf) {
            std::fprintf(stderr, "FAIL: cannot open output\n");
            return 1;
        }
        char hdr[6] = {};
        std::fread(hdr, 1, 5, pf);
        std::fclose(pf);
        if (std::strncmp(hdr, "%PDF-", 5) != 0) {
            std::fprintf(stderr, "FAIL: not a valid PDF\n");
            return 1;
        }
        std::printf("[OK] Valid PDF file\n");

        // Cleanup temp
        QFile::remove(profile_path);
        QDir().rmdir(tmp_dir);
    }

    // -- Test 3: multi-page letter (long body) --
    {
        QString tmp_dir = QDir::tempPath() + "/briefutil_test2";
        QDir().mkpath(tmp_dir);

        QString profile_path = tmp_dir + "/test_profile.json";
        QFile f(profile_path);
        if (!f.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "FAIL: cannot write multi-page test profile: %s\n",
                         qs(profile_path).c_str());
            return 1;
        }
        f.write(k_default_profile_simple_json);
        f.close();

        auto lr = load_sender_profile(qs(profile_path));
        lr.profile.signature_image.clear();

        Letter_input input;
        input.recipient = "Firma Beispiel GmbH\n54321 Beispielstadt";
        input.subject = "Langer Brief";
        input.date = "14. M\xc3\xa4rz 2026";

        // Generate a long body (UTF-8)
        std::string body;
        for (int i = 0; i < 15; i++) {
            if (i > 0) body += "\n\n";
            body += "Dies ist Absatz " + std::to_string(i + 1)
                + ". Der Text ist absichtlich lang, um die Paginierung "
                "zu testen und sicherzustellen, dass der \xc3" "\x9c" "berlauf "
                "korrekt auf Folgeseiten umgebrochen wird.";
        }
        input.body = body;

        auto br = build_letter(lr.profile, input, qs(tmp_dir));
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
        auto rr = render_pdf(doc, mp_output);
        if (!rr.ok) {
            std::fprintf(stderr, "FAIL: render multi-page: %s\n", rr.detail.c_str());
            return 1;
        }
        std::printf("[OK] Multi-page PDF: %s\n", mp_output.c_str());

        QFile::remove(profile_path);
        QDir().rmdir(tmp_dir);
    }

    // -- Test 4: commercial profile load + render --
    {
        QString tmp_dir = QDir::tempPath() + "/briefutil_test3";
        QDir().mkpath(tmp_dir);

        QString profile_path = tmp_dir + "/test_profile.json";
        QFile f(profile_path);
        if (!f.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "FAIL: cannot write commercial test profile: %s\n",
                         qs(profile_path).c_str());
            return 1;
        }
        f.write(k_default_profile_commercial_json);
        f.close();

        auto lr = load_sender_profile(qs(profile_path));
        if (!lr.ok) {
            std::fprintf(stderr, "FAIL: commercial profile load: %s\n",
                         lr.error.c_str());
            return 1;
        }
        if (lr.profile.style != Profile_style::commercial) {
            std::fprintf(stderr, "FAIL: expected commercial style\n");
            return 1;
        }
        if (lr.profile.footer_lines.size() != 2) {
            std::fprintf(stderr, "FAIL: expected 2 commercial footer lines, got %zu\n",
                         lr.profile.footer_lines.size());
            return 1;
        }
        if (lr.profile.return_address_line
            != "Muster AG \xE2\x80\xA2 Musterstr. 6 \xE2\x80\xA2 12345 Musterstadt") {
            std::fprintf(stderr, "FAIL: commercial return_address_line is incorrect: '%s'\n",
                         lr.profile.return_address_line.c_str());
            return 1;
        }

        lr.profile.signature_image.clear();

        Letter_input input;
        input.recipient = "Firma Beispiel GmbH\n54321 Beispielstadt";
        input.subject = "Kommerzieller Brief";
        input.date = "14. M\xc3\xa4rz 2026";
        input.body =
            "Sehr geehrte Damen und Herren,\n\n"
            "anbei erhalten Sie unser aktualisiertes Angebot.";

        auto br = build_letter(lr.profile, input, qs(tmp_dir));
        if (!br.error.empty()) {
            std::fprintf(stderr, "FAIL: commercial build_letter: %s\n", br.error.c_str());
            return 1;
        }
        auto& doc = br.doc;
        if (doc.pages.empty()) {
            std::fprintf(stderr, "FAIL: commercial build_letter produced no pages\n");
            return 1;
        }

        bool found_company = false;
        bool found_sender = false;
        bool found_top_rule = false;
        bool found_return_line = false;
        bool found_return_underline = false;
        bool found_recipient = false;
        bool found_subject = false;
        bool found_fold1 = false;
        bool found_fold2 = false;
        bool found_punch = false;
        for (const auto& element : doc.pages[0].elements) {
            if (const auto* text = std::get_if<Text_block>(&element)) {
                if (text->text == "Muster AG") {
                    found_company = true;
                    if (!nearly_equal(text->x_mm, 125.0f) || !nearly_equal(text->y_mm, 33.0f)) {
                        std::fprintf(stderr, "FAIL: commercial company text layout is incorrect: x=%.2f y=%.2f\n",
                                     text->x_mm, text->y_mm);
                        return 1;
                    }
                }

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
                        std::fprintf(stderr, "FAIL: return-address line is not in the DIN address text area: x=%.2f width=%.2f\n",
                                     text->x_mm, text->width_mm);
                        return 1;
                    }
                }

                if (text->text == "Firma Beispiel GmbH\n54321 Beispielstadt") {
                    found_recipient = true;
                    if (!nearly_equal(text->x_mm, 25.0f)
                        || !nearly_equal(text->y_mm, 63.5f)
                        || !nearly_equal(text->width_mm, 80.0f)) {
                        std::fprintf(stderr, "FAIL: recipient block is not in the DIN address text area: x=%.2f y=%.2f width=%.2f\n",
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
            }

            if (const auto* line = std::get_if<Line_segment>(&element)) {
                if (nearly_equal(line->y1_mm, 45.0f) && nearly_equal(line->y2_mm, 45.0f)) {
                    found_top_rule = true;
                    if (!(line->x2_mm > 125.0f && line->x2_mm < 180.0f)) {
                        std::fprintf(stderr, "FAIL: commercial top rule length is incorrect: x2=%.2f\n",
                                     line->x2_mm);
                        return 1;
                    }
                }

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
        if (!found_company) {
            std::fprintf(stderr, "FAIL: commercial company block not found\n");
            return 1;
        }
        if (!found_sender) {
            std::fprintf(stderr, "FAIL: commercial sender block not found\n");
            return 1;
        }
        if (!found_top_rule) {
            std::fprintf(stderr, "FAIL: commercial top rule not found at DIN header position\n");
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

        std::string commercial_output = std::string(output) + ".commercial.pdf";
        auto rr = render_pdf(doc, commercial_output);
        if (!rr.ok) {
            std::fprintf(stderr, "FAIL: render commercial: %s\n", rr.detail.c_str());
            return 1;
        }
        std::printf("[OK] Commercial PDF: %s\n", commercial_output.c_str());

        QFile::remove(profile_path);
        QDir().rmdir(tmp_dir);
    }

    // -- Test 5: empty subject does not create placeholder or extra gap --
    {
        QString tmp_dir = QDir::tempPath() + "/briefutil_test4";
        QDir().mkpath(tmp_dir);

        QString profile_path = tmp_dir + "/test_profile.json";
        QFile f(profile_path);
        if (!f.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "FAIL: cannot write empty-subject test profile: %s\n",
                         qs(profile_path).c_str());
            return 1;
        }
        f.write(k_default_profile_simple_json);
        f.close();

        auto lr = load_sender_profile(qs(profile_path));
        if (!lr.ok) {
            std::fprintf(stderr, "FAIL: empty-subject profile load: %s\n",
                         lr.error.c_str());
            return 1;
        }
        lr.profile.signature_image.clear();

        Letter_input input;
        input.recipient = "Firma Beispiel GmbH\n54321 Beispielstadt";
        input.subject.clear();
        input.date = "14. M\xc3\xa4rz 2026";
        input.body = "Erste Zeile ohne Betreff";

        auto br = build_letter(lr.profile, input, qs(tmp_dir));
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
        bool found_body = false;
        for (const auto& element : doc.pages[0].elements) {
            if (const auto* text = std::get_if<Text_block>(&element)) {
                if (text->text == "[no subject]") {
                    found_placeholder = true;
                }
            }
            // Body is now rendered as Text_spans via the layout engine
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

        QFile::remove(profile_path);
        QDir().rmdir(tmp_dir);
    }

    std::printf("\nAll letter-builder tests passed.\n");
    return 0;
}
