#include "proxy.h"
#include "default_profiles.h"
#include "briefutil/letter_builder.h"
#include "mustermann_signature.png.h"
#include "briefutil/sender_profile.h"

#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QHash>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
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
    return QDir::homePath() + "/briefutil/templates/";
}

static bool is_valid_font_config(const Font_family_config& fc)
{
    return !fc.sans.empty()
        && !fc.sans_bold.empty()
        && !fc.sans_italic.empty()
        && !fc.sans_bold_italic.empty()
        && !fc.mono.empty();
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

static QString join_lines(const std::vector<std::string>& lines)
{
    QStringList result;
    for (const auto& line : lines) {
        result.push_back(QString::fromStdString(line));
    }
    return result.join('\n');
}

static std::vector<std::string> split_lines(const QString& text)
{
    std::vector<std::string> result;
    for (const auto& line : text.split('\n')) {
        auto trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            result.push_back(trimmed.toStdString());
        }
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

static void append_windows_registry_fonts(QHash<QString, QString>& map,
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
            candidates << (base + " Bold")
                       << (base + " DemiBold")
                       << (base + " Semibold")
                       << (base + " SemiBold")
                       << (base + " Demi Bold")
                       << (base + " Medium");
            break;
        case Font_role::SANS_ITALIC:
            candidates << (base + " Italic")
                       << (base + " Oblique");
            break;
        case Font_role::SANS_BOLD_ITALIC:
            candidates << (base + " Bold Italic")
                       << (base + " BoldItalic")
                       << (base + " Bold Oblique")
                       << (base + " Semibold Italic")
                       << (base + " SemiBold Italic")
                       << (base + " Demi Bold Italic")
                       << (base + " Medium Italic");
            break;
        case Font_role::SANS:
        case Font_role::MONO:
            candidates << (base + " Regular")
                       << (base + " Roman")
                       << (base + " Book");
            break;
        case Font_role::ANY:
            candidates << (base + " Regular")
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

static Font_family_config font_config_from_inputs(const QString& sans, const QString& sans_bold,
                                                  const QString& sans_italic, const QString& sans_bold_italic,
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

void Proxy::make_pdf(int from, const QString& to,
                     const QString& subject, const QString& body)
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
            "Invalid font configuration. Use built-in PDF fonts such as Helvetica or Courier, installed fonts such as Noto Sans, or .ttf/.otf font files.");
        return;
    }

    auto result = generate_letter_pdf(profile, input,
                                      m_sender_template_dir.toStdString(),
                                      pdf_path.toUtf8().toStdString(),
                                      m_theme);

    if (!result.ok) {
        emit pdf_generated(false,
            QString::fromStdString(result.message.empty()
                ? result.detail : result.message));
        return;
    }

    qInfo("briefutil: native PDF generated in %lld ms", timer.elapsed());

    if (!open_generated_pdf(pdf_path)) {
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

QString Proxy::get_font_sans() const             { return m_font_sans_input; }
QString Proxy::get_font_sans_bold() const        { return m_font_sans_bold_input; }
QString Proxy::get_font_sans_italic() const      { return m_font_sans_italic_input; }
QString Proxy::get_font_sans_bold_italic() const { return m_font_sans_bold_italic_input; }
QString Proxy::get_font_mono() const             { return m_font_mono_input; }
double  Proxy::get_body_size() const             { return m_theme.typo.body_size_pt; }
double  Proxy::get_body_leading() const          { return m_theme.typo.body_lead_pt; }
QString Proxy::get_template_dir() const          { return m_sender_template_dir; }

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
    discover_profiles();
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
    result.insert("returnAddressLine", QString::fromStdString(profile.return_address_line));
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

    Sender_profile updated = m_profiles[index].profile;

    auto id = profile_data.value("id").toString().trimmed();
    if (id.isEmpty()) {
        return false;
    }
    updated.id = id.toStdString();

    auto style = profile_data.value("style").toString().trimmed().toLower();
    updated.style = style == "commercial"
        ? Profile_style::COMMERCIAL : Profile_style::SIMPLE;

    updated.sender_lines = split_lines(profile_data.value("senderLines").toString());
    updated.email = profile_data.value("email").toString().trimmed().toStdString();
    updated.return_address_line = profile_data.value("returnAddressLine").toString().trimmed().toStdString();
    updated.signer_name = profile_data.value("signerName").toString().trimmed().toStdString();

    auto signature_image = normalize_asset_name(profile_data.value("signatureImage").toString());
    if (!validate_profile_image_name(signature_image)) {
        return false;
    }
    updated.signature_image = signature_image.toStdString();

    auto logo_image = normalize_asset_name(profile_data.value("logoImage").toString());
    if (!validate_profile_image_name(logo_image)) {
        return false;
    }
    updated.logo_image = logo_image.toStdString();
    updated.signer_title = profile_data.value("signerTitle").toString().trimmed().toStdString();
    updated.footer_lines = split_lines(profile_data.value("footerLines").toString());

    color_t top_rule_color;
    if (!parse_hex_color(profile_data.value("topRuleColor").toString(), top_rule_color)) {
        return false;
    }
    updated.top_rule_color = top_rule_color;

    std::string error;
    if (!::save_sender_profile(updated, m_profiles[index].path.toStdString(), &error)) {
        qWarning("briefutil: failed to save profile '%s': %s",
                 qPrintable(m_profiles[index].path),
                 error.c_str());
        return false;
    }

    const bool id_changed = updated.id != m_profiles[index].profile.id;
    m_profiles[index].profile = std::move(updated);
    if (id_changed) {
        emit sender_templates_changed();
    }
    return true;
}

int Proxy::create_new_profile()
{
    QString base_name = "New Profile";
    QString file_name = base_name + ".json";
    QDir dir(m_sender_template_dir);
    int counter = 2;
    while (dir.exists(file_name)) {
        file_name = base_name + " " + QString::number(counter++) + ".json";
    }

    Sender_profile_entry entry;
    entry.path = dir.filePath(file_name);
    m_profiles.push_back(std::move(entry));

    emit sender_templates_changed();
    return (int)m_profiles.size() - 1;
}

bool Proxy::delete_sender_profile(int index)
{
    if (index < 0 || index >= (int)m_profiles.size()) {
        return false;
    }

    QFile::remove(m_profiles[index].path);
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
    QSettings settings("briefutil", "briefutil");
    settings.setValue("appearance/darkMode", dark);
}

bool Proxy::load_dark_mode() const
{
    QSettings settings("briefutil", "briefutil");
    return settings.value("appearance/darkMode", false).toBool();
}
