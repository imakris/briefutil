#pragma once

#include <algorithm>
#include <cctype>
#include <string>


// ============================================================================
// PDF backend selection
// ============================================================================

#ifndef BRIEFUTIL_HAS_MARK2HARU
#define BRIEFUTIL_HAS_MARK2HARU 0
#endif

enum class Pdf_backend
{
    Haru,
    Mark2Haru,
};

inline const char* pdf_backend_name(Pdf_backend backend)
{
    switch (backend) {
        case Pdf_backend::Haru:      return "haru";
        case Pdf_backend::Mark2Haru: return "mark2haru";
    }
    return "haru";
}

inline Pdf_backend pdf_backend_from_name(const std::string& name)
{
    std::string normalized = name;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (normalized == "mark2haru") {
        return Pdf_backend::Mark2Haru;
    }
    return Pdf_backend::Haru;
}

inline bool pdf_backend_available(Pdf_backend backend)
{
    return backend == Pdf_backend::Haru
        || (backend == Pdf_backend::Mark2Haru && BRIEFUTIL_HAS_MARK2HARU != 0);
}
