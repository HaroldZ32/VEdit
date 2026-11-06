#!/usr/bin/env bash
# Focused probes: the two argv shapes that must work on BOTH git 1.8.3.1 and 2.53.
#   * `git stash save -- <message>` (leading dash message; `-m` is unknown on 1.8.3.1)
#   * `git -c protocol.file.allow=always submodule add <local path>` (host 2.53 only:
#     the baseline has no such restriction) — the FIXTURE passes it, never the product.
set -u
export HOME=/tmp/probehome3 GIT_CONFIG_NOSYSTEM=1
rm -rf /tmp/probe3 && mkdir -p /tmp/probe3 && cd /tmp/probe3 && mkdir repo && cd repo
git init -q . && git config user.email p@x && git config user.name P
echo a > a.txt && git add a.txt && git commit -qm init
echo "== version =="; git --version
echo "== stash save -- leading-dash =="; echo b >> a.txt; git stash save -- "-m looks like an option" 2>&1; echo "rc=$?"
echo "== stash list =="; git stash list --format='%gd%x09%H%x09%s' 2>&1
echo "== stash pop =="; git stash pop 2>&1 | tail -1; echo "pop rc=${PIPESTATUS[0]}"
echo "== stash save -- plain =="; echo c >> a.txt; git stash save -- "plain msg" 2>&1; echo "rc=$?"
echo "== stash drop =="; git stash drop 2>&1; echo "rc=$?"
echo "== submodule add with -c protocol.file.allow=always =="
mkdir -p /tmp/probe3/sub && cd /tmp/probe3/sub && git init -q . \
  && git config user.email s@x && git config user.name S \
  && echo s > s.txt && git add s.txt && git commit -qm s
cd /tmp/probe3/repo && git -c protocol.file.allow=always submodule add /tmp/probe3/sub sub 2>&1; echo "rc=$?"
echo "== .gitmodules =="; cat .gitmodules 2>&1
echo "== submodule status =="; git submodule status 2>&1; echo "rc=$?"
echo "== fresh clone + submodule update --init (with -c) =="
cd /tmp/probe3 && git clone -q repo clone1 && cd clone1 \
  && git -c protocol.file.allow=always submodule update --init 2>&1; echo "rc=$?"
echo "== submodule status in clone =="; git submodule status 2>&1; echo "rc=$?"
echo "== submodule init then update (product shape) =="
cd /tmp/probe3 && rm -rf clone2 && git clone -q repo clone2 && cd clone2 \
  && git submodule init 2>&1; echo "init rc=$?" \
  && git -c protocol.file.allow=always submodule update --init 2>&1; echo "update rc=$?"
echo "== submodule update --init on a repo without submodules =="
cd /tmp/probe3 && rm -rf plain && mkdir plain && cd plain && git init -q . \
  && git config user.email p@x && git config user.name P && echo z > z.txt && git add z.txt && git commit -qm z \
  && git submodule init 2>&1; echo "init rc=$?"; git submodule update --init 2>&1; echo "update rc=$?"
