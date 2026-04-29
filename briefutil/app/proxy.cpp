#include "proxy.h"
#include "briefutil/default_profiles.h"
#include "briefutil/letter_builder.h"
#include "briefutil/localization.h"
#include "briefutil/pdf_backend.h"
#include "mustermann_signature.png.h"
#include "briefutil/sender_profile.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLocale>
#include <QHash>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
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
#include <cstring>


// ============================================================================
// Settings persistence
// ============================================================================

static QString default_sender_template_dir()
{
    QString base_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base_dir.isEmpty()) {
        base_dir = QDir::homePath() + "/.local/share/briefutil";
    }

    QString path = QDir::fromNativeSeparators(QDir(base_dir).filePath("templates"));
    if (!path.endsWith('/')) {
        path += '/';
    }
    return path;
}

static QString normalize_profile_language(const QString& language)
{
    const QString normalized = language.trimmed().toLower();
    if (normalized == "de"
        || normalized == "de-de"
        || normalized == "german"
        || normalized == "deutsch")
    {
        return "de";
    }
    return "en";
}

static QLocale qlocale_for_profile_language(const QString& language)
{
    if (normalize_profile_language(language) == "de") {
        return QLocale(QLocale::German, QLocale::Germany);
    }
    return QLocale(QLocale::English, QLocale::UnitedStates);
}

static bool is_valid_font_config(const font_family_config_t& fc)
{
    return !fc.sans.empty()
        && !fc.sans_bold.empty()
        && !fc.sans_italic.empty()
        && !fc.sans_bold_italic.empty()
        && !fc.mono.empty();
}

static QString normalize_layout_preset(QString preset)
{
    preset = preset.trimmed().toLower();
    if (preset == "din_5008_form_a" || preset == "us_letter") {
        return preset;
    }
    return "din_5008_form_b";
}

static void ensure_template_dir_ready(const QString& dir_path)
{
    QDir templates_dir(dir_path);
    if (!templates_dir.exists()) {
        templates_dir.mkpath(".");
    }

    auto write_file_if_missing = [&](
        const QString& path,
        const char* data,
        size_t size) {
        QFileInfo info(path);
        if (info.exists()) {
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return;
        }
        file.write(data, (qint64)size);
    };

    write_file_if_missing(
        dir_path + "Max Mustermann.json",
        k_default_profile_simple_json,
        std::strlen(k_default_profile_simple_json));
    write_file_if_missing(
        dir_path + "Max Mustermann, Mustermann AG.json",
        k_default_profile_commercial_json,
        std::strlen(k_default_profile_commercial_json));
    write_file_if_missing(
        dir_path + "mustermann_signature.png",
        (const char*)mustermann_signature_png::data().first,
        mustermann_signature_png::data().second);
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
    if (!match.hasMatch()) return false;

    auto hex = match.captured(1);
    bool ok_r = false;
    bool ok_g = false;
    bool ok_b = false;
    int r = hex.mid(0, 2).toInt(&ok_r, 16);
    int g = hex.mid(2, 2).toInt(&ok_g, 16);
    int b = hex.mid(4, 2).toInt(&ok_b, 16);
    if (!ok_r || !ok_g || !ok_b) return false;

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
    if (normalized == "sans") return Font_role::SANS;
    if (normalized == "sans_bold") return Font_role::SANS_BOLD;
    if (normalized == "sans_italic") return Font_role::SANS_ITALIC;
    if (normalized == "sans_bold_italic") return Font_role::SANS_BOLD_ITALIC;
    if (normalized == "mono") return Font_role::MONO;
    return Font_role::ANY;
}

static bool is_base14_font_name(const std::string& s)
{
    static const char* k_base14[] = {
        "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique",
        "Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique",
        "Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic",
        "Symbol", "ZapfDingbats"
    };
    for (const auto* name : k_base14) {
        if (s == name) return true;
    }
    return false;
}

