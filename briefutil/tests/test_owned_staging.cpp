#include "briefutil/owned_staging.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#if defined(_WIN32)
#include <QProcess>
#endif

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

static constexpr const char* k_target_name    = "letter.pdf";
static constexpr const char* k_sentinel_bytes = "Disposable content outside the owned staging slot";

static void require(bool passed, const char* message)
{
    if (!passed) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void write_file(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "could not create a test file");
    require(file.write(bytes) == bytes.size(), "could not write a test file");
}

static void require_sentinel(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "staging cleanup deleted an outside-slot sentinel");
    require(file.readAll() == k_sentinel_bytes, "staging cleanup changed an outside-slot sentinel");
}

enum class Link_kind
{
    SYMBOLIC,
#if defined(_WIN32)
    JUNCTION,
#endif
};

static void create_directory_link(const QString& target, const QString& link, Link_kind kind)
{
#if defined(_WIN32)
    if (kind == Link_kind::JUNCTION) {
        const int result = QProcess::execute("cmd.exe", {
            "/d", "/c", "mklink", "/J",
            QDir::toNativeSeparators(link),
            QDir::toNativeSeparators(target),
        });
        require(result == 0 && QFileInfo(link).isJunction(), "could not create a test junction");
        return;
    }
#else
    (void)kind;
#endif
    std::error_code error;
    fs::create_directory_symlink(
        QFileInfo(target).filesystemFilePath(), QFileInfo(link).filesystemFilePath(), error);
    if (error) {
        std::fprintf(stderr, "FAIL: could not create a test directory symlink: %s\n",
            error.message().c_str());
        std::exit(1);
    }
}

struct Staging_paths
{
    QDir    publish_dir;
    QString root_path;
    QString slot_path;
    QString sentinel_path;
};

static Staging_paths create_paths(const QTemporaryDir& temporary)
{
    require(temporary.isValid(), "could not create a temporary directory");
    Staging_paths paths;
    paths.publish_dir   = QDir(temporary.filePath("publish"));
    paths.root_path     = paths.publish_dir.filePath(briefutil::k_owned_staging_dir);
    paths.sentinel_path = temporary.filePath("outside-slot/test-only.txt");
    require(QDir().mkpath(paths.publish_dir.absolutePath()), "could not create a publication directory");
    require(QDir().mkpath(QFileInfo(paths.sentinel_path).absolutePath()),
        "could not create an outside-slot directory");
    write_file(paths.sentinel_path, k_sentinel_bytes);
    {
        briefutil::Owned_staging_slot slot;
        require(slot.open(paths.publish_dir, k_target_name, nullptr), "could not discover the staging slot");
        paths.slot_path = QFileInfo(slot.staged_path()).absolutePath();
    }
    return paths;
}

static void test_reclaim(Link_kind kind, bool nested)
{
    QTemporaryDir temporary;
    const auto paths = create_paths(temporary);
    QString link_path = paths.slot_path;
    if (nested) {
        link_path += "/debris/directory-link";
    }
    require(QDir().mkpath(QFileInfo(link_path).absolutePath()), "could not prepare abandoned staging");
    create_directory_link(QFileInfo(paths.sentinel_path).absolutePath(), link_path, kind);

    {
        briefutil::Owned_staging_slot slot;
        std::string error;
        require(slot.open(paths.publish_dir, k_target_name, &error), "could not reclaim linked staging debris");
        require_sentinel(paths.sentinel_path);
        const QFileInfo slot_info(paths.slot_path);
        require(slot_info.isDir() && !slot_info.isSymbolicLink() && !slot_info.isJunction(),
            "reclamation must create an ordinary staging directory");
        require(QDir(paths.slot_path).isEmpty(), "reclamation left abandoned staging debris");
        write_file(slot.staged_path(), "new staged content");
    }
    require_sentinel(paths.sentinel_path);
    require(!QFileInfo::exists(paths.root_path), "reclaimed staging was not discarded");
}

static void test_discard(Link_kind kind, bool nested)
{
    QTemporaryDir temporary;
    const auto paths = create_paths(temporary);
    {
        briefutil::Owned_staging_slot slot;
        require(slot.open(paths.publish_dir, k_target_name, nullptr), "could not open staging for discard");
        QString link_path = paths.slot_path;
        if (nested) {
            link_path += "/debris/directory-link";
            require(QDir().mkpath(QFileInfo(link_path).absolutePath()), "could not prepare nested staging");
        }
        else {
            require(QDir().rmdir(paths.slot_path), "could not replace the open staging directory");
        }
        create_directory_link(QFileInfo(paths.sentinel_path).absolutePath(), link_path, kind);
    }
    require_sentinel(paths.sentinel_path);
    require(!QFileInfo::exists(paths.root_path), "discard left a linked staging entry behind");
}

static void test_root_refusal(Link_kind kind)
{
    QTemporaryDir temporary;
    const auto paths = create_paths(temporary);
    const QDir outside(QFileInfo(paths.sentinel_path).absolutePath());
    const QString outside_slot = outside.filePath(QFileInfo(paths.slot_path).fileName());
    require(QDir().mkpath(outside_slot), "could not prepare an outside-slot directory");
    const QString nested_sentinel = QDir(outside_slot).filePath("test-only.txt");
    write_file(nested_sentinel, k_sentinel_bytes);
    create_directory_link(outside.absolutePath(), paths.root_path, kind);
    const auto before = outside.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    {
        briefutil::Owned_staging_slot slot;
        std::string error;
        require(!slot.open(paths.publish_dir, k_target_name, &error), "a linked staging root must be refused");
        require(!error.empty() && slot.staged_path().isEmpty(), "a refused staging root needs a failure result");
    }
    require_sentinel(paths.sentinel_path);
    require_sentinel(nested_sentinel);
    outside.refresh();
    require(outside.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot) == before,
        "a refused staging root changed the linked directory");
    std::error_code error;
    require(fs::remove(QFileInfo(paths.root_path).filesystemFilePath(), error) && !error,
        "could not remove the test staging-root link");
}

static void test_dangling_slot()
{
    QTemporaryDir temporary;
    const auto paths = create_paths(temporary);
    require(QDir().mkpath(paths.root_path), "could not prepare dangling staging");
    create_directory_link(temporary.filePath("missing-directory"), paths.slot_path, Link_kind::SYMBOLIC);
    {
        briefutil::Owned_staging_slot slot;
        require(slot.open(paths.publish_dir, k_target_name, nullptr), "could not reclaim a dangling staging link");
        write_file(slot.staged_path(), "new staged content");
    }
    require(!QFileInfo::exists(temporary.filePath("missing-directory")),
        "reclamation created a dangling link's target");
    require_sentinel(paths.sentinel_path);
    require(!QFileInfo::exists(paths.root_path), "dangling staging was not discarded");
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const Link_kind kinds[] = {
        Link_kind::SYMBOLIC,
#if defined(_WIN32)
        Link_kind::JUNCTION,
#endif
    };
    for (const auto kind : kinds) {
        test_reclaim(kind, false);
        test_reclaim(kind, true);
        test_discard(kind, false);
        test_discard(kind, true);
        test_root_refusal(kind);
    }
    test_dangling_slot();
    std::puts("PASS: staging cleanup preserves targets of directory links");
    return 0;
}
