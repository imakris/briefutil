#include "proxy.h"
#include "cli_output.h"
#include "briefutil/brief_service.h"
#include "briefutil/localization.h"
#include "briefutil/owned_staging.h"
#include "briefutil/path_utils.h"
#include "briefutil/sender_profile.h"
#include "briefutil/sender_profile_schema.h"
#include "briefutil/template_store.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#endif

#include <algorithm>
#include <cmath>
#include <memory>

#ifndef BRIEFUTIL_VERSION_STRING
#define BRIEFUTIL_VERSION_STRING "unknown"
#endif

// ============================================================================
// Settings persistence
// ============================================================================

static QString default_sender_template_dir()
{
    return QString::fromStdString(briefutil::default_template_dir());
}

static QString normalize_profile_language(const QString& language)
{
    return QString::fromStdString(briefutil::normalize_language(language.toStdString()));
}

static bool is_valid_font_config(const Font_family_config& fc)
{
    return briefutil::is_valid_font_config(fc);
}

static QString normalize_layout_preset(QString preset)
{
    preset = preset.trimmed().toLower();
    if (preset == "din_5008_form_a" || preset == "us_letter") {
        return preset;
    }
    return "din_5008_form_b";
}

static float font_scale_from_percent(double percent)
{
    return briefutil::font_scale_from_percent(percent);
}

static float clamp_float(float value, float minimum, float maximum)
{
    return std::clamp(value, minimum, maximum);
}

namespace {

struct Typo_setting
{
    const char*                   key;
    float typography_config_t::*  field;
    float                         min_value;
    float                         max_value;
};

constexpr Typo_setting k_typo_settings[] = {
    { "typo/body_size",    &typography_config_t::body_size_pt, 6.0f, 24.0f },
    { "typo/body_leading", &typography_config_t::body_lead_pt, 6.0f, 36.0f },
    { "typo/header_scale", &typography_config_t::header_scale, 0.5f, 2.0f  },
    { "typo/body_scale",   &typography_config_t::body_scale,   0.5f, 2.0f  },
    { "typo/footer_scale", &typography_config_t::footer_scale, 0.5f, 2.0f  },
};

} // anonymous namespace

static bool ensure_template_dir_ready(const QString& dir_path)
{
    std::string error;
    if (!briefutil::ensure_template_dir_ready(dir_path.toStdString(), &error)) {
        qWarning("briefutil: failed to initialize template directory '%s': %s",
            qPrintable(dir_path),
            error.c_str());
        return false;
    }
    return true;
}

static QString build_info_value(QSettings& settings, const QString& key, const QString& fallback)
{
    auto value = settings.value("build/" + key);
    if (!value.isValid() || value.toString().trimmed().isEmpty()) {
        return fallback;
    }
    return value.toString();
}

static QString default_build_info_path()
{
    return QCoreApplication::applicationDirPath() + "/briefutil_app_build_info.ini";
}

static QString build_caption(const QString& version)
{
    return QStringLiteral("v%1").arg(version);
}

static QString build_details(
    const QString& version,
    const QString& commit,
    const QString& timestamp)
{
    return QStringLiteral("briefutil %1\nCommit %2\nBuilt %3").arg(version, commit, timestamp);
}

static QString join_lines(const std::vector<std::string>& lines)
{
    QStringList result;
    for (const auto& line : lines) {
        result.push_back(QString::fromStdString(line));
    }
    return result.join('\n');
}

static std::vector<std::string> split_profile_lines(const QString& text)
{
    std::vector<std::string> result;
    if (text.isEmpty()) {
        return result;
    }

    for (auto line : text.split('\n', Qt::KeepEmptyParts)) {
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        result.push_back(line.toStdString());
    }
    return result;
}

static QString normalize_asset_name(const QString& v)
{
    auto trimmed = v.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(trimmed.replace('\\', '/'));
}

static QString color_to_hex(color_t color)
{
    auto to_component = [](float value) -> int {
        return std::clamp((int)std::lround(value * 255.0f), 0, 255);
    };
    return QString("#%1%2%3")
        .arg(to_component(color.r), 2, 16, QChar('0'))
        .arg(to_component(color.g), 2, 16, QChar('0'))
        .arg(to_component(color.b), 2, 16, QChar('0'))
        .toUpper();
}

static bool parse_hex_color(const QString& v, color_t& out)
{
    QRegularExpression re("^#([0-9A-Fa-f]{6})$");
    auto match = re.match(v.trimmed());
    if (!match.hasMatch()) {
        return false;
    }

    auto hex  = match.captured(1);
    bool ok_r = false;
    bool ok_g = false;
    bool ok_b = false;
    int  r    = hex.mid(0, 2).toInt(&ok_r, 16);
    int  g    = hex.mid(2, 2).toInt(&ok_g, 16);
    int  b    = hex.mid(4, 2).toInt(&ok_b, 16);
    if (!ok_r || !ok_g || !ok_b) {
        return false;
    }

    out = {
        r / 255.0f,
        g / 255.0f,
        b / 255.0f,
    };
    return true;
}

enum class Font_role
{
    SANS,
    SANS_BOLD,
    SANS_ITALIC,
    SANS_BOLD_ITALIC,
    MONO,
    ANY,
};

