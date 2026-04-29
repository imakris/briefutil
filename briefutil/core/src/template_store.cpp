#include "briefutil/template_store.h"

#include "briefutil/default_profiles.h"
#include "briefutil/path_utils.h"
#include "mustermann_signature.png.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <cstring>

namespace briefutil {

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
    if (output_dir.empty()) {
        output_dir = read_output_dir_conf(join_path(application_dir, "output_dir.conf"));
    }
    if (output_dir.empty()) {
        output_dir = read_output_dir_conf(join_path(current_dir, "output_dir.conf"));
    }
    if (!output_dir.empty() && QDir(QString::fromStdString(output_dir)).exists()) {
        return with_trailing_slash(QString::fromStdString(output_dir));
    }
    return default_output_dir();
}

static bool write_file_if_missing(
    const QString& path,
    const char* data,
    size_t size,
    std::string* error)
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

bool ensure_template_dir_ready(const std::string& dir_path, std::string* error)
{
    QDir templates_dir(QString::fromStdString(dir_path));
    if (!templates_dir.exists() && !templates_dir.mkpath(".")) {
        if (error) {
            *error = "Could not create template directory.";
        }
        return false;
    }

    return write_file_if_missing(
            templates_dir.filePath("Max Mustermann.json"),
            k_default_profile_simple_json,
            std::strlen(k_default_profile_simple_json),
            error)
        && write_file_if_missing(
            templates_dir.filePath("Max Mustermann, Mustermann AG.json"),
            k_default_profile_commercial_json,
            std::strlen(k_default_profile_commercial_json),
            error)
        && write_file_if_missing(
            templates_dir.filePath("mustermann_signature.png"),
            reinterpret_cast<const char*>(mustermann_signature_png::data().first),
            mustermann_signature_png::data().second,
            error);
}

std::vector<profile_entry_t> discover_profiles(
    const std::string& template_dir,
    std::vector<std::string>* errors)
{
    std::vector<profile_entry_t> profiles;
    QDir dir(QString::fromStdString(template_dir));
    const auto profile_files = dir.entryList({ "*.json" }, QDir::Files, QDir::Name);
    for (const auto& profile_file : profile_files) {
        const auto profile_path = dir.filePath(profile_file);
        auto result = load_sender_profile(profile_path.toStdString());
        if (result.ok) {
            profiles.push_back({
                std::move(result.profile),
                profile_path.toStdString(),
                QFileInfo(profile_path).absoluteDir().absolutePath().toStdString(),
            });
        }
        else if (errors) {
            errors->push_back(profile_file.toStdString() + ": " + result.error);
        }
    }
    return profiles;
}

} // namespace briefutil
