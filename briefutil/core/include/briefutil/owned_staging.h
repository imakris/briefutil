#pragma once

#include <QDir>
#include <QString>

#include <memory>
#include <string>

class QLockFile;

namespace briefutil {

// The one directory briefutil stages into, created inside whatever directory it
// is about to publish into. Nothing but briefutil ever writes below it, so what
// briefutil owns is settled by where an entry lives. No predicate anywhere
// reads a filename as proof of ownership: a name the user, a sync tool, or an
// unrelated crash can produce just as easily proves nothing about who wrote it.
inline constexpr const char* k_owned_staging_dir = ".briefutil-staging";

// The private working area of one publication target.
//
// Opening a slot empties it, which reclaims whatever a run that died before
// publishing left behind. That reclamation matches no names at all - everything
// inside the slot is there because briefutil put it there - and it is exact
// because opening also takes a cross-process lock on the slot, so no live run
// owns the contents either. The lock is never taken from a process that is
// merely slow: it changes hands only once its previous owner is provably gone.
//
// The slot owns that exclusion rather than requiring it of the caller, because
// the callers do not all have a lock to lend. Two of them run under a lock of
// their own on the thing being published, and would otherwise have had to
// remember to; the others hold nothing.
class Owned_staging_slot
{
public:
    Owned_staging_slot();
    ~Owned_staging_slot();

    Owned_staging_slot(const Owned_staging_slot&)            = delete;
    Owned_staging_slot& operator=(const Owned_staging_slot&) = delete;

    // Opens the slot that `target_name` publishes from, under `publish_dir`,
    // and empties it. Fails while another briefutil run holds the same slot.
    bool open(
        const QDir&    publish_dir,
        const QString& target_name,
        std::string*   error);

    // The file to write before handing it to its final name. It sits in a
    // subdirectory of the publish directory, so the handover stays a
    // same-volume rename.
    const QString& staged_path() const { return m_staged_path; }

private:
    void discard();

    QDir                       m_publish_dir;
    QString                    m_slot_path;
    QString                    m_staged_path;
    std::unique_ptr<QLockFile> m_lock;
};

enum class Publish_outcome
{
    PUBLISHED,
    TARGET_EXISTS,
    FAILED,
};

// Hands a staged file over to its final name in one indivisible step.
//
// `replace_existing` selects the replacing OS primitive. Without it the
// platform's non-replacing rename IS the exclusion test, so there is no window
// between deciding that the target is absent and publishing into it. Neither
// path moves the target out of the way first, so no crash between two
// statements can leave the target missing.
Publish_outcome publish_staged_file(
    const QString& staged_path,
    const QString& target_path,
    bool           replace_existing,
    std::string*   detail);

} // namespace briefutil