static Font_role font_role_from_string(const QString& role)
{
    auto normalized = role.trimmed().toLower();
    if (normalized == "sans")             { return Font_role::SANS;             }
    if (normalized == "sans_bold")        { return Font_role::SANS_BOLD;        }
    if (normalized == "sans_italic")      { return Font_role::SANS_ITALIC;      }
    if (normalized == "sans_bold_italic") { return Font_role::SANS_BOLD_ITALIC; }
    if (normalized == "mono")             { return Font_role::MONO;             }
    return Font_role::ANY;
}

static bool is_base14_font_name(const std::string& s)
{
    static constexpr const char* k_base14[] = {
        "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique",
        "Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique",
        "Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic",
        "Symbol", "ZapfDingbats"
    };
    for (const auto* name : k_base14) {
        if (s == name) {
            return true;
        }
    }
    return false;
}

static bool is_supported_font_file_path(const QString& path)
{
    auto suffix = QFileInfo(path).suffix().toLower();
    return suffix == "ttf";
}

static QString normalize_saved_font_input(QString value)
{
    value = value.trimmed();
    if (value.isEmpty()) {
        return QString();
    }
    if (is_base14_font_name(value.toStdString())) {
        return QString();
    }
    return value;
}

#ifdef Q_OS_WIN
static QString normalize_font_lookup_key(QString name)
{
    name.remove(QRegularExpression("\\s*\\([^)]*\\)$"));
    name.replace(QRegularExpression("([a-z0-9])([A-Z])"), "\\1 \\2");
    name.replace(QRegularExpression("[-_]+"), " ");
    return name.simplified().toLower();
}

static QStringList windows_font_base_dirs()
{
    QStringList dirs;

    auto add_dir = [&](const QString& path) {
        if (path.isEmpty()) {
            return;
        }
        auto absolute = QDir(path).absolutePath();
        if (!dirs.contains(absolute)) {
            dirs.push_back(absolute);
        }
    };

    add_dir(QStandardPaths::writableLocation(QStandardPaths::FontsLocation));
    add_dir(qEnvironmentVariable("WINDIR") + "/Fonts");
    add_dir(qEnvironmentVariable("LOCALAPPDATA") + "/Microsoft/Windows/Fonts");
    return dirs;
}

static QString resolve_windows_font_path(QString path, const QStringList& base_dirs)
{
    path = path.trimmed();
    if (path.isEmpty()) {
        return QString();
    }

    QFileInfo info(path);
    if (info.isAbsolute() && info.exists() && info.isFile()) {
        return info.absoluteFilePath();
    }

    for (const auto& base_dir : base_dirs) {
        QFileInfo candidate(QDir(base_dir).filePath(path));
        if (candidate.exists() && candidate.isFile()) {
            return candidate.absoluteFilePath();
        }
    }

    return QString();
}

static void append_windows_registry_fonts(
    QHash<QString, QString>&   map,
    const QString&             registry_path,
    const QStringList&         base_dirs)
{
    QSettings settings(registry_path, QSettings::NativeFormat);
    for (const auto& key : settings.allKeys()) {
        auto path = resolve_windows_font_path(settings.value(key).toString(), base_dirs);
        if (path.isEmpty() || !is_supported_font_file_path(path)) {
            continue;
        }

        const auto normalized_key = normalize_font_lookup_key(key);
        if (!normalized_key.isEmpty() && !map.contains(normalized_key)) {
            map.insert(normalized_key, path);
        }

        const auto basename_key = normalize_font_lookup_key(QFileInfo(path).completeBaseName());
        if (!basename_key.isEmpty() && !map.contains(basename_key)) {
            map.insert(basename_key, path);
        }
    }
}

static const QHash<QString, QString>& installed_windows_font_files()
{
    static const QHash<QString, QString> fonts = [] {
        QHash<QString, QString> map;
        const auto base_dirs = windows_font_base_dirs();
        append_windows_registry_fonts(
            map,
            "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
            base_dirs);
        append_windows_registry_fonts(
            map,
            "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
            base_dirs);
        return map;
    }();
    return fonts;
}

static QStringList font_name_candidates(const QString& family, Font_role role)
{
    auto base = family.trimmed();
    QStringList candidates;
    if (base.isEmpty()) {
        return candidates;
    }

    candidates.push_back(base);
    switch (role) {
        case Font_role::SANS_BOLD:
            candidates
                << (base + " Bold")
                << (base + " DemiBold")
                << (base + " Semibold")
                << (base + " SemiBold")
                << (base + " Demi Bold")
                << (base + " Medium");
            break;
        case Font_role::SANS_ITALIC:
            candidates
                << (base + " Italic")
                << (base + " Oblique");
            break;
        case Font_role::SANS_BOLD_ITALIC:
            candidates
                << (base + " Bold Italic")
                << (base + " BoldItalic")
                << (base + " Bold Oblique")
                << (base + " Semibold Italic")
                << (base + " SemiBold Italic")
                << (base + " Demi Bold Italic")
                << (base + " Medium Italic");
            break;
        case Font_role::SANS:
        case Font_role::MONO:
            candidates
                << (base + " Regular")
                << (base + " Roman")
                << (base + " Book");
            break;
        case Font_role::ANY:
            candidates
                << (base + " Regular")
                << (base + " Roman")
                << (base + " Book")
                << (base + " Bold")
                << (base + " DemiBold")
                << (base + " Semibold")
                << (base + " SemiBold")
                << (base + " Italic")
                << (base + " Bold Italic")
                << (base + " BoldItalic")
                << (base + " Oblique")
                << (base + " Bold Oblique")
                << (base + " Semibold Italic")
                << (base + " SemiBold Italic");
            break;
        default:
            break;
    }
    candidates.removeDuplicates();
    return candidates;
}

