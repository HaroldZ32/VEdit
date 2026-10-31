# Probe image for the R2 pre-flight probes (C17 overlay / C15 seam / fold interface).
# Base: Ubuntu 26.04, the release this repository's host runs, so the probe measures
# the same KF6/Qt6 generation the product will target.
#
# NOTE: at probe time the m0-build task had not produced its pinned image yet
# (no `vprobe`/`vedit-build` image existed in `docker images`, and no
# evidence/m0-build/ artifacts were on disk). These probes therefore build their own
# image from the distribution's packaged Qt6/KF6 development headers; every version is
# frozen by the image id and captured in evidence (dpkg listing). When the m0-build
# image lands, the probes only have to change FROM.
FROM docker.m.daocloud.io/library/ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive \
    LC_ALL=C.UTF-8 \
    LANG=C.UTF-8

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config git ca-certificates curl xz-utils \
        extra-cmake-modules \
        qt6-base-dev qt6-base-dev-tools qt6-svg-dev \
        libkf6texteditor-dev libkf6syntaxhighlighting-dev libkf6texteditor-bin \
        libkf6xmlgui-dev libkf6parts-dev libkf6i18n-dev libkf6config-dev \
        libtree-sitter-dev \
        xvfb x11-utils libxcb-cursor0 libxkbcommon-x11-0 libgl1 libegl1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

# The KTextEditor framework alone has no editor implementation: the KatePart
# plugins (editor factory, syntax highlighting bridge) ship in a separate
# package. Without it KTextEditor::Editor::instance() returns null.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libkf6texteditor-katepart \
    && rm -rf /var/lib/apt/lists/*
