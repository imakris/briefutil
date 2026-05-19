// Reads a .md file, builds a letter with its content as body, renders to PDF.
// Usage: test_md_to_pdf <input.md> [output.pdf]

#include "briefutil/default_profiles.h"
#include "briefutil/letter_builder.h"
#include "briefutil/pdf_renderer.h"
#include "briefutil/sender_profile.h"

#include "test_helpers.h"

#include <QCoreApplication>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>


int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        std::fprintf(stderr, "Usage: test_md_to_pdf <input.md> [output.pdf]\n");
        return 1;
    }

    const char* md_path = argv[1];
    const char* output  = argc > 2 ? argv[2] : "md_output.pdf";

    // Read markdown file
    std::ifstream ifs(md_path);
    if (!ifs) {
        std::fprintf(stderr, "Cannot open: %s\n", md_path);
        return 1;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string body = ss.str();
    std::printf("Read %zu bytes from %s\n", body.size(), md_path);

    Profile_fixture fx("briefutil_md_test", k_default_profile_simple_json);
    if (!fx.ok) {
        std::fprintf(stderr, "Profile setup failed: %s\n", fx.error.c_str());
        return 1;
    }
    fx.profile.signature_image.clear();

    Letter_input input;
    input.recipient = "Firma Beispiel GmbH\nHerrn Erich Beispiel\n"
                      "Beispielweg 42\n54321 Beispielstadt";
    input.subject   = "Angebot: Dienstleistungspakete 2026";
    input.date      = "14. M\xc3\xa4rz 2026";
    input.body      = body;

    auto br = build_letter(fx.profile, input, qs(fx.tmp_dir));
    if (!br.error.empty()) {
        std::fprintf(stderr, "Build failed: %s\n", br.error.c_str());
        return 1;
    }

    std::printf("Built %zu page(s)\n", br.doc.pages.size());

    auto rr = render_pdf(
        br.doc,
        output,
        default_font_family(),
        default_localization());
    if (!rr.ok) {
        std::fprintf(
            stderr,
            "Render failed: %s (%s)\n",
            rr.message.c_str(),
            rr.detail.c_str());
        return 1;
    }

    std::printf("PDF saved to: %s\n", output);
    return 0;
}
