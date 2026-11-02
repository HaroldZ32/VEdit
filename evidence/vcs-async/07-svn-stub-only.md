# T99 N-2: SVN real-subprocess path — DECISION: (b) stub-only, explicitly accepted
# date: 2026-11-01T14:45:10+08:00

## tooling availability
```
$ command -v svn svnadmin            # host
host: svn absent, svnadmin absent

image: svn absent, svnadmin absent
git version 2.53.0
```

## why (b) was chosen
- The pinned image and the host both have no svn binary, so an end-to-end SVN smoke
  (svnadmin create / svn checkout / svn info / svn diff) cannot run in this environment.
- Git's real subprocess path IS covered: the lane selftest starts a job against a real
  repository (with a slow git wrapper proving non-blocking), and `vedit-app --selfcheck-ffi`
  runs the VCS status job through the real C ABI.
- SVN behaviour is covered only by canned-runner Rust tests (crates/vd-vcs/tests) and the
  StubVcsSource panel tests. This is now stated in AGENT.md (VCS section) so nobody reads
  "Subversion" in the UI and assumes the real path was verified.

## what would close it
- Install subversion in the pinned image (or a dedicated svn smoke image) and add:
  svnadmin create /tmp/repo && svn checkout file:///tmp/repo wc && svn add/commit a file
  then assert the panel reads branch/revision/changes/diff from the real svn binary.
  That is a separate task (new image dependency), not a silent gap.