static QString resolve_windows_system_font(const QString& family, Font_role role)
{
    const auto& fonts      = installed_windows_font_files();
    auto        candidates = font_name_candidates(family, role);
    if (role != Font_role::ANY) {
        candidates.append(font_name_candidates(family, Font_role::ANY));
        candidates.removeDuplicates();
    }

    for (const auto& candidate : candidates) {
        auto it = fonts.constFind(normalize_font_lookup_key(candidate));
        if (it != fonts.cend()) {
            return it.value();
        }
    }
    return QString();
}
#endif

static QString resolve_font_value(const QString& value, Font_role role)
{
    auto trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    auto s = trimmed.toStdString();
    if (looks_like_font_file(s)) {
        QFileInfo info(trimmed);
        return
            info.exists() &&
            info.isFile()
                ? info.absoluteFilePath()
                : QString();
    }

#ifdef Q_OS_WIN
    return resolve_windows_system_font(trimmed, role);
#else
    Q_UNUSED(role);
    return QString();
#endif
}

static Font_family_config font_config_from_inputs(
    const QString& sans,
    const QString& sans_bold,
    const QString& sans_italic,
    const QString& sans_bold_italic,
    const QString& mono)
{
    return {
        resolve_font_value(sans,             Font_role::SANS).toStdString(),
        resolve_font_value(sans_bold,        Font_role::SANS_BOLD).toStdString(),
        resolve_font_value(sans_italic,      Font_role::SANS_ITALIC).toStdString(),
        resolve_font_value(sans_bold_italic, Font_role::SANS_BOLD_ITALIC).toStdString(),
        resolve_font_value(mono,             Font_role::MONO).toStdString(),
    };
}

void Proxy::load_settings()
{
    QSettings s("briefutil", "briefutil");

    const std::pair<const char*, QString*> font_slots[] = {
        { "fonts/sans",             &m_font_sans_input },
        { "fonts/sans_bold",        &m_font_sans_bold_input },
        { "fonts/sans_italic",      &m_font_sans_italic_input },
        { "fonts/sans_bold_italic", &m_font_sans_bold_italic_input },
        { "fonts/mono",             &m_font_mono_input },
    };
    for (const auto& [key, slot] : font_slots) {
        *slot = normalize_saved_font_input(s.value(key).toString());
    }

    if (!s.value("fonts/print_default_v1", false).toBool()) {
        const QString preferred = "Noto Sans";
        if (m_font_sans_input.isEmpty()             &&
            m_font_sans_bold_input.isEmpty()        &&
            m_font_sans_italic_input.isEmpty()      &&
            m_font_sans_bold_italic_input.isEmpty() &&
            !resolve_font_value(preferred, Font_role::SANS).isEmpty()             &&
            !resolve_font_value(preferred, Font_role::SANS_BOLD).isEmpty()        &&
            !resolve_font_value(preferred, Font_role::SANS_ITALIC).isEmpty()      &&
            !resolve_font_value(preferred, Font_role::SANS_BOLD_ITALIC).isEmpty())
        {
            m_font_sans_input             = preferred;
            m_font_sans_bold_input        = preferred;
            m_font_sans_italic_input      = preferred;
            m_font_sans_bold_italic_input = preferred;
            for (const auto& [key, slot] : font_slots) {
                s.setValue(key, *slot);
            }
        }
        s.setValue("fonts/print_default_v1", true);
    }
    m_theme.fonts = font_config_from_inputs(
        m_font_sans_input,
        m_font_sans_bold_input,
        m_font_sans_italic_input,
        m_font_sans_bold_italic_input,
        m_font_mono_input);

    const auto def_typo = default_typography();
    for (const auto& t : k_typo_settings) {
        m_theme.typo.*t.field = clamp_float(
            s.value(t.key, def_typo.*t.field).toFloat(),
            t.min_value,
            t.max_value);
    }

    QString saved_dir = s.value("paths/template_dir").toString();
    if (qEnvironmentVariableIsEmpty("BRIEFUTIL_TEMPLATE_DIR") && !saved_dir.isEmpty()) {
        m_sender_template_dir = saved_dir;
    }

    m_layout_preset = normalize_layout_preset(
        s.value("layout/preset", m_layout_preset).toString());
    m_dark_mode = s.value("appearance/darkMode", false).toBool();
}

