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
        QFile::exists(QDir(env_template_dir).filePath(".briefutil_templates_initialized")),
        "template initialization marker should be written");
    require(
        briefutil::discover_profiles(env_template_dir.toStdString()).size() >= 2,
        "seeded profiles should be discoverable");
    // Two profiles, the signature image and the marker. Anything else means
    // seeding left working state behind.
    require(
        QDir(env_template_dir)
            .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)
            .size() == 4,
        "seeding should leave only the seeded files and the marker");
    const QString simple_profile_path = QDir(env_template_dir).filePath("Max Mustermann.json");
    require(QFile::remove(simple_profile_path), "could not delete default profile");
    require(
        briefutil::ensure_template_dir_ready(env_template_dir.toStdString(), &error),
        "template initialization should still succeed after deleting a profile");
    require(
        !QFile::exists(simple_profile_path),
        "deleted default profile should not be restored after initialization");
    // A run killed mid-seed leaves part of the seed behind. The next run has to
    // finish it. If it mistakes the leftovers for a directory the user filled in
    // himself it publishes the marker over a half-populated directory, and
    // nothing ever repairs that.
    const QString partial_dir = root.filePath("partial-templates");
    require(QDir().mkpath(partial_dir), "could not create partially seeded dir");
    QFile abandoned_image(QDir(partial_dir).filePath("mustermann_signature.png"));
    require(abandoned_image.open(QIODevice::WriteOnly), "could not create abandoned image");
    abandoned_image.close();
    require(
        briefutil::ensure_template_dir_ready(partial_dir.toStdString(), &error),
        "an interrupted seed should still initialize");
    require(
        QFile::exists(QDir(partial_dir).filePath("Max Mustermann.json")),
        "an interrupted seed must be completed rather than marked initialized");

    // Same failure one step earlier: the seeder died while QSaveFile still had
    // the profile staged beside its final name, so the only entry present is
    // that staging file and not one byte of the seed has committed.
    const QString staged_dir = root.filePath("staged-templates");
    require(QDir().mkpath(staged_dir), "could not create abandoned staging dir");
    QFile abandoned_stage(QDir(staged_dir).filePath("Max Mustermann.json.Ab3x9Z"));
    require(abandoned_stage.open(QIODevice::WriteOnly), "could not create abandoned staging file");
    abandoned_stage.close();
    require(
        briefutil::ensure_template_dir_ready(staged_dir.toStdString(), &error),
        "an abandoned staging file should still initialize");
    require(
        QFile::exists(QDir(staged_dir).filePath("Max Mustermann.json")),
        "an abandoned staging file must not pass for user content");

    // The other side of that decision: a directory the user has already filled
    // in is left alone.
    const QString user_dir = root.filePath("user-templates");
    require(QDir().mkpath(user_dir), "could not create user template dir");
    QFile user_profile(QDir(user_dir).filePath("My Company.json"));
    require(user_profile.open(QIODevice::WriteOnly), "could not create user profile");
    user_profile.write("{}");
    user_profile.close();
    require(
        briefutil::ensure_template_dir_ready(user_dir.toStdString(), &error),
        "a user-populated template dir should initialize");
    require(
        !QFile::exists(QDir(user_dir).filePath("Max Mustermann.json")),
        "a template dir the user populated must not be seeded with the defaults");

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

    const QString portable_root   = root.filePath("portable");
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