static bool is_supported_font_file_path(const QString& path)
{
    auto suffix = QFileInfo(path).suffix().toLower();
    return suffix == "ttf" || suffix == "otf";
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
        if (path.isEmpty()) return;
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
    if (path.isEmpty()) return QString();

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
    QHash<QString, QString>& map,
    const QString& registry_path,
    const QStringList& base_dirs)
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
    if (base.isEmpty()) return candidates;

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
    const auto& fonts = installed_windows_font_files();
    auto candidates = font_name_candidates(family, role);
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
    if (trimmed.isEmpty()) return QString();

    auto s = trimmed.toStdString();
    if (looks_like_font_file(s)) {
        return QFileInfo(trimmed).exists() ? QFileInfo(trimmed).absoluteFilePath() : QString();
    }
    if (is_base14_font_name(s)) {
        return trimmed;
    }

#ifdef Q_OS_WIN
    return resolve_windows_system_font(trimmed, role);
#else
    Q_UNUSED(role);
    return QString();
#endif
}

static font_family_config_t font_config_from_inputs(
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

    auto def = default_font_family();
    m_font_sans_input = s.value("fonts/sans",             QString::fromStdString(def.sans)).toString();
    m_font_sans_bold_input = s.value("fonts/sans_bold",        QString::fromStdString(def.sans_bold)).toString();
    m_font_sans_italic_input = s.value("fonts/sans_italic",      QString::fromStdString(def.sans_italic)).toString();
    m_font_sans_bold_italic_input = s.value("fonts/sans_bold_italic", QString::fromStdString(def.sans_bold_italic)).toString();
    m_font_mono_input = s.value("fonts/mono",             QString::fromStdString(def.mono)).toString();
    m_theme.fonts = font_config_from_inputs(
        m_font_sans_input,
        m_font_sans_bold_input,
        m_font_sans_italic_input,
        m_font_sans_bold_italic_input,
        m_font_mono_input);

    auto def_typo = default_typography();
    m_theme.typo.body_size_pt = s.value("typo/body_size",    def_typo.body_size_pt).toFloat();
    m_theme.typo.body_lead_pt = s.value("typo/body_leading", def_typo.body_lead_pt).toFloat();

    QString saved_dir = s.value("paths/template_dir").toString();
    if (!saved_dir.isEmpty()) {
        m_sender_template_dir = saved_dir;
    }

    m_layout_preset = normalize_layout_preset(
        s.value("layout/preset", m_layout_preset).toString());
    m_dark_mode = s.value("appearance/darkMode", false).toBool();
}

void Proxy::save_settings() const
{
    QSettings s("briefutil", "briefutil");

    s.setValue("fonts/sans",             m_font_sans_input);
    s.setValue("fonts/sans_bold",        m_font_sans_bold_input);
    s.setValue("fonts/sans_italic",      m_font_sans_italic_input);
    s.setValue("fonts/sans_bold_italic", m_font_sans_bold_italic_input);
    s.setValue("fonts/mono",             m_font_mono_input);
    s.setValue("typo/body_size",         (double)m_theme.typo.body_size_pt);
    s.setValue("typo/body_leading",      (double)m_theme.typo.body_lead_pt);
    s.setValue("paths/template_dir",     m_sender_template_dir);
    s.setValue("layout/preset",          m_layout_preset);
    s.setValue("appearance/darkMode",    m_dark_mode);
}

localization_t Proxy::current_localization(const sender_profile_t& profile) const
{
    if (normalize_profile_language(QString::fromStdString(profile.language)) == "de") {
        return german_localization();
    }
    return english_localization();
}

letter_layout_spec_t Proxy::current_layout_spec() const
{
    if (m_layout_preset == "din_5008_form_a") {
        return din_5008_form_a();
    }
    if (m_layout_preset == "us_letter") {
        return us_letter();
    }
    return din_5008_form_b();
}


// ============================================================================
// Construction and profile discovery
// ============================================================================