void Proxy::save_settings() const
{
    QSettings s("briefutil", "briefutil");

    const std::pair<const char*, const QString*> font_slots[] = {
        { "fonts/sans",             &m_font_sans_input },
        { "fonts/sans_bold",        &m_font_sans_bold_input },
        { "fonts/sans_italic",      &m_font_sans_italic_input },
        { "fonts/sans_bold_italic", &m_font_sans_bold_italic_input },
        { "fonts/mono",             &m_font_mono_input },
    };
    for (const auto& [key, slot] : font_slots) {
        s.setValue(key, *slot);
    }
    for (const auto& t : k_typo_settings) {
        s.setValue(t.key, static_cast<double>(m_theme.typo.*t.field));
    }

    if (qEnvironmentVariableIsEmpty("BRIEFUTIL_TEMPLATE_DIR")) {
        s.setValue("paths/template_dir", m_sender_template_dir);
    }
    s.setValue("layout/preset",       m_layout_preset);
    s.setValue("appearance/darkMode", m_dark_mode);
}

Localization Proxy::current_localization(const Sender_profile& profile) const
{
    return briefutil::localization_for_language(profile.language);
}

// ============================================================================
// Construction and profile discovery
// ============================================================================

Proxy::Proxy(QObject*)
{
    m_output_dir = QString::fromStdString(briefutil::configured_output_dir(
        QCoreApplication::applicationDirPath().toStdString(),
        QDir::currentPath().toStdString()));
    QSettings build_info(default_build_info_path(), QSettings::IniFormat);
    auto version = build_info_value(
        build_info, "version", QStringLiteral(BRIEFUTIL_VERSION_STRING));
    auto commit = build_info_value(build_info, "git_commit", QStringLiteral("unknown"));
    auto timestamp = build_info_value(
        build_info, "build_timestamp", QStringLiteral("unknown"));
    m_build_caption = build_caption(version);
    m_build_details = build_details(version, commit, timestamp);

    QDir qodir(m_output_dir);
    if (!qodir.exists()) {
        qodir.mkpath(".");
    }

    // Default template directory (may be overridden by saved settings)
    m_sender_template_dir = default_sender_template_dir();

    // Load persistent settings (may override template dir and theme)
    load_settings();

    ensure_template_dir_ready(m_sender_template_dir);

    // Debounce filesystem-watcher events so a flurry of writes (e.g.
    // editor autosave) only triggers one reload.
    m_discover_timer = new QTimer(this);
    m_discover_timer->setSingleShot(true);
    m_discover_timer->setInterval(250);
    connect(m_discover_timer, &QTimer::timeout, this, [this]() {
        discover_profiles();
    });

    install_template_watcher();
    discover_profiles();
}

void Proxy::install_template_watcher()
{
    if (!m_template_watcher) {
        m_template_watcher = new QFileSystemWatcher(this);
        connect(
            m_template_watcher,
            &QFileSystemWatcher::directoryChanged,
            this,
            [this](const QString&) {
                if (m_discover_timer) {
                    m_discover_timer->start();
                }
            });
    }
    auto current = m_template_watcher->directories();
    if (!current.isEmpty()) {
        m_template_watcher->removePaths(current);
    }
    if (QDir(m_sender_template_dir).exists()) {
        m_template_watcher->addPath(m_sender_template_dir);
    }
}

void Proxy::discover_profiles()
{
    m_profiles.clear();
    std::vector<std::string> errors;
    auto profiles = briefutil::discover_profiles(m_sender_template_dir.toStdString(), &errors);
    for (auto& entry : profiles) {
        m_profiles.push_back({
            std::move(entry.profile),
            QString::fromStdString(entry.path),
        });
    }
    for (const auto& error : errors) {
        qWarning("briefutil: failed to load profile: %s", error.c_str());
    }
    emit sender_templates_changed();
}

QList<QString> Proxy::get_sender_templates() const
{
    QList<QString> profile_names;
    profile_names.reserve((qsizetype)m_profiles.size());
    for (const auto& entry : m_profiles) {
        profile_names.push_back(QString::fromStdString(entry.profile.id));
    }
    return profile_names;
}


// ============================================================================
// PDF generation
// ============================================================================

static QString sanitize_filename(const QString& input)
{
    return QString::fromStdString(briefutil::sanitize_filename_component(input.toStdString()));
}

static QString unique_file_name(const QDir& dir, const QString& stem, const QString& extension)
{
    QString candidate = stem + "." + extension;
    int     counter   = 2;
    while (dir.exists(candidate)) {
        candidate = stem + " " + QString::number(counter++) + "." + extension;
    }
    return candidate;
}

static QString unique_profile_file_name(const QDir& dir, const QString& base_name)
{
    QString stem = sanitize_filename(base_name);
    if (stem.isEmpty()) {
        stem = "profile";
    }
    return unique_file_name(dir, stem, "json");
}

static QString unique_image_file_name(const QDir& dir, const QString& file_name)
{
    QFileInfo info(file_name);
    QString   stem = info.completeBaseName();
    if (stem.isEmpty()) {
        stem = "image";
    }
    return unique_file_name(dir, stem, info.suffix());
}

static bool open_generated_pdf(const QString& pdf_path)
{
#ifdef Q_OS_WIN
    auto result = reinterpret_cast<qintptr>(
        ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(pdf_path.utf16()), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(pdf_path));
#endif
}

