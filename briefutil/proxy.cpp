#include "proxy.h"
#include "default_profiles.h"
#include "letter_builder.h"
#include "mustermann_signature.png.h"
#include "pdf_renderer_haru.h"
#include "sender_profile.h"

#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

#include <cstring>
#include <string>

// ============================================================================
// Construction and profile discovery
// ============================================================================

Proxy::Proxy(QObject*)
{
    // Output directory
    QFile output_dir_file("./output_dir.conf");
    QString output_dir;
    if (output_dir_file.open(QIODevice::ReadOnly)) {
        output_dir = QString::fromUtf8(output_dir_file.readAll()).trimmed();
    }

    QDir qodir(output_dir);
    if (!output_dir.isEmpty() && qodir.exists()) {
        m_output_dir = output_dir;
    }
    else {
        m_output_dir = QDir::homePath() + "/briefutil/output/";
        qodir = QDir(m_output_dir);
    }
    if (!qodir.exists())
        qodir.mkpath(".");

    // Template directory
    m_sender_template_dir = QDir::homePath() + "/briefutil/templates/";
    QDir templates_dir(m_sender_template_dir);

    if (!templates_dir.exists()) {
        templates_dir.mkpath(".");
    }

    // Seed the default profiles and signature asset if they are missing.
    auto write_file_if_missing = [&](const QString& path, const char* data,
                                     size_t size, bool refresh_old_placeholder = false) {
        QFileInfo info(path);
        if (info.exists()) {
            if (!(refresh_old_placeholder && info.size() == 67)) {
                return;
            }
        }

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return;
        }
        file.write(data, (qint64)size);
    };

    write_file_if_missing(m_sender_template_dir + "Max Mustermann.json",
                          k_default_profile_simple_json,
                          std::strlen(k_default_profile_simple_json));
    write_file_if_missing(m_sender_template_dir + "Max Mustermann, Mustermann AG.json",
                          k_default_profile_commercial_json,
                          std::strlen(k_default_profile_commercial_json));
    write_file_if_missing(m_sender_template_dir + "mustermann_signature.png",
                          (const char*)mustermann_signature_png::data().first,
                          mustermann_signature_png::data().second,
                          true);

    // Discover .json sender profiles
    const auto profile_files = templates_dir.entryList({ "*.json" }, QDir::Files, QDir::Name);
    for (const auto& profile_file : profile_files) {
        const auto profile_path = templates_dir.filePath(profile_file);
        auto result = load_sender_profile(profile_path.toStdString());
        if (result.ok) {
            m_profiles.push_back(std::move(result.profile));
        }
        else {
            qWarning("briefutil: failed to load profile '%s': %s",
                     qPrintable(profile_file),
                     result.error.c_str());
        }
    }
}


QList<QString> Proxy::get_sender_templates() const
{
    QList<QString> profile_names;
    profile_names.reserve((qsizetype)m_profiles.size());
    for (const auto& profile : m_profiles) {
        profile_names.push_back(QString::fromStdString(profile.id));
    }
    return profile_names;
}


// ============================================================================
// PDF generation — native renderer
// ============================================================================

static QString sanitize_filename(const QString& input)
{
    QString sanitized = input.trimmed();
    sanitized.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1F]"), "_");
    return sanitized;
}

void Proxy::make_pdf(int from, const QString& to,
                     const QString& subject, const QString& body)
{
    if (from < 0 || from >= (int)m_profiles.size()) {
        emit pdf_generated(false, "Invalid sender profile selection.");
        return;
    }

    const auto& profile = m_profiles[from];

    // Build filename: separate display subject from filename slug
    QString prefix = QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss") + " ";
    QString filename_slug = sanitize_filename(subject);
    if (filename_slug.isEmpty())
        filename_slug = "letter";

    QString pdf_filename = prefix + filename_slug + ".pdf";
    pdf_filename.replace(QRegularExpression("\\s+"), " ");

    QString pdf_path = m_output_dir + "/" + pdf_filename;

    // Format date using German locale
    QLocale german(QLocale::German);
    QString date_str = german.toString(QDate::currentDate(), "d. MMMM yyyy");

    QElapsedTimer timer;
    timer.start();

    // Build the letter
    Letter_input input;
    input.recipient = to.toStdString();
    input.subject   = subject.toStdString();
    input.body      = body.toStdString();
    input.date      = date_str.toStdString();

    auto build_result = build_letter(profile, input,
                                     m_sender_template_dir.toStdString());

    if (!build_result.error.empty()) {
        emit pdf_generated(false,
            QString::fromStdString(build_result.error));
        return;
    }

    // Render
    auto result = render_pdf(build_result.doc, pdf_path.toStdString());

    if (!result.ok) {
        emit pdf_generated(false,
            QString::fromStdString(result.message.empty()
                ? result.detail : result.message));
        return;
    }

    // Open the PDF in the default viewer
    qInfo("briefutil: native PDF generated in %lld ms",
          timer.elapsed());

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(pdf_path))) {
        emit pdf_generated(false,
            "PDF wurde erstellt, konnte aber nicht automatisch geoeffnet werden: "
            + pdf_path);
        return;
    }

    emit pdf_generated(true, QString());
}


// ============================================================================
// Dark mode support
// ============================================================================

void Proxy::set_window_dark_mode(QWindow* window, bool dark)
{
#ifdef Q_OS_WIN
    if (!window)
        return;

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    BOOL useDarkMode = dark ? TRUE : FALSE;
    constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &useDarkMode, sizeof(useDarkMode));
#else
    Q_UNUSED(window);
    Q_UNUSED(dark);
#endif
}

void Proxy::save_dark_mode(bool dark)
{
    QSettings settings("briefutil", "briefutil");
    settings.setValue("appearance/darkMode", dark);
}

bool Proxy::load_dark_mode() const
{
    QSettings settings("briefutil", "briefutil");
    return settings.value("appearance/darkMode", false).toBool();
}
