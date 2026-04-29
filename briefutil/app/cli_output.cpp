#include "cli_output.h"

#include <QFileInfo>
#include <QStringList>

QString briefutil_pdf_path_from_cli_stdout(const QString& stdout_text)
{
    const auto lines = stdout_text.split('\n', Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QString candidate = it->trimmed();
        if (candidate.endsWith(".pdf", Qt::CaseInsensitive) &&
            QFileInfo(candidate).isFile())
        {
            return candidate;
        }
    }
    return {};
}