Proxy::Proxy(QObject*)
{
    // Output directory. Prefer a portable-launcher root when present, then
    // look next to the executable, then in the current working directory.
    auto read_dir_conf = [](const QString& path) -> QString {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return QString();
        return QString::fromUtf8(f.readAll()).trimmed();
    };

    QString output_dir;
    const QString portable_root = qEnvironmentVariable("BRIEFUTIL_PORTABLE_ROOT");
    if (!portable_root.isEmpty()) {
        output_dir = read_dir_conf(
            QDir::fromNativeSeparators(QDir(portable_root).filePath("output_dir.conf")));
    }
    if (output_dir.isEmpty()) {
        output_dir = read_dir_conf(
            QCoreApplication::applicationDirPath() + "/output_dir.conf");
    }
    if (output_dir.isEmpty()) {
        output_dir = read_dir_conf(
            QDir::current().filePath("output_dir.conf"));
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
        connect(m_template_watcher, &QFileSystemWatcher::directoryChanged,
                this, [this](const QString&) {
            if (m_discover_timer) m_discover_timer->start();
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
    QDir templates_dir(m_sender_template_dir);
    const auto profile_files = templates_dir.entryList({ "*.json" }, QDir::Files, QDir::Name);
    for (const auto& profile_file : profile_files) {
        const auto profile_path = templates_dir.filePath(profile_file);
        auto result = load_sender_profile(profile_path.toStdString());
        if (result.ok) {
            m_profiles.push_back({ std::move(result.profile), profile_path });
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
    QString sanitized = input.trimmed();
    sanitized.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1F]"), "_");
    return sanitized;
}

static QString unique_profile_file_name(const QDir& dir, const QString& base_name)
{
    QString stem = sanitize_filename(base_name);
    if (stem.isEmpty()) {
        stem = "profile";
    }

    QString candidate = stem + ".json";
    int counter = 2;
    while (dir.exists(candidate)) {
        candidate = stem + " " + QString::number(counter++) + ".json";
    }
    return candidate;
}

static bool open_generated_pdf(const QString& pdf_path)
{
#ifdef Q_OS_WIN
    auto result = reinterpret_cast<qintptr>(ShellExecuteW(
        nullptr,
        L"open",
        reinterpret_cast<LPCWSTR>(pdf_path.utf16()),
        nullptr,
        nullptr,
        SW_SHOWNORMAL));
    return result > 32;
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(pdf_path));
#endif
}

void Proxy::make_pdf(
    int from,
    const QString& to,
    const QString& subject,
    const QString& body)
{
    if (from < 0 || from >= (int)m_profiles.size()) {
        emit pdf_generated(false, "Invalid sender profile selection.");
        return;
    }

    const auto& profile = m_profiles[from].profile;

    QString prefix = QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss") + " ";
    QString filename_slug = sanitize_filename(subject);
    if (filename_slug.isEmpty()) {
        filename_slug = "letter";
    }

    QString pdf_filename = prefix + filename_slug + ".pdf";
    pdf_filename.replace(QRegularExpression("\\s+"), " ");
    QString pdf_path = m_output_dir + "/" + pdf_filename;

    QString date_str = qlocale_for_profile_language(
        QString::fromStdString(profile.language)).toString(
            QDate::currentDate(),
            QLocale::LongFormat);

    QElapsedTimer timer;
    timer.start();

    letter_input_t input;
    input.recipient = to.toStdString();
    input.subject   = subject.toStdString();
    input.body      = body.toStdString();
    input.date      = date_str.toStdString();

    if (!is_valid_font_config(m_theme.fonts)) {
        emit pdf_generated(false,
            "Invalid font configuration. Use built-in PDF fonts such as Helvetica or Courier, installed fonts such as Noto Sans, or .ttf/.otf font files.");
        return;
    }

    auto loc = current_localization(profile);
    auto layout = current_layout_spec();
    auto backend = Pdf_backend::Haru;
    if (!pdf_backend_available(backend)) {
        emit pdf_generated(
            false,
            "Selected PDF backend is not available in this build.");
        return;
    }

    auto result = generate_letter_pdf(
        profile,
        input,
        m_sender_template_dir.toStdString(),
        pdf_path.toUtf8().toStdString(),
        m_theme,
        layout,
        loc,
        backend);

    if (!result.ok) {
        emit pdf_generated(false,
            QString::fromStdString(result.message.empty()
                ? result.detail : result.message));
        return;
    }

    qInfo("briefutil: native PDF generated in %lld ms", timer.elapsed());

    if (!open_generated_pdf(pdf_path)) {
        emit pdf_generated(
            false,
            QString::fromStdString(
                format_pdf_open_failed(
                    loc.error_pdf_open_failed_format,
                    pdf_path.toUtf8().toStdString())));
        return;
    }

    emit pdf_generated(true, QString());
}


// ============================================================================
// Settings accessors
// ============================================================================

QString Proxy::get_font_sans() const             { return m_font_sans_input; }
QString Proxy::get_font_sans_bold() const        { return m_font_sans_bold_input; }
QString Proxy::get_font_sans_italic() const      { return m_font_sans_italic_input; }
QString Proxy::get_font_sans_bold_italic() const { return m_font_sans_bold_italic_input; }
QString Proxy::get_font_mono() const             { return m_font_mono_input; }
double  Proxy::get_body_size() const             { return m_theme.typo.body_size_pt; }
double  Proxy::get_body_leading() const          { return m_theme.typo.body_lead_pt; }
QString Proxy::get_template_dir() const          { return m_sender_template_dir; }
QString Proxy::get_layout_preset() const         { return m_layout_preset; }

void Proxy::update_font_and_save(QString& slot, const QString& v)
{
    slot = v.trimmed();
    m_theme.fonts = font_config_from_inputs(
        m_font_sans_input, m_font_sans_bold_input, m_font_sans_italic_input,
        m_font_sans_bold_italic_input, m_font_mono_input);
    save_settings();
}

void Proxy::set_font_sans(const QString& v)             { update_font_and_save(m_font_sans_input, v); }
void Proxy::set_font_sans_bold(const QString& v)        { update_font_and_save(m_font_sans_bold_input, v); }
void Proxy::set_font_sans_italic(const QString& v)      { update_font_and_save(m_font_sans_italic_input, v); }
void Proxy::set_font_sans_bold_italic(const QString& v)  { update_font_and_save(m_font_sans_bold_italic_input, v); }
void Proxy::set_font_mono(const QString& v)             { update_font_and_save(m_font_mono_input, v); }

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
    if (trimmed.isEmpty()) return false;

    auto s = trimmed.toStdString();
    if (looks_like_font_file(s)) {
        return QFileInfo(trimmed).exists() && is_supported_font_file_path(trimmed);
    }
    if (is_base14_font_name(s)) return true;
    return !resolve_font_value(trimmed, font_role_from_string(role)).isEmpty();
}

bool Proxy::font_value_is_file_backed(const QString& v, const QString& role) const
{
    auto resolved = resolve_font_value(v, font_role_from_string(role));
    return !resolved.isEmpty() && looks_like_font_file(resolved.toStdString());
}

bool Proxy::validate_directory(const QString& v) const
{
    auto trimmed = v.trimmed();
    if (trimmed.isEmpty()) return true;

    if (!QDir::isAbsolutePath(trimmed)) return false;

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
    result.insert("id", QString::fromStdString(profile.id));
    result.insert("style", profile.style == Profile_style::COMMERCIAL ? "commercial" : "simple");
    result.insert("senderLines", join_lines(profile.sender_lines));
    result.insert("email", QString::fromStdString(profile.email));
    result.insert("language", normalize_profile_language(QString::fromStdString(profile.language)));
    result.insert("returnAddressLine", QString::fromStdString(profile.return_address_line));
    result.insert("closingPhrase", QString::fromStdString(profile.closing_phrase));
    result.insert("signerName", QString::fromStdString(profile.signer_name));
    result.insert("signatureImage", QString::fromStdString(profile.signature_image));
    result.insert("logoImage", QString::fromStdString(profile.logo_image));
    result.insert("topRuleColor", color_to_hex(profile.top_rule_color));
    result.insert("footerLines", join_lines(profile.footer_lines));
    result.insert("signerTitle", QString::fromStdString(profile.signer_title));
    return result;
}

bool Proxy::save_sender_profile(int index, const QVariantMap& profile_data)
{
    if (index < 0 || index >= (int)m_profiles.size()) {
        qWarning("briefutil: cannot save sender profile: invalid index %d", index);
        return false;
    }

    sender_profile_t updated = m_profiles[index].profile;

    auto id = profile_data.value("id").toString().trimmed();
    if (id.isEmpty()) {
        return false;
    }
    updated.id = id.toStdString();

    auto style = profile_data.value("style").toString().trimmed().toLower();
    updated.style = style == "commercial"
        ? Profile_style::COMMERCIAL : Profile_style::SIMPLE;

    updated.sender_lines = split_profile_lines(profile_data.value("senderLines").toString());
    updated.email = profile_data.value("email").toString().trimmed().toStdString();
    updated.language = normalize_profile_language(profile_data.value("language").toString()).toStdString();
    updated.return_address_line = profile_data.value("returnAddressLine").toString().trimmed().toStdString();
    updated.closing_phrase = profile_data.value("closingPhrase").toString().trimmed().toStdString();
    updated.signer_name = profile_data.value("signerName").toString().trimmed().toStdString();

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
    updated.logo_image = logo_image.toStdString();

    // If the id changed, rename the backing JSON file as well so that the
    // template directory stays consistent with the in-memory profile list.
    const QString old_path = m_profiles[index].path;
    QString save_path = old_path;
    const bool id_changed = updated.id != m_profiles[index].profile.id;
    if (id_changed) {
        QString safe_id = sanitize_filename(QString::fromStdString(updated.id));
        if (safe_id.isEmpty()) safe_id = "profile";
        QDir dir(m_sender_template_dir);

        // Don't clobber another profile's file.
        QString candidate_name = safe_id + ".json";
        QFileInfo old_info(old_path);
        if (dir.exists(candidate_name)
            && QFileInfo(dir.filePath(candidate_name)).absoluteFilePath()
               != old_info.absoluteFilePath()) {
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
        QFile::remove(old_path);
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

    sender_profile_entry_t entry;
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
    sender_profile_entry_t entry;
    entry.profile = m_profiles[index].profile;

    QString base_id = QString::fromStdString(entry.profile.id).trimmed();
    if (base_id.isEmpty()) {
        base_id = "New Profile";
    }

    QString candidate_id = base_id + " Copy";
    int counter = 2;
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
    if (trimmed.isEmpty()) return false;

    for (int i = 0; i < (int)m_profiles.size(); ++i) {
        if (i == exclude_index) continue;
        if (QString::fromStdString(m_profiles[i].profile.id) == trimmed) {
            return true;
        }
    }
    return false;
}

bool Proxy::validate_profile_image_name(const QString& v) const
{
    auto trimmed = normalize_asset_name(v);
    if (trimmed.isEmpty()) return true;
    if (QDir::isAbsolutePath(trimmed)) return false;
    if (trimmed.startsWith("../") || trimmed.contains("/../") || trimmed == "..") return false;
    if (!trimmed.endsWith(".png", Qt::CaseInsensitive)) return false;

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

    auto target_name = source_info.fileName();
    auto target_path = QDir(m_sender_template_dir).filePath(target_name);
    if (QFileInfo(target_path).absoluteFilePath() != source_info.absoluteFilePath()) {
        QFile::remove(target_path);
        if (!QFile::copy(source_info.absoluteFilePath(), target_path)) {
            return QString();
        }
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
    m_dark_mode = dark;
    save_settings();
}

bool Proxy::load_dark_mode() const
{
    return m_dark_mode;
}
