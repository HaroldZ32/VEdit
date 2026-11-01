#pragma once

// Panel-facing VCS seam (T23). The panel never talks to git/svn directly: it
// renders whatever a VcsSource reports, so the same widget is driven by the
// production backend (C ABI -> vd-vcs) or by a stub in tests.
//
// The seam is deliberately read-only: there is no method through which the panel
// could request a commit/update/revert/switch. The absence of write methods is
// the panel-side half of the "no write operation is ever called" guarantee; the
// backend-side half is the allow-list in crates/vd-vcs/src/runner.rs.
#include <cstddef>
#include <string>
#include <vector>

namespace vedit::workspace::vcs {

enum class Backend { None, Git, Subversion };

const char *backendName(Backend backend);

struct Change {
    std::string path;
    int kind = 0; // 1 added, 2 modified, 3 deleted, 4 renamed, 5 conflict, 6 unknown
};

const char *changeKindName(int kind);

struct Info {
    Backend backend = Backend::None;
    std::string branch;
    std::string revision;
    std::string note;
};

struct Status {
    Backend backend = Backend::None;
    std::string branch;
    std::vector<Change> changes;
    std::string note;
};

struct Diff {
    std::string path;
    std::string text;
    bool binary = false;
    std::string note;
};

class VcsSource {
public:
    virtual ~VcsSource() = default;
    virtual Info info(const std::string &root) = 0;
    virtual Status status(const std::string &root) = 0;
    virtual Diff diff(const std::string &root, const std::string &path) = 0;
};

// Production source: the C ABI exposed by vd-vcs (read-only endpoints only).
// Built only when the FFI header + static library are present.
class FfiVcsSource final : public VcsSource {
public:
    Info info(const std::string &root) override;
    Status status(const std::string &root) override;
    Diff diff(const std::string &root, const std::string &path) override;
};

// Test/dev stub: canned data, records every call, counts write attempts (there
// is no write method, so any attempt would have to be a future API change).
class StubVcsSource final : public VcsSource {
public:
    StubVcsSource(Info info, Status status, std::vector<Diff> diffs);

    Info info(const std::string &root) override;
    Status status(const std::string &root) override;
    Diff diff(const std::string &root, const std::string &path) override;

    std::size_t calls() const { return calls_; }
    std::size_t writeAttempts() const { return write_attempts_; }

private:
    Info info_;
    Status status_;
    std::vector<Diff> diffs_;
    std::size_t calls_ = 0;
    std::size_t write_attempts_ = 0;
};

} // namespace vedit::workspace::vcs