static QString briefutil_cli_path()
{
    const QString app_dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QString exe_name = "briefutil_cli.exe";
#else
    const QString exe_name = "briefutil_cli";
#endif
    const QString local_path = QDir(app_dir).filePath(exe_name);
    return local_path;
}

static bool write_utf8_temp_file(
    QTemporaryFile&    file,
    const QString&     text,
    QString*           error)
{
    if (!file.open()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    if (!file.flush()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    file.close();
    return true;
}

static QString cli_failure_message(QProcess& process, const QString& process_error = {})
{
    const QString stderr_text = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (!stderr_text.isEmpty())   { return stderr_text;                                             }
    if (!process_error.isEmpty()) { return QString("PDF generation failed: %1").arg(process_error); }

    if (process.error() != QProcess::UnknownError) {
        return process.errorString();
    }
    return QString("PDF generation failed with exit code %1.").arg(process.exitCode());
}

void Proxy::make_pdf(
    int            from,
    const QString& to,
    const QString& subject,
    const QString& body)
{
    if (from < 0 || from >= (int)m_profiles.size()) {
        emit pdf_generated(false, "Invalid sender profile selection.");
        return;
    }

    if (!validate_font_value(m_font_sans_input, "sans")                         ||
        !validate_font_value(m_font_sans_bold_input, "sans_bold")               ||
        !validate_font_value(m_font_sans_italic_input, "sans_italic")           ||
        !validate_font_value(m_font_sans_bold_italic_input, "sans_bold_italic") ||
        !validate_font_value(m_font_mono_input, "mono")                         ||
        !is_valid_font_config(m_theme.fonts))
    {
        emit pdf_generated(
            false,
            "Invalid font configuration. Leave font fields empty for bundled "
            "fonts or use installed TrueType fonts or explicit .ttf files.");
        return;
    }

    const QString cli_path = briefutil_cli_path();
    if (!QFileInfo::exists(cli_path)) {
        emit pdf_generated(
            false,
            QString("Could not find the briefutil CLI executable: %1").arg(cli_path));
        return;
    }

    auto recipient_file = std::make_shared<QTemporaryFile>(
        QDir::tempPath() + "/briefutil-recipient-XXXXXX.txt");
    auto body_file = std::make_shared<QTemporaryFile>(
        QDir::tempPath() + "/briefutil-body-XXXXXX.md");
    QString temp_error;
    if (!write_utf8_temp_file(*recipient_file, to, &temp_error)) {
        emit pdf_generated(
            false,
            QString("Could not prepare recipient text for PDF generation: %1").arg(temp_error));
        return;
    }
    if (!write_utf8_temp_file(*body_file, body, &temp_error)) {
        emit pdf_generated(
            false,
            QString("Could not prepare body text for PDF generation: %1").arg(temp_error));
        return;
    }

    const Sender_profile profile = m_profiles[from].profile;
    QStringList args;
    args
        << "--to-file"               << recipient_file->fileName()
        << "--subject"               << subject
        << "--body-file"             << body_file->fileName()
        << "--profile-path"          << m_profiles[from].path
        << "--template-dir"          << m_sender_template_dir
        << "--output-dir"            << m_output_dir
        << "--layout"                << m_layout_preset
        << "--font-sans"             << QString::fromStdString(m_theme.fonts.sans)
        << "--font-sans-bold"        << QString::fromStdString(m_theme.fonts.sans_bold)
        << "--font-sans-italic"      << QString::fromStdString(m_theme.fonts.sans_italic)
        << "--font-sans-bold-italic" << QString::fromStdString(m_theme.fonts.sans_bold_italic)
        << "--font-mono"             << QString::fromStdString(m_theme.fonts.mono)
        << "--body-size"             << QString::number(m_theme.typo.body_size_pt, 'g', 12)
        << "--body-leading"          << QString::number(m_theme.typo.body_lead_pt, 'g', 12)
        << "--header-scale"          << QString::number(m_theme.typo.header_scale * 100.0f, 'g', 12)
        << "--body-scale"            << QString::number(m_theme.typo.body_scale * 100.0f, 'g', 12)
        << "--footer-scale"          << QString::number(m_theme.typo.footer_scale * 100.0f, 'g', 12);

    auto process       = new QProcess(this);
    auto timer         = std::make_shared<QElapsedTimer>();
    auto completed     = std::make_shared<bool>(false);
    auto process_error = std::make_shared<QString>();
    timer->start();

    process->setProgram(cli_path);
    process->setArguments(args);
    process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(process, &QProcess::errorOccurred, this,
        [this, process, recipient_file, body_file, completed, process_error](
            QProcess::ProcessError error)
        {
            if (*completed) {
                return;
            }
            const QString error_text = process->errorString();
            if (process_error->isEmpty() && !error_text.isEmpty()) { *process_error = error_text; }
            if (error != QProcess::FailedToStart)                  { return;                      }
            *completed = true;
            emit pdf_generated(
                false,
                QString("Could not start the briefutil CLI: %1").arg(*process_error));
            process->deleteLater();
        });

    connect(process,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this, process, recipient_file, body_file, timer, completed, process_error, profile](
            int exit_code,
            QProcess::ExitStatus exit_status)
        {
            if (*completed) {
                return;
            }
            *completed = true;

            if (exit_status != QProcess::NormalExit || exit_code != 0) {
                emit pdf_generated(false, cli_failure_message(*process, *process_error));
                process->deleteLater();
                return;
            }

            qInfo("briefutil: CLI PDF generation finished in %lld ms", timer->elapsed());

            const QString stdout_text = QString::fromUtf8(
                process->readAllStandardOutput()).trimmed();
            const QString pdf_path = briefutil_pdf_path_from_cli_stdout(stdout_text);
            if (pdf_path.isEmpty()) {
                emit pdf_generated(
                    false,
                    "PDF generation completed but did not report an output path.");
                process->deleteLater();
                return;
            }

            if (!open_generated_pdf(pdf_path)) {
                emit pdf_generated(
                    false,
                    QString::fromStdString(
                        format_pdf_open_failed(
                            current_localization(profile).error_pdf_open_failed_format,
                            pdf_path.toUtf8().toStdString())));
                process->deleteLater();
                return;
            }

            emit pdf_generated(true, QString());
            process->deleteLater();
        });

    QTimer::singleShot(300000, process,
        [this, process, recipient_file, body_file, completed] {
            if (*completed) {
                return;
            }
            *completed = true;
            process->kill();
            emit pdf_generated(false, "PDF generation timed out.");
            process->deleteLater();
        });

    process->start();
}


// ============================================================================
// Settings accessors
// ============================================================================

QString Proxy::get_font_sans() const                 { return m_font_sans_input;                 }
QString Proxy::get_font_sans_bold() const            { return m_font_sans_bold_input;            }
QString Proxy::get_font_sans_italic() const          { return m_font_sans_italic_input;          }
QString Proxy::get_font_sans_bold_italic() const     { return m_font_sans_bold_italic_input;     }
QString Proxy::get_font_mono() const                 { return m_font_mono_input;                 }
double  Proxy::get_body_size() const                 { return m_theme.typo.body_size_pt;         }
double  Proxy::get_body_leading() const              { return m_theme.typo.body_lead_pt;         }
double  Proxy::get_header_font_scale_percent() const { return m_theme.typo.header_scale * 100.0; }
double  Proxy::get_body_font_scale_percent() const   { return m_theme.typo.body_scale   * 100.0; }
double  Proxy::get_footer_font_scale_percent() const { return m_theme.typo.footer_scale * 100.0; }
QString Proxy::get_template_dir() const              { return m_sender_template_dir;             }
QString Proxy::get_layout_preset() const             { return m_layout_preset;                   }

void Proxy::update_font_and_save(QString& slot, const QString& v)
{
    slot = v.trimmed();
    m_theme.fonts = font_config_from_inputs(
        m_font_sans_input,
        m_font_sans_bold_input,
        m_font_sans_italic_input,
        m_font_sans_bold_italic_input,
        m_font_mono_input);
    save_settings();
}

void Proxy::set_font_sans(const QString& v)             { update_font_and_save(m_font_sans_input, v);             }
void Proxy::set_font_sans_bold(const QString& v)        { update_font_and_save(m_font_sans_bold_input, v);        }
void Proxy::set_font_sans_italic(const QString& v)      { update_font_and_save(m_font_sans_italic_input, v);      }
void Proxy::set_font_sans_bold_italic(const QString& v) { update_font_and_save(m_font_sans_bold_italic_input, v); }
void Proxy::set_font_mono(const QString& v)             { update_font_and_save(m_font_mono_input, v);             }

void Proxy::set_body_size(double v)
{
    m_theme.typo.body_size_pt = clamp_float((float)v, 6.0f, 24.0f);
    save_settings();
}

void Proxy::set_body_leading(double v)
{
    m_theme.typo.body_lead_pt = clamp_float((float)v, 6.0f, 36.0f);
    save_settings();
}

void Proxy::set_header_font_scale_percent(double v)
{
    m_theme.typo.header_scale = font_scale_from_percent(v);
    save_settings();
}

void Proxy::set_body_font_scale_percent(double v)
{
    m_theme.typo.body_scale = font_scale_from_percent(v);
    save_settings();
}

void Proxy::set_footer_font_scale_percent(double v)
{
    m_theme.typo.footer_scale = font_scale_from_percent(v);
    save_settings();
}

void Proxy::set_template_dir(const QString& v)
{
    QString dir = v.trimmed();
    if (dir.isEmpty()) {
        dir = default_sender_template_dir();
    }

    QDir normalized(dir);
    QString candidate = normalized.absolutePath();
    if (!candidate.endsWith('/')) {
        candidate += '/';
    }

    // Only commit to a template directory the app could actually initialize;
    // otherwise keep the previous one rather than persisting a broken path.
    if (!ensure_template_dir_ready(candidate)) {
        return;
    }

    m_sender_template_dir = candidate;
    save_settings();
    install_template_watcher();
    discover_profiles();
}

void Proxy::set_layout_preset(const QString& v)
{
    m_layout_preset = normalize_layout_preset(v);
    save_settings();
}

bool Proxy::validate_font_value(const QString& v, const QString& role) const
{
    auto trimmed = v.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }

    auto s = trimmed.toStdString();
    if (looks_like_font_file(s)) {
        return QFileInfo(trimmed).exists() && is_supported_font_file_path(trimmed);
    }
    return !resolve_font_value(trimmed, font_role_from_string(role)).isEmpty();
}

