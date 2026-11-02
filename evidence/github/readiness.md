# GitHub push readiness — read-only audit (not committed)

Target: `github.com/HaroldZ32/VEdit` · date: 2026-10-30 (Asia/Shanghai)
Status: **read-only audit only — nothing was pushed, no remote was added, no
git config, `.gitignore` or tracked file was changed, and no credential value
was read or printed.**

State drift during the audit: the snapshot in §1–§6 was taken while the tree was
**clean at HEAD `15481c0`**; while this report was being written, the T21 lane
started editing `crates/vd-app/src/{app.rs,selftest.rs}` and the T29 lane created
an untracked `docs/` directory. Those changes are **not** part of the audited
state and must be committed (or deliberately excluded) before any push — see §1.1.

## 0. Verdict

**Technically pushable today, but three decisions/actions are required first:**

1. **No remote is configured** (`git remote -v` is empty). `origin` must be added.
2. **The remote is not empty and is unrelated**: it holds one commit
   `4b9e17c "Initial commit"` that adds only `LICENSE`; local history (25 linear
   commits) has **no LICENSE/README**. A plain push is rejected as
   non-fast-forward. Choose:
   - **(A) force-with-lease** (no branch protection, admin=true) → replaces the
     LICENSE-only initial commit; **local must first gain LICENSE/README** (T29
     pending), otherwise the repo ends up with no license file;
   - **(B) fetch + `merge --allow-unrelated-histories`** → keeps LICENSE, adds a
     merge commit (breaks the current linear history style).
3. **Credentials**: `gh` is authenticated as **HaroldZ32** with `repo` scope, but
   **no git credential helper is configured** and **SSH is unusable** here. A
   push needs either the one-shot inline helper
   `git -c credential.helper='!gh auth git-credential' push …` (no config
   change) or a one-time `gh auth setup-git` (writes global git config —
   approval needed).

No LFS is needed: the largest tracked file is 681 KB and the whole history is
4.17 MiB; the three deliverables are correctly ignored and belong in a GitHub
Release, not in git history.

## 1. Local repository state

```
$ git remote -v
(no output — no remote configured)

$ git status --short
(no output — working tree clean)

$ git log --oneline -5
15481c0 chore(evidence): keep only the reviewable X-server screenshot (T25)
9beb352 feat(packaging): self-contained tar.gz and self-extracting .run for CentOS 7 (T25)
f339edc test(packaging): record the AppImage sha256 and refresh clean-room sizes (T23)
f98354a feat(packaging): self-contained VEdit AppImage with offline clean-room acceptance (T23)
1265520 fix(app): runtime-overridable selftest fixtures, behaviour assertions and real screenshots (T21 follow-up)

$ git branch -a
* main
```

- Local branch is **`main`**, matching the remote default branch `main`.
- Working tree is **clean**; no stash (`git stash list` empty), no tags.

### 1.1 State drift observed after the audit

```
$ git status --short          (re-checked at the end of the audit)
 M crates/vd-app/src/app.rs
 M crates/vd-app/src/selftest.rs
?? docs/
?? evidence/github/
```

`crates/vd-app/*` are the T21 lane's in-flight edits and `docs/` is the T29
lane's in-flight documentation; `evidence/github/` is this report (deliberately
left uncommitted for the captain's review). The push execution order must
therefore start by letting those lanes commit and re-checking `git status`, or
the published tree will silently lack their work.

## 2. History shape

```
$ git log --oneline | wc -l
25

$ git rev-list --merges --count HEAD
0

$ git log --graph --oneline -20
* 15481c0 … (25 commits, every one a single-parent commit)
```

Linear history, **zero merge commits**, no tags.

Commit identities used: `Deep Worker <deep-worker@vedit.local>`,
`Junior Engineer <juniorengineer@vedit.local>`,
`Senior Engineer <seniorengineer@vedit.local>`,
`VEdit agent <agent@vedit.local>`. Repository config:
`user.name=VEdit agent`, `user.email=agent@vedit.local`.

## 3. Remote existence and state

