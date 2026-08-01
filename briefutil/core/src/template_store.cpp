#include "briefutil/template_store.h"

#include "briefutil/default_profiles.h"
#include "briefutil/path_utils.h"
#include "mustermann_signature.png.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLatin1StringView>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>

#include <cstring>

namespace briefutil {

static constexpr const char* k_template_init_marker = ".briefutil_templates_initialized";
static constexpr const char* k_template_seed_lock   = ".briefutil_templates_seeding";

// Seeding writes three small files, so a run that has to wait is waiting for
// another briefutil process that is about to finish.
static constexpr int k_template_seed_lock_timeout_ms = 10000;

static std::string with_trailing_slash(QString path)
{
    path = QDir::fromNativeSeparators(path);
    if (!path.endsWith('/')) {
        path += '/';
    }
    return path.toStdString();
}

std::string default_template_dir()
{
    const QString env_dir = qEnvironmentVariable("BRIEFUTIL_TEMPLATE_DIR");
    if (!env_dir.isEmpty()) {
        return with_trailing_slash(env_dir);
    }

    QString base_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base_dir.isEmpty()) {
        base_dir = QDir::homePath() + "/.local/share/briefutil";
    }
    return with_trailing_slash(QDir(base_dir).filePath("templates"));
}

std::string default_output_dir()
{
    return with_trailing_slash(QDir::home().filePath("briefutil/output"));
}

std::string read_output_dir_conf(const std::string& path)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).trimmed().toStdString();
}

std::string configured_output_dir(
    const std::string& application_dir,
    const std::string& current_dir)
{
    const QString env_output_dir = qEnvironmentVariable("BRIEFUTIL_OUTPUT_DIR");
    if (!env_output_dir.isEmpty()) {
        return with_trailing_slash(env_output_dir);
    }

    std::string output_dir;
    const QString portable_root = qEnvironmentVariable("BRIEFUTIL_PORTABLE_ROOT");
    if (!portable_root.isEmpty()) {
        output_dir = read_output_dir_conf(
            QDir(portable_root).filePath("output_dir.conf").toStdString());
    }
    if (output_dir.empty()) { output_dir = read_output_dir_conf(join_path(application_dir, "output_dir.conf")); }
    if (output_dir.empty()) { output_dir = read_output_dir_conf(join_path(current_dir, "output_dir.conf"));     }

    if (!output_dir.empty() && QDir(QString::fromStdString(output_dir)).exists()) {
        return with_trailing_slash(QString::fromStdString(output_dir));
    }
    return default_output_dir();
}

static bool write_file_if_missing(
    const QString& path,
    const char*    data,
    size_t         size,
    std::string*   error)
{
    if (QFileInfo::exists(path)) {
        return true;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString().toStdString();
        }
        return false;
    }
    if (file.write(data, static_cast<qint64>(size)) != static_cast<qint64>(size)) {
        if (error) {
            *error = file.errorString().toStdString();
        }
        return false;
    }
    if (!file.commit()) {
        if (error) {
            *error = file.errorString().toStdString();
        }
        return false;
    }
    return true;
}

static bool template_dir_has_entries(const QDir& templates_dir)
{
    const auto entries = templates_dir.entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const auto& entry : entries) {
        // briefutil's own bookkeeping is not user content. The seed lock in
        // particular is present while this very check runs.
        if (entry != QLatin1StringView(k_template_init_marker) &&
            entry != QLatin1StringView(k_template_seed_lock))
        {
            return true;
        }
    }
    return false;
}

static bool seed_default_templates(QDir& templates_dir, std::string* error)
{
    // The signature image lands before the profiles that reference it, so a
    // reader scanning the directory mid-seed can never load a profile whose
    // image is still missing.
    return
        write_file_if_missing(
            templates_dir.filePath("mustermann_signature.png"),
            reinterpret_cast<const char*>(mustermann_signature_png::data().first),
            mustermann_signature_png::data().second,
            error)
        &&
        write_file_if_missing(
            templates_dir.filePath("Max Mustermann.json"),
            k_default_profile_simple_json,
            std::strlen(k_default_profile_simple_json),
            error)
        &&
        write_file_if_missing(
            templates_dir.filePath("Max Mustermann, Mustermann AG.json"),
            k_default_profile_commercial_json,
            std::strlen(k_default_profile_commercial_json),
            error);
}

bool ensure_template_dir_ready(const std::string& dir_path, std::string* error)
{
    QDir templates_dir(QString::fromStdString(dir_path));
    const bool dir_existed = templates_dir.exists();
    if (!dir_existed && !templates_dir.mkpath(".")) {
        if (error) {
            *error = "Could not create template directory.";
        }
        return false;
    }

    const auto marker_path = templates_dir.filePath(k_template_init_marker);
    if (QFileInfo::exists(marker_path)) {
        return true;
    }

    // Only one process may seed a given directory. Without this, two runs both
    // observe an unseeded directory and both write into it, and the caller of
    // the losing run is told the directory is ready while it is still being
    // filled in. The marker is re-read under the lock because the winner may
    // have finished between the check above and the lock being granted.
    QLockFile seed_lock(templates_dir.filePath(k_template_seed_lock));
    seed_lock.setStaleLockTime(0);
    if (!seed_lock.tryLock(k_template_seed_lock_timeout_ms)) {
        if (error) {
            *error = "Another briefutil run is initializing the template directory.";
        }
        return false;
    }
    if (QFileInfo::exists(marker_path)) {
        return true;
    }

    if (!dir_existed || !template_dir_has_entries(templates_dir)) {
        if (!seed_default_templates(templates_dir, error)) {
            return false;
        }
    }

    return write_file_if_missing(marker_path, "", 0, error);
}

std::vector<Profile_entry> discover_profiles(
    const std::string&         template_dir,
    std::vector<std::string>*  errors)
{
    std::vector<Profile_entry> profiles;
    QDir dir(QString::fromStdString(template_dir));
    const auto profile_files = dir.entryList({ "*.json" }, QDir::Files, QDir::Name);
    for (const auto& profile_file : profile_files) {
        const auto profile_path = dir.filePath(profile_file);
        auto       result       = load_sender_profile(profile_path.toStdString());
        if (result.ok) {
            profiles.push_back({
                std::move(result.profile),
                profile_path.toStdString(),
                QFileInfo(profile_path).absoluteDir().absolutePath().toStdString(),
            });
        }
        else
        if (errors) {
            errors->push_back(profile_file.toStdString() + ": " + result.error);
        }
    }
    return profiles;
}

} // namespace briefutil