bool Proxy::validate_directory_syntax_and_root(const QString& v) const
{
    auto trimmed = v.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }

    if (!QDir::isAbsolutePath(trimmed)) {
        return false;
    }

    QDir dir(trimmed);
    return QDir(dir.rootPath()).exists();
}

QVariantMap Proxy::get_sender_profile(int index) const
{
    if (index < 0 || index >= (int)m_profiles.size()) {
        return {};
    }

    const auto& profile = m_profiles[index].profile;
    QVariantMap result;
    for (const auto& f : k_sender_string_fields) {
        result.insert(f.qml_key, QString::fromStdString(profile.*f.member));
    }
    for (const auto& f : k_sender_string_array_fields) {
        result.insert(f.qml_key, join_lines(profile.*f.member));
    }
    result.insert("language",     normalize_profile_language(QString::fromStdString(profile.language)));
    result.insert("style",        profile.style == Profile_style::COMMERCIAL ? "commercial" : "simple");
    result.insert("topRuleColor", color_to_hex(profile.top_rule_color));
    return result;
}

bool Proxy::save_sender_profile(int index, const QVariantMap& profile_data)
{
    if (index < 0 || index >= (int)m_profiles.size()) {
        qWarning("briefutil: cannot save sender profile: invalid index %d", index);
        return false;
    }

    Sender_profile updated = m_profiles[index].profile;

    auto id = profile_data.value("id").toString().trimmed();
    if (id.isEmpty()) {
        return false;
    }
    updated.id = id.toStdString();

    auto style = profile_data.value("style").toString().trimmed().toLower();
    updated.style = style == "commercial"
        ? Profile_style::COMMERCIAL : Profile_style::SIMPLE;

    updated.sender_lines        = split_profile_lines(profile_data.value("senderLines").toString());
    updated.email               = profile_data.value("email").toString().trimmed().toStdString();
    updated.language            = normalize_profile_language(profile_data.value("language").toString()).toStdString();
    updated.return_address_line = profile_data.value("returnAddressLine").toString().trimmed().toStdString();
    updated.closing_phrase      = profile_data.value("closingPhrase").toString().trimmed().toStdString();
    updated.signer_name         = profile_data.value("signerName").toString().trimmed().toStdString();

    auto signature_image = normalize_asset_name(profile_data.value("signatureImage").toString());
    if (!validate_profile_image_name(signature_image)) {
        return false;
    }
    updated.signature_image = signature_image.toStdString();

    updated.signer_title = profile_data.value("signerTitle").toString().trimmed().toStdString();
    updated.footer_lines = split_profile_lines(profile_data.value("footerLines").toString());

    auto logo_image = normalize_asset_name(profile_data.value("logoImage").toString());
    if (updated.style == Profile_style::COMMERCIAL) {
        if (!validate_profile_image_name(logo_image)) {
            return false;
        }
        color_t top_rule_color;
        if (!parse_hex_color(profile_data.value("topRuleColor").toString(), top_rule_color)) {
            return false;
        }
        updated.top_rule_color = top_rule_color;
    }
    updated.logo_image = updated.style == Profile_style::COMMERCIAL
        ? logo_image.toStdString()
        : std::string();

    // If the id changed, rename the backing JSON file as well so that the
    // template directory stays consistent with the in-memory profile list.
    const QString old_path   = m_profiles[index].path;
    QString       save_path  = old_path;
    const bool    id_changed = updated.id != m_profiles[index].profile.id;
    if (id_changed) {
        QString safe_id = sanitize_filename(QString::fromStdString(updated.id));
        if (safe_id.isEmpty()) {
            safe_id = "profile";
        }
        QDir dir(m_sender_template_dir);

        // Don't clobber another profile's file.
        QString candidate_name = safe_id + ".json";
        QFileInfo old_info(old_path);
        if (dir.exists(candidate_name)
            && QFileInfo(dir.filePath(candidate_name)).absoluteFilePath()
               != old_info.absoluteFilePath())
        {
            int counter = 2;
            while (dir.exists(safe_id + " " + QString::number(counter) + ".json")) {
                counter++;
            }
            candidate_name = safe_id + " " + QString::number(counter) + ".json";
        }
        save_path = dir.filePath(candidate_name);
    }

    std::string error;
    if (!::save_sender_profile(updated, save_path.toStdString(), &error)) {
        qWarning("briefutil: failed to save profile '%s': %s",
            qPrintable(save_path),
            error.c_str());
        return false;
    }

    if (id_changed
        && QFileInfo(save_path).absoluteFilePath()
           != QFileInfo(old_path).absoluteFilePath())
    {
        if (!QFile::remove(old_path)) {
            qWarning("briefutil: saved profile to '%s' but could not remove the "
                "previous file '%s'; it may reappear as a duplicate until removed.",
                qPrintable(save_path),
                qPrintable(old_path));
        }
        m_profiles[index].path = save_path;
    }

    m_profiles[index].profile = std::move(updated);
    if (id_changed) {
        emit sender_templates_changed();
    }
    return true;
}

