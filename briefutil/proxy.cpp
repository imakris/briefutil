#include "proxy.h"
#include "default_profiles.h"
#include "letter_builder.h"
#include "mustermann_signature.png.h"
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


// ============================================================================
// Settings persistence
// ============================================================================

static QString default_sender_template_dir()
{
    return QDir::homePath() + "/briefutil/templates/";
}

static bool looks_like_font_file(const std::string& s)
{
    if (s.size() < 4) return false;
    auto ext = s.substr(s.size() - 4);
    return ext == ".ttf" || ext == ".otf" || ext == ".TTF" || ext == ".OTF";
}

static bool is_valid_font_config(const Font_family_config& fc)
{
    const bool sans_is_file = looks_like_font_file(fc.sans);
    const bool sans_bold_is_file = looks_like_font_file(fc.sans_bold);
    const bool sans_italic_is_file = looks_like_font_file(fc.sans_italic);
    const bool sans_bold_italic_is_file = looks_like_font_file(fc.sans_bold_italic);
    const bool mono_is_file = looks_like_font_file(fc.mono);
    const int file_count = (sans_is_file ? 1 : 0)
        + (sans_bold_is_file ? 1 : 0)
        + (sans_italic_is_file ? 1 : 0)
        + (sans_bold_italic_is_file ? 1 : 0)
        + (mono_is_file ? 1 : 0);

    return file_count == 0 || file_count == 5;
}

static void ensure_template_dir_ready(const QString& dir_path)
{
    QDir templates_dir(dir_path);
    if (!templates_dir.exists()) {
        templates_dir.mkpath(".");
    }

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

    write_file_if_missing(dir_path + "Max Mustermann.json",
                          k_default_profile_simple_json,
                          std::strlen(k_default_profile_simple_json));
    write_file_if_missing(dir_path + "Max Mustermann, Mustermann AG.json",
                          k_default_profile_commercial_json,
                          std::strlen(k_default_profile_commercial_json));
    write_file_if_missing(dir_path + "mustermann_signature.png",
                          (const char*)mustermann_signature_png::data().first,
                          mustermann_signature_png::data().second,
                          true);
}

static Font_family_config font_config_from_strings(
    const std::string& sans, const std::string& sans_bold,
    const std::string& sans_italic, const std::string& sans_bold_italic,
    const std::string& mono)
{
    Font_family_config fc;
    fc.sans             = sans;
    fc.sans_bold        = sans_bold;
    fc.sans_italic      = sans_italic;
    fc.sans_bold_italic = sans_bold_italic;
    fc.mono             = mono;

    // Auto-detect source kind: if any font looks like a file path, use TTF mode
    if (looks_like_font_file(sans) || looks_like_font_file(sans_bold) ||
        looks_like_font_file(sans_italic) || looks_like_font_file(sans_bold_italic) ||
        looks_like_font_file(mono)) {
        fc.kind = Font_source_kind::FILE_TTF;
    }
    else {
        fc.kind = Font_source_kind::BASE14;
    }
    return fc;
}

void Proxy::load_settings()
{
    QSettings s("briefutil", "briefutil");

    auto def = default_font_family();
    m_theme.fonts = font_config_from_strings(
        s.value("fonts/sans",             QString::fromStdString(def.sans)).toString().toStdString(),
        s.value("fonts/sans_bold",        QString::fromStdString(def.sans_bold)).toString().toStdString(),
        s.value("fonts/sans_italic",      QString::fromStdString(def.sans_italic)).toString().toStdString(),
        s.value("fonts/sans_bold_italic", QString::fromStdString(def.sans_bold_italic)).toString().toStdString(),
        s.value("fonts/mono",             QString::fromStdString(def.mono)).toString().toStdString());

    auto def_typo = default_typography();
    m_theme.typo.body_size_pt = s.value("typo/body_size",    def_typo.body_size_pt).toFloat();
    m_theme.typo.body_lead_pt = s.value("typo/body_leading", def_typo.body_lead_pt).toFloat();

    QString saved_dir = s.value("paths/template_dir").toString();
    if (!saved_dir.isEmpty()) {
        m_sender_template_dir = saved_dir;
    }
}

void Proxy::save_settings() const
{
    QSettings s("briefutil", "briefutil");

    s.setValue("fonts/sans",             QString::fromStdString(m_theme.fonts.sans));
    s.setValue("fonts/sans_bold",        QString::fromStdString(m_theme.fonts.sans_bold));
    s.setValue("fonts/sans_italic",      QString::fromStdString(m_theme.fonts.sans_italic));
    s.setValue("fonts/sans_bold_italic", QString::fromStdString(m_theme.fonts.sans_bold_italic));
    s.setValue("fonts/mono",             QString::fromStdString(m_theme.fonts.mono));
    s.setValue("typo/body_size",         (double)m_theme.typo.body_size_pt);
    s.setValue("typo/body_leading",      (double)m_theme.typo.body_lead_pt);
    s.setValue("paths/template_dir",     m_sender_template_dir);
}


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
    if (!qodir.exists()) {
        qodir.mkpath(".");
    }

    // Default template directory (may be overridden by saved settings)
    m_sender_template_dir = default_sender_template_dir();

    // Load persistent settings (may override template dir and theme)
    load_settings();

    ensure_template_dir_ready(m_sender_template_dir);

    discover_profiles();
}

