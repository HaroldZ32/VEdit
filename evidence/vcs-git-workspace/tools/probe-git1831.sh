#!/usr/bin/env bash
# Probe every NEW git option batch 5b relies on against the delivery baseline
# (git 1.8.3.1, the pinned Rust image `vedit-build:local`).
# Run: docker run --rm -i vedit-build:local bash -s < tools/probe-git1831.sh
set -u
export HOME=/tmp/probehome GIT_CONFIG_NOSYSTEM=1
rm -rf /tmp/probe && mkdir -p /tmp/probe && cd /tmp/probe && mkdir repo && cd repo
git init -q . && git config user.email p@x && git config user.name P
echo a > a.txt && git add a.txt && git commit -qm init
echo "== version =="; git --version
echo "== stash save positional msg =="; echo b >> a.txt; git stash save "msg one" 2>&1; echo "rc=$?"
echo "== stash list --format =="; git stash list --format='%gd%x09%H%x09%s' 2>&1; echo "rc=$?"
echo "== stash pop =="; git stash pop 2>&1 | tail -2; echo "rc=$?"
echo "== stash save -m =="; echo c >> a.txt; git stash save -m "msg m" 2>&1; echo "rc=$?"
echo "== stash apply =="; git stash apply 2>&1 | tail -2; echo "rc=$?"
echo "== stash drop =="; git stash drop 2>&1; echo "rc=$?"
echo "== worktree list =="; git worktree list 2>&1; echo "rc=$?"
echo "== worktree add =="; git worktree add ../wt1 HEAD 2>&1; echo "rc=$?"
echo "== worktree in help =="; git help -a 2>/dev/null | grep -c ' worktree'
echo "== submodule status =="; git submodule status 2>&1; echo "rc=$?"
echo "== submodule update --init (none) =="; git submodule update --init 2>&1; echo "rc=$?"
echo "== submodule add (local path) =="
mkdir -p /tmp/probe/sub && cd /tmp/probe/sub && git init -q . \
  && git config user.email s@x && git config user.name S \
  && echo s > s.txt && git add s.txt && git commit -qm s
cd /tmp/probe/repo && git submodule add /tmp/probe/sub sub 2>&1; echo "rc=$?"
echo "== submodule update --init after add =="; git submodule update --init 2>&1; echo "rc=$?"
echo "== submodule foreach / status =="; git submodule status 2>&1; echo "rc=$?"
echo "== stash list (empty) =="; git stash list --format='%gd%x09%H%x09%s' 2>&1; echo "rc=$?"
echo "== stash pop (nothing) =="; git stash pop 2>&1; echo "rc=$?"