int Proxy::create_new_profile()
{
    QString base_name = "New Profile";
    QDir dir(m_sender_template_dir);

    Sender_profile_entry entry;
    entry.path = dir.filePath(unique_profile_file_name(dir, base_name));
    m_profiles.push_back(std::move(entry));

    emit sender_templates_changed();
    return (int)m_profiles.size() - 1;
}

int Proxy::clone_sender_profile(int index)
{
    if (index < 0 || index >= (int)m_profiles.size()) {
        return -1;
    }

    QDir dir(m_sender_template_dir);
    Sender_profile_entry entry;
    entry.profile = m_profiles[index].profile;

    QString base_id = QString::fromStdString(entry.profile.id).trimmed();
    if (base_id.isEmpty()) {
        base_id = "New Profile";
    }

    QString candidate_id = base_id + " Copy";
    int     counter      = 2;
    while (profile_name_exists(candidate_id, -1)) {
        candidate_id = base_id + " Copy " + QString::number(counter++);
    }
    entry.profile.id = candidate_id.toStdString();
    entry.path = dir.filePath(unique_profile_file_name(dir, candidate_id));

    std::string error;
    if (!::save_sender_profile(entry.profile, entry.path.toStdString(), &error)) {
        qWarning("briefutil: failed to clone profile '%s': %s",
            qPrintable(entry.path),
            error.c_str());
        return -1;
    }

    m_profiles.push_back(std::move(entry));
    emit sender_templates_changed();
    return (int)m_profiles.size() - 1;
}