void Proxy::discover_profiles()
{
    m_profiles.clear();
    QDir templates_dir(m_sender_template_dir);
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
    emit sender_templates_changed();
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
// PDF generation
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

    QString prefix = QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss") + " ";
    QString filename_slug = sanitize_filename(subject);
    if (filename_slug.isEmpty()) {
        filename_slug = "letter";
    }

    QString pdf_filename = prefix + filename_slug + ".pdf";
    pdf_filename.replace(QRegularExpression("\\s+"), " ");
    QString pdf_path = m_output_dir + "/" + pdf_filename;

    QLocale german(QLocale::German);
    QString date_str = german.toString(QDate::currentDate(), "d. MMMM yyyy");

    QElapsedTimer timer;
    timer.start();

    Letter_input input;
    input.recipient = to.toStdString();
    input.subject   = subject.toStdString();
    input.body      = body.toStdString();
    input.date      = date_str.toStdString();

    if (!is_valid_font_config(m_theme.fonts)) {
        emit pdf_generated(false,
            "Invalid font configuration. Use either built-in font names for all fonts or .ttf/.otf file paths for all fonts.");
        return;
    }

    auto result = generate_letter_pdf(profile, input,
                                      m_sender_template_dir.toStdString(),
                                      pdf_path.toStdString(),
                                      m_theme);

    if (!result.ok) {
        emit pdf_generated(false,
            QString::fromStdString(result.message.empty()
                ? result.detail : result.message));
        return;
    }

    qInfo("briefutil: native PDF generated in %lld ms", timer.elapsed());

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(pdf_path))) {
        emit pdf_generated(false,
            "PDF wurde erstellt, konnte aber nicht automatisch geoeffnet werden: "
            + pdf_path);
        return;
    }

    emit pdf_generated(true, QString());
}


// ============================================================================
// Settings accessors
// ============================================================================

QString Proxy::get_font_sans() const             { return QString::fromStdString(m_theme.fonts.sans); }
QString Proxy::get_font_sans_bold() const        { return QString::fromStdString(m_theme.fonts.sans_bold); }
QString Proxy::get_font_sans_italic() const      { return QString::fromStdString(m_theme.fonts.sans_italic); }
QString Proxy::get_font_sans_bold_italic() const { return QString::fromStdString(m_theme.fonts.sans_bold_italic); }
QString Proxy::get_font_mono() const             { return QString::fromStdString(m_theme.fonts.mono); }
double  Proxy::get_body_size() const             { return m_theme.typo.body_size_pt; }
double  Proxy::get_body_leading() const          { return m_theme.typo.body_lead_pt; }
QString Proxy::get_template_dir() const          { return m_sender_template_dir; }

static void update_font_kind(Font_family_config& fc)
{
    fc.kind = (looks_like_font_file(fc.sans) || looks_like_font_file(fc.sans_bold) ||
               looks_like_font_file(fc.sans_italic) || looks_like_font_file(fc.sans_bold_italic) ||
               looks_like_font_file(fc.mono))
        ? Font_source_kind::FILE_TTF : Font_source_kind::BASE14;
}

void Proxy::set_font_sans(const QString& v)             { m_theme.fonts.sans = v.toStdString(); update_font_kind(m_theme.fonts); save_settings(); }
void Proxy::set_font_sans_bold(const QString& v)        { m_theme.fonts.sans_bold = v.toStdString(); update_font_kind(m_theme.fonts); save_settings(); }
void Proxy::set_font_sans_italic(const QString& v)      { m_theme.fonts.sans_italic = v.toStdString(); update_font_kind(m_theme.fonts); save_settings(); }
void Proxy::set_font_sans_bold_italic(const QString& v) { m_theme.fonts.sans_bold_italic = v.toStdString(); update_font_kind(m_theme.fonts); save_settings(); }
void Proxy::set_font_mono(const QString& v)             { m_theme.fonts.mono = v.toStdString(); update_font_kind(m_theme.fonts); save_settings(); }

void Proxy::set_body_size(double v)
{
    m_theme.typo.body_size_pt = (float)v;
    save_settings();
}

void Proxy::set_body_leading(double v)
{
    m_theme.typo.body_lead_pt = (float)v;
    save_settings();
}

void Proxy::set_template_dir(const QString& v)
{
    QString dir = v.trimmed();
    if (dir.isEmpty()) {
        dir = default_sender_template_dir();
    }

    QDir normalized(dir);
    m_sender_template_dir = normalized.absolutePath();
    if (!m_sender_template_dir.endsWith('/')) {
        m_sender_template_dir += '/';
    }

    ensure_template_dir_ready(m_sender_template_dir);
    save_settings();
    discover_profiles();
}


// ============================================================================
// Dark mode support
// ============================================================================

void Proxy::set_window_dark_mode(QWindow* window, bool dark)
{
#ifdef Q_OS_WIN
    if (!window) {
        return;
    }
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
