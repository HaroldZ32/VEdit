#!/usr/bin/env bash
# Probe the SAME new options against the host git (2.53.0) so one argv works on
# both the delivery baseline (1.8.3.1) and the newer host.
set -u
export HOME=/tmp/probehome2 GIT_CONFIG_NOSYSTEM=1
rm -rf /tmp/probe2 && mkdir -p /tmp/probe2 && cd /tmp/probe2 && mkdir repo && cd repo
git init -q . && git config user.email p@x && git config user.name P
echo a > a.txt && git add a.txt && git commit -qm init
echo "== version =="; git --version
echo "== stash save positional =="; echo b >> a.txt; git stash save "msg one" 2>&1; echo "rc=$?"
echo "== stash list --format =="; git stash list --format='%gd%x09%H%x09%s' 2>&1; echo "rc=$?"
echo "== stash pop =="; git stash pop 2>&1 | tail -2; echo "pop rc=${PIPESTATUS[0]}"
echo "== stash save -- leading-dash msg =="; echo c >> a.txt; git stash save -- "-m looks like an option" 2>&1; echo "rc=$?"
echo "== stash list after =="; git stash list --format='%gd%x09%H%x09%s' 2>&1; echo "rc=$?"
echo "== stash apply stash@{0} =="; git stash apply stash@{0} 2>&1 | tail -2; echo "apply rc=${PIPESTATUS[0]}"
echo "== stash drop stash@{0} =="; git stash drop stash@{0} 2>&1; echo "rc=$?"
echo "== stash save (no msg) =="; echo d >> a.txt; git stash save 2>&1; echo "rc=$?"
echo "== stash pop (final) =="; git stash pop 2>&1 | tail -1; echo "pop rc=${PIPESTATUS[0]}"
echo "== worktree list (one) =="; git worktree list 2>&1; echo "rc=$?"
echo "== worktree add <path> <branch> =="; git worktree add ../wt-1 HEAD 2>&1; echo "rc=$?"
echo "== worktree add named branch =="; git worktree add ../wt-2 -b feature/wt2 HEAD 2>&1; echo "rc=$?"
echo "== worktree list (three, spaced path) =="; git worktree add "../wt with space" -b feature/spaced HEAD 2>&1 >/dev/null; git worktree list 2>&1; echo "rc=$?"
echo "== worktree remove dirty =="; echo dirty >> ../wt-1/a.txt; git worktree remove ../wt-1 2>&1; echo "rc=$?"
echo "== worktree remove clean =="; git worktree remove ../wt-2 2>&1; echo "rc=$?"
echo "== worktree remove clean spaced =="; git worktree remove "../wt with space" 2>&1; echo "rc=$?"
echo "== worktree list (one again) =="; git worktree list 2>&1
echo "== worktree remove missing =="; git worktree remove ../nope 2>&1; echo "rc=$?"
echo "== submodule add local, no protocol config =="
mkdir -p /tmp/probe2/sub && cd /tmp/probe2/sub && git init -q . \
  && git config user.email s@x && git config user.name S \
  && echo s > s.txt && git add s.txt && git commit -qm s
cd /tmp/probe2/repo && git submodule add /tmp/probe2/sub sub 2>&1; echo "rc=$?"
echo "== submodule update --init (no protocol config) =="; rm -rf sub; git submodule update --init 2>&1; echo "rc=$?"
echo "== with protocol.file.allow=always in superproject =="
git config protocol.file.allow always
git submodule add /tmp/probe2/sub sub2 2>&1; echo "rc=$?"
git submodule update --init 2>&1; echo "rc=$?"
echo "== submodule status =="; git submodule status 2>&1; echo "rc=$?"
echo "== submodule init on fresh clone =="; cd /tmp/probe2 && git clone -q repo clone1 && cd clone1 && git config protocol.file.allow always && git submodule init 2>&1; echo "rc=$?"; git submodule update 2>&1; echo "rc=$?"