bool Proxy::delete_sender_profile(int index)
{
    if (index < 0 || index >= (int)m_profiles.size()) {
        return false;
    }

    const QString path = m_profiles[index].path;
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        qWarning("briefutil: failed to delete sender profile '%s'",
            qPrintable(path));
        return false;
    }

    m_profiles.erase(m_profiles.begin() + index);
    emit sender_templates_changed();
    return true;
}

bool Proxy::profile_name_exists(const QString& name, int exclude_index) const
{
    auto trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    for (int i = 0; i < (int)m_profiles.size(); ++i) {
        if (i == exclude_index)                                          { continue;    }
        if (QString::fromStdString(m_profiles[i].profile.id) == trimmed) { return true; }
    }
    return false;
}

bool Proxy::validate_profile_image_name(const QString& v) const
{
    auto trimmed = normalize_asset_name(v);
    if (trimmed.isEmpty()) {
        return true;
    }
    if (!briefutil::is_valid_profile_image_name(trimmed.toStdString())) {
        return false;
    }

    QFileInfo info(QDir(m_sender_template_dir).filePath(trimmed));
    return info.exists() && info.isFile();
}

bool Proxy::validate_hex_color(const QString& v) const
{
    color_t color;
    return parse_hex_color(v, color);
}

QString Proxy::import_template_image(const QUrl& source_url) const
{
    if (!source_url.isLocalFile()) {
        return QString();
    }

    QFileInfo source_info(source_url.toLocalFile());
    if (!source_info.exists() || !source_info.isFile()) {
        return QString();
    }
    if (!source_info.fileName().endsWith(".png", Qt::CaseInsensitive)) {
        return QString();
    }

    QDir dir(m_sender_template_dir);

    // Re-importing a file already inside the template directory is a no-op.
    if (QFileInfo(dir.filePath(source_info.fileName())).absoluteFilePath()
        == source_info.absoluteFilePath())
    {
        return source_info.fileName();
    }

    // Never overwrite an existing asset; other profiles may reference it by
    // name. The copy is staged in briefutil's own directory and handed over
    // with a non-replacing rename, so a failed copy can neither destroy an
    // existing asset nor leave a partial file under the final name, and nothing
    // in the user's template directory is removed to make room for it.
    const QString target_name = unique_image_file_name(dir, source_info.fileName());
    const QString target_path = dir.filePath(target_name);

    briefutil::Owned_staging_slot staging;
    if (!staging.open(dir, target_name, nullptr)) {
        return QString();
    }
    if (!QFile::copy(source_info.absoluteFilePath(), staging.staged_path())) {
        return QString();
    }
    if (briefutil::publish_staged_file(staging.staged_path(), target_path, false, nullptr) !=
        briefutil::Publish_outcome::PUBLISHED)
    {
        return QString();
    }
    return target_name;
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
    HWND            hwnd                            = reinterpret_cast<HWND>(window->winId());
    BOOL            useDarkMode                     = dark ? TRUE : FALSE;
    constexpr DWORD k_dwmwa_use_immersive_dark_mode = 20;
    DwmSetWindowAttribute(hwnd, k_dwmwa_use_immersive_dark_mode,
        &useDarkMode, sizeof(useDarkMode));
#else
    Q_UNUSED(window);
    Q_UNUSED(dark);
#endif
}

void Proxy::save_dark_mode(bool dark)
{
    m_dark_mode = dark;
    save_settings();
}

bool Proxy::load_dark_mode() const
{
    return m_dark_mode;
}

QString Proxy::get_build_caption() const
{
    return m_build_caption;
}

QString Proxy::get_build_details() const
{
    return m_build_details;
}
