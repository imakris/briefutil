#include "briefutil/template_store.h"

#include "briefutil/default_profiles.h"
#include "briefutil/owned_staging.h"
#include "briefutil/path_utils.h"
#include "mustermann_signature.png.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLatin1StringView>
#include <QLockFile>
#include <QStandardPaths>

#include <array>
#include <cstddef>
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

// Staging happens in briefutil's own directory rather than beside the user's
// templates. That is what lets the directory scan below decide what briefutil
// owns by exact name: there is no unpredictable sibling of a template left in
// the user's directory for it to have to recognize.
static bool write_file_if_missing(
    const QString& path,
    const char*    data,
    size_t         size,
    std::string*   error)
{
    if (QFileInfo::exists(path)) {
        return true;
    }

    const QFileInfo    target(path);
    Owned_staging_slot staging;
    if (!staging.open(target.absoluteDir(), target.fileName(), error)) {
        return false;
    }

    QFile file(staging.staged_path());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString().toStdString();
        }
        return false;
    }
    if (file.write(data, static_cast<qint64>(size)) != static_cast<qint64>(size) ||
        !file.flush())
    {
        if (error) {
            *error = file.errorString().toStdString();
        }
        return false;
    }
    file.close();

    // TARGET_EXISTS is the same judgment as the exists() check above: what this
    // owes the caller is that the file is there, and a non-replacing publish
    // that lost to whoever created it in the meantime has still delivered that.
    std::string publish_detail;
    switch (publish_staged_file(staging.staged_path(), path, false, &publish_detail)) {
        case Publish_outcome::PUBLISHED:
        case Publish_outcome::TARGET_EXISTS: return true;
        case Publish_outcome::FAILED:
        default:                             break;
    }
    if (error) {
        *error = publish_detail;
    }
    return false;
}

struct Seeded_template
{
    const char*  name;
    const char*  data;
    std::size_t  size;
};

// The files a seed writes, in the order it writes them: the signature image
// lands before the profiles that reference it, so a reader scanning the
// directory mid-seed can never load a profile whose image is still missing.
static std::array<Seeded_template, 3> seeded_templates()
{
    const auto signature = mustermann_signature_png::data();
    return {{
        { "mustermann_signature.png",
          reinterpret_cast<const char*>(signature.first),
          signature.second },
        { "Max Mustermann.json",
          k_default_profile_simple_json,
          std::strlen(k_default_profile_simple_json) },
        { "Max Mustermann, Mustermann AG.json",
          k_default_profile_commercial_json,
          std::strlen(k_default_profile_commercial_json) },
    }};
}

// Everything briefutil puts in a template directory has one fixed name, and its
// working state lives in one directory it creates itself, so this list is
// complete by construction. Every other entry is the user's, including one whose
// name merely resembles something briefutil writes: a name is not evidence of
// who wrote it, and an entry briefutil cannot account for belongs to the user.
static bool is_own_entry(const QString& entry)
{
    for (const char* own_name : {
             k_template_init_marker,
             k_template_seed_lock,
             k_owned_staging_dir })
    {
        if (entry == QLatin1StringView(own_name)) {
            return true;
        }
    }
    for (const auto& seeded : seeded_templates()) {
        if (entry == QLatin1StringView(seeded.name)) {
            return true;
        }
    }
    return false;
}

// True when the directory holds anything briefutil did not put there, which is
// what decides whether the defaults are seeded into it at all.
//
// Bare non-emptiness cannot decide that. A run killed mid-seed leaves some of
// the seeded files behind, and reading those as content the user supplied makes
// the next run skip seeding and publish the initialization marker over a
// directory that is missing the profiles. Seeding has exactly one caller and the
// marker is the only gate in front of it, so that state is permanent.
static bool template_dir_has_user_content(const QDir& templates_dir)
{
    const auto entries = templates_dir.entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const auto& entry : entries) {
        if (!is_own_entry(entry)) {
            return true;
        }
    }
    return false;
}

// Idempotent: each file is written only if it is missing, so this both seeds a
// fresh directory and completes one an earlier run abandoned.
static bool seed_default_templates(QDir& templates_dir, std::string* error)
{
    for (const auto& seeded : seeded_templates()) {
        if (!write_file_if_missing(
                templates_dir.filePath(QLatin1StringView(seeded.name)),
                seeded.data,
                seeded.size,
                error))
        {
            return false;
        }
    }
    return true;
}

bool ensure_template_dir_ready(const std::string& dir_path, std::string* error)
{
    QDir templates_dir(QString::fromStdString(dir_path));
    if (!templates_dir.exists() && !templates_dir.mkpath(".")) {
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

    // The marker says the directory is ready, so it is published only once this
    // run has established that: either it seeded the directory itself, or the
    // directory already held content of the user's own that must be left alone.
    if (!template_dir_has_user_content(templates_dir) &&
        !seed_default_templates(templates_dir, error))
    {
        return false;
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
