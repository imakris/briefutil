#include "briefutil/template_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <cstdio>

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static std::string with_slash(QString path)
{
    path = QDir::fromNativeSeparators(path);
    if (!path.endsWith('/')) {
        path += '/';
    }
    return path.toStdString();
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir root;
    require(root.isValid(), "could not create temporary directory");

    const QString env_template_dir = root.filePath("env-templates");
    qputenv("BRIEFUTIL_TEMPLATE_DIR", env_template_dir.toUtf8());
    require(
        briefutil::default_template_dir() == with_slash(env_template_dir),
        "BRIEFUTIL_TEMPLATE_DIR should override default template dir");

    std::string error;
    require(
        briefutil::ensure_template_dir_ready(env_template_dir.toStdString(), &error),
        "template seeding should succeed");
    require(
        QFile::exists(QDir(env_template_dir).filePath("Max Mustermann.json")),
        "simple default profile should be seeded");
    require(
        QFile::exists(QDir(env_template_dir).filePath("mustermann_signature.png")),
        "default signature image should be seeded");
    require(
        briefutil::discover_profiles(env_template_dir.toStdString()).size() >= 2,
        "seeded profiles should be discoverable");
    QFile bad_profile(QDir(env_template_dir).filePath("bad.json"));
    require(bad_profile.open(QIODevice::WriteOnly), "could not create malformed profile");
    bad_profile.write("{");
    bad_profile.close();
    std::vector<std::string> profile_errors;
    (void)briefutil::discover_profiles(env_template_dir.toStdString(), &profile_errors);
    require(!profile_errors.empty(), "malformed profiles should be reported");

    const QString missing_output_dir = root.filePath("missing-output");
    qputenv("BRIEFUTIL_OUTPUT_DIR", missing_output_dir.toUtf8());
    require(
        briefutil::configured_output_dir("ignored-app-dir", "ignored-cwd")
            == with_slash(missing_output_dir),
        "BRIEFUTIL_OUTPUT_DIR should be authoritative even before it exists");
    qunsetenv("BRIEFUTIL_OUTPUT_DIR");

    const QString portable_root = root.filePath("portable");
    const QString portable_output = root.filePath("portable-output");
    QDir().mkpath(portable_root);
    QDir().mkpath(portable_output);
    QFile conf(QDir(portable_root).filePath("output_dir.conf"));
    require(conf.open(QIODevice::WriteOnly), "could not write output_dir.conf");
    conf.write(portable_output.toUtf8());
    conf.close();
    qputenv("BRIEFUTIL_PORTABLE_ROOT", portable_root.toUtf8());
    require(
        briefutil::configured_output_dir("ignored-app-dir", "ignored-cwd")
            == with_slash(portable_output),
        "portable output_dir.conf should be honored");
    const QString missing_conf_output = root.filePath("missing-conf-output");
    QFile conf2(QDir(portable_root).filePath("output_dir.conf"));
    require(conf2.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "could not rewrite output_dir.conf");
    conf2.write(missing_conf_output.toUtf8());
    conf2.close();
    require(
        briefutil::configured_output_dir("ignored-app-dir", "ignored-cwd")
            == briefutil::default_output_dir(),
        "missing output_dir.conf target should fall back");

    qunsetenv("BRIEFUTIL_TEMPLATE_DIR");
    qunsetenv("BRIEFUTIL_PORTABLE_ROOT");
    return 0;
}
