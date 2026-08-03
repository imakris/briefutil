#include "briefutil/owned_staging.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>

#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace briefutil {

// A slot holds exactly one file and is private to whoever holds the slot lock,
// so this name has nothing to distinguish and nothing ever matches on it. The
// lock is the exclusion mechanism; a random suffix here would add no exclusion
// the slot does not already provide.
static constexpr const char* k_staged_file_name = "staged";

// How long a run waits for another briefutil run that is already staging into
// the same slot. The callers that take a lock of their own on what they are
// publishing never contend here, because that lock has already excluded the
// other run; the ones that can contend write a few kilobytes and are called
// straight from the GUI thread. So a wait this long already says the other run
// is stuck, and failing then is more useful than freezing the window on it.
static constexpr int k_slot_lock_timeout_ms = 5000;

// The slot is named after a digest of the target rather than after the target
// itself. The target filename is caller-supplied and unbounded, and carrying a
// second copy of it as an extra path component would eat the Windows MAX_PATH
// margin that k_max_filename_component_bytes is sized against. Two targets that
// collided here would take each other's slot lock in turn, so they would wait
// on one another instead of publishing at once; neither could reach anything
// the user owns, because nothing outside the slot is ever removed.
static QString slot_name(const QString& target_name)
{
    const QByteArray digest = QCryptographicHash::hash(
        target_name.toUtf8(),
        QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.left(8).toHex());
}

Owned_staging_slot::Owned_staging_slot()  = default;
Owned_staging_slot::~Owned_staging_slot()
{
    discard();
}

bool Owned_staging_slot::open(
    const QDir&    publish_dir,
    const QString& target_name,
    std::string*   error)
{
    discard();

    const QString root_path = publish_dir.filePath(QString::fromLatin1(k_owned_staging_dir));
    const QString slot      = slot_name(target_name);
    const QString lock_path = root_path + QStringLiteral("/") + slot + QStringLiteral(".lock");

    // Creating the shared root and taking the slot lock inside it are two steps,
    // and the run that discards the last slot under this directory removes the
    // root between them. Doing both again settles it: the run that removed the
    // root has finished, and once this run's own lock file is in the root the
    // root is no longer empty, so nothing can remove it out from under the slot.
    for (int attempt = 0; attempt < 2 && !m_lock; ++attempt) {
        if (!QDir(root_path).mkpath(".")) {
            continue;
        }
        auto lock = std::make_unique<QLockFile>(lock_path);
        // A slot changes hands only once the process holding it is provably
        // gone, never because it is taking its time.
        lock->setStaleLockTime(0);
        if (lock->tryLock(k_slot_lock_timeout_ms)) {
            m_lock = std::move(lock);
        }
        else
        if (lock->error() == QLockFile::LockFailedError) {
            if (error) {
                *error = "Another briefutil run is still using the staging directory: " +
                    root_path.toStdString();
            }
            return false;
        }
    }
    if (!m_lock) {
        if (error) {
            *error = "Could not create the staging directory: " + root_path.toStdString();
        }
        return false;
    }

    m_publish_dir = publish_dir;
    m_slot_path   = root_path + QStringLiteral("/") + slot;

    QDir slot_dir(m_slot_path);
    if (slot_dir.exists() && !slot_dir.removeRecursively()) {
        if (error) {
            *error = "Could not reclaim the staging directory: " + m_slot_path.toStdString();
        }
        discard();
        return false;
    }
    if (!slot_dir.mkpath(".")) {
        if (error) {
            *error = "Could not create the staging directory: " + m_slot_path.toStdString();
        }
        discard();
        return false;
    }

    m_staged_path = slot_dir.filePath(QString::fromLatin1(k_staged_file_name));
    return true;
}

void Owned_staging_slot::discard()
{
    // This runs from the destructor and has no channel to report on. Losing any
    // of it leaves debris inside briefutil's own staging area, which the next
    // run for this target reclaims, and leaves nothing of the user's touched
    // either way.
    if (!m_slot_path.isEmpty()) {
        QDir(m_slot_path).removeRecursively();
        m_slot_path.clear();
        m_staged_path.clear();
    }
    if (m_lock) {
        // Releasing removes the lock file, which is what lets the shared root
        // go once the last slot under this directory is done with it. The rmdir
        // refuses while another slot or another run's lock is still in there,
        // which is exactly when the root has to stay.
        m_lock->unlock();
        m_lock.reset();
        m_publish_dir.rmdir(QString::fromLatin1(k_owned_staging_dir));
    }
}

Publish_outcome publish_staged_file(
    const QString& staged_path,
    const QString& target_path,
    bool           replace_existing,
    std::string*   detail)
{
    if (!replace_existing) {
        QFile staged(staged_path);
        if (staged.rename(target_path)) {
            return Publish_outcome::PUBLISHED;
        }
        // QFile::rename never overwrites, so a target that is present after
        // the attempt is the reason the attempt failed.
        if (QFileInfo::exists(target_path)) {
            return Publish_outcome::TARGET_EXISTS;
        }
        if (detail) {
            *detail = staged.errorString().toStdString();
        }
        return Publish_outcome::FAILED;
    }

#if defined(_WIN32)
    // MOVEFILE_COPY_ALLOWED is deliberately omitted: a copy is not atomic, and
    // the staging file always lives under the target's own directory.
    const std::wstring staged_native = staged_path.toStdWString();
    const std::wstring target_native = target_path.toStdWString();
    if (MoveFileExW(staged_native.c_str(), target_native.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        return Publish_outcome::PUBLISHED;
    }
    if (detail) {
        *detail = "Win32 error " + std::to_string(static_cast<unsigned long>(GetLastError()));
    }
#else
    if (std::rename(
            QFile::encodeName(staged_path).constData(),
            QFile::encodeName(target_path).constData()) == 0)
    {
        return Publish_outcome::PUBLISHED;
    }
    if (detail) {
        *detail = std::strerror(errno);
    }
#endif
    return Publish_outcome::FAILED;
}

} // namespace briefutil
