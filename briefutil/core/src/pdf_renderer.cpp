#include "briefutil/pdf_renderer.h"


render_result_t render_pdf(
    const document_t& doc,
    const std::string& output_path,
    const font_family_config_t& fonts,
    const localization_t& loc,
    Pdf_backend backend)
{
    switch (backend) {
        case Pdf_backend::Haru:      return render_pdf_haru(doc, output_path, fonts, loc);
        case Pdf_backend::Mark2Haru: return render_pdf_mark2haru(doc, output_path, fonts, loc);
        default:                     return { false, "", "Unknown PDF backend.", "" };
    }
}