```
$ gh repo view HaroldZ32/VEdit --json name,isPrivate,defaultBranchRef
{"defaultBranchRef":{"name":"main"},"isPrivate":false,"name":"VEdit"}

$ GIT_TERMINAL_PROMPT=0 git ls-remote https://github.com/HaroldZ32/VEdit
4b9e17cebf669d2096ea7d5c20f78fed8d602365	HEAD
4b9e17cebf669d2096ea7d5c20f78fed8d602365	refs/heads/main

$ gh api "repos/HaroldZ32/VEdit/commits?per_page=5" --jq '.[] | …'
4b9e17c Initial commit

$ gh api repos/HaroldZ32/VEdit/commits/4b9e17c --jq '{message, files}'
{"date":"2026-10-30T02:14:49Z","files":["LICENSE"],"message":"Initial commit"}

$ gh api repos/HaroldZ32/VEdit --jq '{size_kb, default_branch, private, pushed_at}'
{"default_branch":"main","private":false,"pushed_at":"2026-10-30T02:14:50Z","size_kb":1}

$ gh api "repos/HaroldZ32/VEdit/commits/$(git rev-parse HEAD)" --jq '.sha'
422 No commit found for SHA: 15481c0dd4a5fdecd6c8189e0803df65065e2a01

$ gh api repos/HaroldZ32/VEdit/branches/main/protection
404 Branch not protected

$ gh api repos/HaroldZ32/VEdit --jq '{push: .permissions.push, admin: .permissions.admin}'
{"admin":true,"push":true}
```

- Repo exists, **public**, default branch `main`, description already set.
- Remote contains only the automatic `Initial commit` (LICENSE) — the local HEAD
  is **not** on the remote, and the local tree has **no LICENSE**.
- No branch protection; the authenticated account has **admin + push**.
- Remote size is 1 KB, so a first push is cheap.

## 4. Credentials (presence only — no value printed)

```
$ gh auth status
github.com
  ✓ Logged in to github.com account HaroldZ32 (/home/haroldzhao/.config/gh/hosts.yml)
  - Active account: true
  - Git operations protocol: https
  - Token: gho_************************************
  - Token scopes: 'gist', 'read:org', 'repo', 'workflow'

$ for v in GITHUB_TOKEN GH_TOKEN GH_HOST GITHUB_ENTERPRISE_TOKEN; …
GITHUB_TOKEN=UNSET
GH_TOKEN=UNSET
GH_HOST=UNSET
GITHUB_ENTERPRISE_TOKEN=UNSET

$ git config --get credential.helper          → <unset>
$ git config --global --get credential.helper → <unset>

$ ssh -o BatchMode=yes -T git@github.com
Bad owner or permissions on /etc/ssh/ssh_config.d/20-systemd-ssh-proxy.conf
ssh_rc=255          (no SSH keys present; ~/.ssh has no *.pub)
```

- **HTTPS via the `gh` token is the only viable path**; SSH is broken by a
  system ssh_config permission problem and there are no keys.
- The token's scopes include `repo` and `workflow`; push permission confirmed by
  the API (`push: true`, `admin: true`).
- Since no credential helper is configured, a `git push` over HTTPS would not
  use `gh`'s token automatically. Options (approval needed for the persistent
  one):
  - one-shot, no config change:
    `git -c credential.helper='!gh auth git-credential' push -u origin main`
  - persistent: `gh auth setup-git` (writes the global git config).

## 5. Repository size / LFS

```
$ git count-objects -vH
count: 742
size: 4.01 MiB
in-pack: 0
packs: 0
size-pack: 0 bytes

$ du -sh .git            → 5.2M
$ du -sh --exclude=.git . → 9.2G      (dominated by ignored build dirs)
$ git ls-files | wc -l    → 468
$ git ls-tree -r -l HEAD | awk '{s+=$4} END {print s}'
tracked_bytes=4377432 (4.17 MiB)

$ git ls-tree -r -l HEAD | awk '$4>204800 {…}' | sort -rn
  681220  build/slang/evidence/02b-python-build.log
  636176  build/gui-spike/evidence/vedit-gui-spike.bin
  525379  packaging/tarball/evidence/10-payload-manifest.txt

$ git ls-files | grep -Ei '\.(tar\.gz|tgz|run|AppImage)$'
(none)
```

- **No LFS required**: largest tracked file is 681 KB (a build log); no binary
  deliverable is tracked; total history is ~4.2 MiB.
