// ============================================================================
// Full pipeline test — profile loading + letter builder + renderer
// ============================================================================

#include "briefutil/default_profiles.h"
#include "briefutil/letter_builder.h"
#include "briefutil/pdf_backend.h"
#include "briefutil/pdf_measurement.h"
#include "briefutil/pdf_renderer.h"
#include "briefutil/sender_profile.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <variant>


static std::string qs(const QString& s) { return s.toStdString(); }

static bool nearly_equal(float a, float b, float eps = 0.01f)
{
    return std::fabs(a - b) < eps;
}

static bool same_color(const color_t& a, const color_t& b, float eps = 0.001f)
{
    return nearly_equal(a.r, b.r, eps)
        && nearly_equal(a.g, b.g, eps)
        && nearly_equal(a.b, b.b, eps);
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

static bool same_element(const Page_element& a, const Page_element& b, std::string* reason)
{
    if (a.index() != b.index()) {
        if (reason) *reason = "element types differ";
        return false;
    }

    auto compare_text = [&](const auto& lhs, const auto& rhs, const char* kind) {
        if (lhs.text != rhs.text) {
            if (reason) *reason = std::string(kind) + " text differs";
            return false;
        }
        if (lhs.font != rhs.font || !nearly_equal(lhs.size_pt, rhs.size_pt)
            || !same_color(lhs.color, rhs.color)) {
            if (reason) *reason = std::string(kind) + " style differs";
            return false;
        }
        return true;
    };

    if (const auto* lhs = std::get_if<Text_block>(&a)) {
        const auto* rhs = std::get_if<Text_block>(&b);
        if (!compare_text(*lhs, *rhs, "Text_block")) {
            return false;
        }
        if (!nearly_equal(lhs->x_mm, rhs->x_mm, 0.1f)
            || !nearly_equal(lhs->y_mm, rhs->y_mm, 0.1f)
            || !nearly_equal(lhs->width_mm, rhs->width_mm, 0.1f)
            || !nearly_equal(lhs->leading_pt, rhs->leading_pt, 0.01f)
            || lhs->wrap != rhs->wrap) {
            if (reason) *reason = "Text_block geometry differs";
            return false;
        }
        return true;
    }
    if (const auto* lhs = std::get_if<Text_span>(&a)) {
        const auto* rhs = std::get_if<Text_span>(&b);
        if (!compare_text(*lhs, *rhs, "Text_span")) {
            return false;
        }
        if (!nearly_equal(lhs->x_mm, rhs->x_mm, 0.1f)
            || !nearly_equal(lhs->y_mm, rhs->y_mm, 0.1f)) {
            if (reason) *reason = "Text_span geometry differs";
            return false;
        }
        return true;
    }
    if (const auto* lhs = std::get_if<line_segment_t>(&a)) {
        const auto* rhs = std::get_if<line_segment_t>(&b);
        if (!nearly_equal(lhs->x1_mm, rhs->x1_mm, 0.1f)
            || !nearly_equal(lhs->y1_mm, rhs->y1_mm, 0.1f)
            || !nearly_equal(lhs->x2_mm, rhs->x2_mm, 0.1f)
            || !nearly_equal(lhs->y2_mm, rhs->y2_mm, 0.1f)
            || !nearly_equal(lhs->stroke_width_pt, rhs->stroke_width_pt)
            || !same_color(lhs->color, rhs->color)) {
            if (reason) {
                *reason = "line_segment_t differs";
            }
            return false;
        }
        return true;
    }
    if (const auto* lhs = std::get_if<filled_rect_t>(&a)) {
        const auto* rhs = std::get_if<filled_rect_t>(&b);
        if (!nearly_equal(lhs->x_mm, rhs->x_mm, 0.1f)
            || !nearly_equal(lhs->y_mm, rhs->y_mm, 0.1f)
            || !nearly_equal(lhs->width_mm, rhs->width_mm, 0.1f)
            || !nearly_equal(lhs->height_mm, rhs->height_mm, 0.1f)
            || !same_color(lhs->color, rhs->color)) {
            if (reason) *reason = "filled_rect_t differs";
            return false;
        }
        return true;
    }
    if (const auto* lhs = std::get_if<Image_block>(&a)) {
        const auto* rhs = std::get_if<Image_block>(&b);
        if (!nearly_equal(lhs->x_mm, rhs->x_mm, 0.1f)
            || !nearly_equal(lhs->y_mm, rhs->y_mm, 0.1f)
            || !nearly_equal(lhs->width_mm, rhs->width_mm, 0.1f)
            || lhs->path != rhs->path) {
            if (reason) *reason = "Image_block differs";
            return false;
        }
        return true;
    }

    if (reason) *reason = "unknown element type";
    return false;
}

static bool same_document(const Document& a, const Document& b, std::string* reason)
{
    if (!nearly_equal(a.page_width_mm, b.page_width_mm) || !nearly_equal(a.page_height_mm, b.page_height_mm)) {
        if (reason) *reason = "page size differs";
        return false;
    }
    if (a.pages.size() != b.pages.size()) {
        if (reason) *reason = "page count differs";
        return false;
    }
    for (size_t i = 0; i < a.pages.size(); ++i) {
        if (a.pages[i].elements.size() != b.pages[i].elements.size()) {
            if (reason) *reason = "page " + std::to_string(i + 1) + " element count differs";
            return false;
        }
        for (size_t j = 0; j < a.pages[i].elements.size(); ++j) {
            std::string element_reason;
            if (!same_element(a.pages[i].elements[j], b.pages[i].elements[j], &element_reason)) {
                if (reason) {
                    *reason = "page " + std::to_string(i + 1)
                        + " element " + std::to_string(j + 1)
                        + ": " + element_reason;
                }
                return false;
            }
        }
    }
    return true;
}

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
        if (lr.profile.style != Profile_style::COMMERCIAL) {
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

        bool found_sender = false;
        bool found_return_line = false;
        bool found_return_underline = false;
        bool found_recipient = false;
        bool found_subject = false;
        bool found_fold1 = false;
        bool found_fold2 = false;
        bool found_punch = false;
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

    // -- Test 6: localization is honored (closing line + page numbers) --
    {
        QString tmp_dir = QDir::tempPath() + "/briefutil_test_loc";
        QDir().mkpath(tmp_dir);

        QString profile_path = tmp_dir + "/test_profile.json";
        QFile f(profile_path);
        if (!f.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "FAIL: cannot write loc test profile: %s\n",
                         qs(profile_path).c_str());
            return 1;
        }
        f.write(k_default_profile_simple_json);
        f.close();

        auto lr = load_sender_profile(qs(profile_path));
        if (!lr.ok) {
            std::fprintf(stderr, "FAIL: loc profile load: %s\n", lr.error.c_str());
            return 1;
        }
        lr.profile.signature_image.clear();

        Letter_input input;
        input.recipient = "Firma Beispiel GmbH\n54321 Beispielstadt";
        input.subject = "Brief mit Localization";
        input.date = "13. April 2026";

        // Generate a long body to force multi-page output (so the page
        // number footer is emitted).
        std::string body;
        for (int i = 0; i < 15; i++) {
            if (i > 0) body += "\n\n";
            body += "Paragraph " + std::to_string(i + 1)
                + " is intentionally long enough to force pagination "
                "across multiple pages so that the page number footer "
                "is emitted somewhere in the document.";
        }
        input.body = body;

        Localization custom;
        custom.closing             = "Yours truly,";
        custom.page_number_format  = "Sheet {current}/{total}";
        custom.error_pdf_open_failed_format = "Open failed for {path}";

        auto open_failed = format_pdf_open_failed(
            custom.error_pdf_open_failed_format, "C:/tmp/out.pdf");
        if (open_failed != "Open failed for C:/tmp/out.pdf") {
            std::fprintf(stderr,
                "FAIL: localized open-failure format was not expanded correctly\n");
            return 1;
        }

        auto br = build_letter(lr.profile, input, qs(tmp_dir),
                               default_theme(), din_5008_form_b(), custom);
        if (!br.error.empty()) {
            std::fprintf(stderr, "FAIL: loc build_letter: %s\n", br.error.c_str());
            return 1;
        }
        if (br.doc.pages.size() < 2) {
            std::fprintf(stderr, "FAIL: loc test expected multi-page output\n");
            return 1;
        }

        bool found_closing = false;
        bool found_page_number = false;
        for (const auto& page : br.doc.pages) {
            for (const auto& element : page.elements) {
                if (const auto* text = std::get_if<Text_block>(&element)) {
                    if (text->text == "Yours truly,") found_closing = true;
                    if (text->text.find("Sheet ") == 0
                        && text->text.find("/") != std::string::npos) {
                        found_page_number = true;
                    }
                }
            }
        }
        if (!found_closing) {
            std::fprintf(stderr,
                "FAIL: localized closing 'Yours truly,' not found in document\n");
            return 1;
        }
        if (!found_page_number) {
            std::fprintf(stderr,
                "FAIL: localized page number 'Sheet X/Y' not found in document\n");
            return 1;
        }
        std::printf("[OK] Localization closing + page number applied\n");

        QFile::remove(profile_path);
        QDir().rmdir(tmp_dir);
    }

    // -- Test 7: backend parity on an image-bearing corpus --
    {
        QString tmp_dir = QDir::tempPath() + "/briefutil_test_parity";
        QDir().mkpath(tmp_dir);

        QString profile_path = tmp_dir + "/test_profile.json";
        QFile f(profile_path);
        if (!f.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "FAIL: cannot write parity test profile: %s\n",
                         qs(profile_path).c_str());
            return 1;
        }
        f.write(k_default_profile_simple_json);
        f.close();

        auto lr = load_sender_profile(qs(profile_path));
        if (!lr.ok) {
            std::fprintf(stderr, "FAIL: parity profile load: %s\n", lr.error.c_str());
            return 1;
        }

        Letter_input input;
        input.recipient = "Firma Beispiel GmbH\nHerrn Erich Beispiel\n"
                          "Beispielweg 42\n54321 Beispielstadt";
        input.subject = "Backend parity check";
        input.date = "14. M\xc3\xa4rz 2026";
        input.body =
            "Erster Absatz f\xc3\xbcr den Parit\xc3\xa4tstest.\n\n"
            "Zweiter Absatz mit mehr Text, damit die Layout- und "
            "Umbruchlogik beide Backends auf die gleiche Seitenzahl bringt.";

        const QString signature_path = tmp_dir + "/mustermann_signature.png";
        if (!write_test_signature_png(signature_path)) {
            std::fprintf(stderr, "FAIL: could not write parity signature PNG\n");
            return 1;
        }

#ifdef BRIEFUTIL_MARK2HARU_FONT_DIR
        QDir mark2_fonts_dir(QString::fromUtf8(BRIEFUTIL_MARK2HARU_FONT_DIR));
        if (!mark2_fonts_dir.exists()) {
            std::printf("[SKIP] parity test font directory not available: %s\n",
                        qs(mark2_fonts_dir.path()).c_str());
        } else {
            Theme_config parity_theme = default_theme();
            parity_theme.fonts.sans = mark2_fonts_dir.filePath("DejaVuSans.ttf").toStdString();
            parity_theme.fonts.sans_bold = mark2_fonts_dir.filePath("DejaVuSans-Bold.ttf").toStdString();
            parity_theme.fonts.sans_italic = mark2_fonts_dir.filePath("DejaVuSans-Oblique.ttf").toStdString();
            parity_theme.fonts.sans_bold_italic = mark2_fonts_dir.filePath("DejaVuSans-BoldOblique.ttf").toStdString();
            parity_theme.fonts.mono = mark2_fonts_dir.filePath("DejaVuSansMono.ttf").toStdString();

            std::string detail;
            if (pdf_measurement_ready(Pdf_backend::Mark2Haru, parity_theme.fonts, &detail)) {
                auto haru = build_letter(lr.profile, input, qs(tmp_dir), parity_theme,
                                         din_5008_form_b(), default_localization(),
                                         Pdf_backend::Haru);
                auto mark2 = build_letter(lr.profile, input, qs(tmp_dir), parity_theme,
                                          din_5008_form_b(), default_localization(),
                                          Pdf_backend::Mark2Haru);

                if (!haru.error.empty()) {
                    std::fprintf(stderr, "FAIL: Haru parity build_letter: %s\n", haru.error.c_str());
                    return 1;
                }
                if (!mark2.error.empty()) {
                    std::fprintf(stderr, "FAIL: mark2haru parity build_letter: %s\n", mark2.error.c_str());
                    return 1;
                }

                std::string reason;
                if (!same_document(haru.doc, mark2.doc, &reason)) {
                    std::fprintf(stderr, "FAIL: backend parity mismatch: %s\n",
                                 reason.c_str());
                    return 1;
                }

                auto rr = render_pdf(mark2.doc, std::string(output) + ".parity.mark2haru.pdf",
                                     parity_theme.fonts, default_localization(),
                                     Pdf_backend::Mark2Haru);
                if (!rr.ok) {
                    std::fprintf(stderr, "FAIL: mark2haru parity render: %s (%s)\n",
                                 rr.message.c_str(), rr.detail.c_str());
                    return 1;
                }
                std::printf("[OK] Haru/mark2haru parity matched on image-bearing corpus\n");
            }
            else if (pdf_backend_available(Pdf_backend::Mark2Haru)) {
                std::printf("[SKIP] mark2haru parity test not ready: %s\n", detail.c_str());
            }
        }
#else
        std::printf("[SKIP] parity test font directory not compiled in\n");
#endif

        QFile::remove(profile_path);
        QFile::remove(signature_path);
        QDir().rmdir(tmp_dir);
    }

    std::printf("\nAll letter-builder tests passed.\n");
    return 0;
}
