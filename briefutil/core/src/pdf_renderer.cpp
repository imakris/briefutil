#include "briefutil/pdf_renderer.h"


Render_result render_pdf(const Document& doc,
                         const std::string& output_path,
                         const Font_family_config& fonts,
                         const Localization& loc,
                         Pdf_backend backend)
{
    switch (backend) {
        case Pdf_backend::Haru:
            return render_pdf_haru(doc, output_path, fonts, loc);
        case Pdf_backend::Mark2Haru:
            return render_pdf_mark2haru(doc, output_path, fonts, loc);
    }
    return { false, "", "Unknown PDF backend.", "" };
}