- The 9.2 GB working tree is entirely ignored build output (`target/` 7.3 GB,
  `build/gui-spike/` 1.4 GB, `packaging/*/out`, `packaging/appimage/AppDir`,
  `build/slang/`, `.mpd/` 84 MB).
- One tracked file is a cleanup candidate (not changed): the stale binary
  `build/gui-spike/evidence/vedit-gui-spike.bin` (636 KB) duplicates a binary
  that is otherwise ignored.

### Deliverables on disk (all ignored, Release attachments, not history)

```
48105976  packaging/appimage/out/VEdit-0.1.0-x86_64.AppImage
40887126  packaging/tarball/out/VEdit-0.1.0-x86_64.run
40884328  packaging/tarball/out/vedit-0.1.0-linux-x86_64.tar.gz
55010959  build/slang/cache/cmake-3.31.6-linux-x86_64.tar.gz   (build input cache)
19169299  build/gui-spike/out/vedit-spike-0.1.0-linux-x86_64.tar.gz
18430456  build/gui-spike/out/VEdit-Spike-0.1.0-x86_64.AppImage
```

## 6. `.gitignore` coverage

```
$ git check-ignore -v …
packaging/appimage/out     IGNORED  packaging/appimage/.gitignore:3:out/
packaging/appimage/AppDir  IGNORED  packaging/appimage/.gitignore:2:AppDir/
packaging/appimage/.cache  IGNORED  packaging/appimage/.gitignore:4:.cache/
packaging/tarball/out      IGNORED  packaging/tarball/.gitignore:2:out/
build/slang/out            IGNORED  .gitignore:19:build/slang/out/
target                     IGNORED  .gitignore:10:target/
.mpd                       IGNORED  .gitignore:4:.mpd/
.codegraph                 IGNORED  .gitignore:7:.codegraph/
```

Root `.gitignore` already covers `.mpd/`, `.codegraph/`, `target/`,
`build/artifacts/`, `*.AppImage`, `*.AppImage.zsync`,
`build/slang/{cache,out,tmp,slang-12.1}`; the two packaging directories carry
their own `.gitignore` (`out/`, `AppDir/`, `.cache/`, `stage/`).

**Residual gaps (reported only, not changed):**
- no global rule for `*.tar.gz` / `*.tgz` / `*.run` — currently safe because the
  only such files live inside already-ignored `out/` directories, but a future
  artifact written elsewhere would not be ignored;
- `build/gui-spike/.gitignore` exists for that tree (out/AppDir/tarroot/.cache/
  app/target), but the historical binary under `evidence/` is already tracked;
- `build/slang/.gitignore` is empty (the root rules for `build/slang/*` do the
  work).

## 7. Missing before a real push (needs captain decision / other lanes)

| item | state |
|---|---|
| `git remote origin` | missing (`git remote -v` empty) |
| remote strategy | remote has an unrelated LICENSE-only initial commit; force-with-lease vs merge must be chosen |
| `LICENSE` in local tree | **absent** (remote's only file); T29 owns docs/licences |
| `README.md` at root | absent (only `packaging/`/`evidence/` READMEs are tracked); T29 owns it |
| `THIRD_PARTY_NOTICES` | absent (T29) |
| credential helper / SSH | unset / broken → use the gh inline helper or `gh auth setup-git` (approval) |
| release artefacts | correctly untracked; must be attached to a GitHub Release (AppImage 48,105,976 B sha256 `b589416e…`, tar.gz 40,884,328 B, .run 40,887,126 B) |
| branch/release conventions | to be dictated by the captain (none exist yet; no tags) |

## 8. Exact commands for the (not yet authorised) push

```bash
# 1) add the remote (local change, approval needed)
git remote add origin https://github.com/HaroldZ32/VEdit.git

# 2) choose ONE:
# (A) replace the LICENSE-only initial commit (local must contain LICENSE first)
git -c credential.helper='!gh auth git-credential' push --force-with-lease -u origin main
# (B) keep it as an unrelated merge
git fetch origin main
git merge --allow-unrelated-histories origin/main
git -c credential.helper='!gh auth git-credential' push -u origin main
```

No command above was executed.
