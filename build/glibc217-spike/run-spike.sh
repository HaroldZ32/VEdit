#!/usr/bin/env bash
# glibc-2.17 feasibility spike (T76) — transcribed from the commands actually
# executed in the spike container on 2026-10-31 (see
# evidence/glibc217-feasibility/logs/*.log).
#
# NOT re-executed as one script inside the time box: the spike was run step by
# step against a long-lived container so every stage kept its own log. Running
# this end to end rebuilds everything from scratch (~15 min on 16 cores).
#
# What it proves: Qt 6.10.2 (Core/Gui/Widgets) and one real KF6 6.24 framework
# (KSyntaxHighlighting) built NATIVELY inside CentOS 7 (glibc 2.17) end up at
# max GLIBC <= 2.16, and a minimal Qt6 Widgets program runs in a stock centos:7
# with no bundled glibc and no custom loader.
#
#   bash build/glibc217-spike/run-spike.sh
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SPIKE="$ROOT/evidence/glibc217-feasibility"
LOGS="$SPIKE/logs"
IMAGE="${SPIKE_BASE_IMAGE:-vedit-build:local}"   # CentOS 7.9.2009, glibc 2.17
CONTAINER="${SPIKE_CONTAINER:-t76-spike}"
CMAKE_VER="${CMAKE_VER:-3.31.0}"
QT_VER="${QT_VER:-6.10.2}"
KF_VER="${KF_VER:-6.24.0}"

mkdir -p "$LOGS"
exec 9>&2

log() { printf '%s\n' "$*" | tee -a "$LOGS/00-spike-console.log"; }

in_image() { docker exec "$CONTAINER" bash -lc "$1"; }

start_container() {
    docker rm -f "$CONTAINER" >/dev/null 2>&1
    docker run -d --name "$CONTAINER" -v "$ROOT":/vedit-build -w /vedit-build "$IMAGE" sleep infinity >/dev/null
    log "container $CONTAINER up from $IMAGE"
}

# --- stage 1: toolchain ------------------------------------------------------
stage_toolchain() {
    in_image '
        cat > /etc/yum.repos.d/tuna-sclo.repo <<EOF
[centos-sclo-rh]
name=CentOS-7.9.2009 SCLo rh (tuna vault)
baseurl=https://mirrors.tuna.tsinghua.edu.cn/centos-vault/7.9.2009/sclo/x86_64/rh/
gpgcheck=0
enabled=1
[centos-sclo-sclo]
name=CentOS-7.9.2009 SCLo sclo (tuna vault)
baseurl=https://mirrors.tuna.tsinghua.edu.cn/centos-vault/7.9.2009/sclo/x86_64/sclo/
gpgcheck=0
enabled=1
EOF
        rm -f /etc/yum.repos.d/CentOS-SCLo-*.repo
        yum install -y scl-utils devtoolset-11-gcc devtoolset-11-gcc-c++ devtoolset-11-runtime
        scl enable devtoolset-11 -- g++ --version | head -1
    ' >>"$LOGS/01-devtoolset-install.log" 2>&1
    # NOTE gpgcheck=0: the SCL SIG key is not in the image. A real delivery build
    # must import the SCLo signing key instead (not a feasibility question).
}

# --- stage 2: cmake + Qt6 source --------------------------------------------
stage_fetch() {
    in_image "
        cd /spike 2>/dev/null || { mkdir -p /spike && cd /spike; }
        curl -fsSLO https://github.com/Kitware/CMake/releases/download/v${CMAKE_VER}/cmake-${CMAKE_VER}-linux-x86_64.tar.gz
        tar xzf cmake-${CMAKE_VER}-linux-x86_64.tar.gz
        ./cmake-${CMAKE_VER}-linux-x86_64/bin/cmake --version | head -1
        curl -fsSLO https://download.qt.io/official_releases/qt/6.10/${QT_VER}/submodules/qtbase-everywhere-src-${QT_VER}.tar.xz
        tar xJf qtbase-everywhere-src-${QT_VER}.tar.xz
        curl -fsSLO https://download.qt.io/official_releases/qt/6.10/${QT_VER}/submodules/qttools-everywhere-src-${QT_VER}.tar.xz
        tar xJf qttools-everywhere-src-${QT_VER}.tar.xz
    " >>"$LOGS/03-fetch-cmake-qt.log" 2>&1
}

# --- stage 3: Qt6 Core/Gui/Widgets ------------------------------------------
stage_qtbase() {
    # -qt-freetype -qt-harfbuzz: CentOS 7's FreeType 2.8 lacks FT_Done_MM_Var,
    # the system one breaks src/gui/text/freetype/qfontengine_ft.cpp.
    in_image "
        source scl_source enable devtoolset-11
        export PATH=/spike/cmake-${CMAKE_VER}-linux-x86_64/bin:\$PATH
        rm -rf /spike/qt6-gui-build && mkdir -p /spike/qt6-gui-build && cd /spike/qt6-gui-build
        time /spike/qtbase-everywhere-src-${QT_VER}/configure -prefix /opt/qt6-gui -release \
            -no-dbus -no-icu -no-glib -no-openssl -no-opengl -no-pch -qt-freetype -qt-harfbuzz \
            -nomake examples -nomake tests -confirm-license -opensource
        echo CONFIGURE_EXIT=\$?
        time cmake --build . --parallel \$(nproc); echo BUILD_EXIT=\$?
        cmake --install .; echo INSTALL_EXIT=\$?
        du -sh /opt/qt6-gui
    " >>"$LOGS/09-qtbase-gui-build.log" 2>&1
}

# --- stage 4: minimal Qt6 Widgets program + GLIBC measurement ---------------
stage_widgets() {
    docker cp "$SPIKE/spike/qt6_widgets.cpp" "$CONTAINER:/spike/qt6_widgets.cpp"
    in_image '
        source scl_source enable devtoolset-11
        B=/opt/qt6-gui
        g++ -std=c++17 -O2 -fPIC /spike/qt6_widgets.cpp -o /spike/qt6_widgets \
            -I$B/include -I$B/include/QtCore -I$B/include/QtGui -I$B/include/QtWidgets \
            -L$B/lib -lQt6Widgets -lQt6Gui -lQt6Core -Wl,-rpath,$B/lib
        echo LINK_EXIT=$?
        QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORM_PLUGIN_PATH=$B/plugins/platforms /spike/qt6_widgets
        echo RUN_EXIT=$?
        for f in $B/lib/libQt6Core.so.6.10.2 $B/lib/libQt6Gui.so.6.10.2 \
                 $B/lib/libQt6Widgets.so.6.10.2 $B/plugins/platforms/libqoffscreen.so /spike/qt6_widgets; do
            echo "--- $f"
            echo -n "  max GLIBC: "; readelf --dyn-syms --wide "$f" | grep -o "GLIBC_[0-9.]*" | sort -Vu | tail -1
            echo -n "  refs needing >2.17: "
            readelf --dyn-syms --wide "$f" | grep -o "GLIBC_[0-9.]*" | awk -F_ "{v=\$2; split(v,a,\".\"); maj=a[1]+0; min=a[2]+0; if (maj>2 || (maj==2 && min>17)) c++} END {print c+0}"
        done
    ' >>"$LOGS/10-widgets-link-measure.log" 2>&1
}

# --- stage 5: clean-room run in a stock centos:7 ----------------------------
stage_cleanroom() {
    in_image 'cd /spike && tar czf /vedit-build/target/t76-qt6-native.tar.gz qt6_widgets -C /opt qt6-gui -C /spike/qt6-gui-build plugins/platforms/libqoffscreen.so'
    docker run --rm -v "$ROOT/target":/art docker.m.daocloud.io/library/centos:7 bash -lc '
        mkdir -p /run-qt && tar xzf /art/t76-qt6-native.tar.gz -C /run-qt
        cat > /etc/yum.repos.d/tuna-vault.repo <<EOF
[vault-base]
name=CentOS-7.9.2009 base (tuna vault)
baseurl=https://mirrors.tuna.tsinghua.edu.cn/centos-vault/7.9.2009/os/x86_64/
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-CentOS-7
enabled=1
EOF
        rm -f /etc/yum.repos.d/CentOS-*.repo
        yum install -y libpng >/dev/null 2>&1; echo YUM_LIBPNG_EXIT=$?
        LD_LIBRARY_PATH=/run-qt/qt6-gui/lib QT_QPA_PLATFORM=offscreen \
            QT_QPA_PLATFORM_PLUGIN_PATH=/run-qt/plugins /run-qt/qt6_widgets
        echo CLEANROOM_RUN_EXIT=$?
        ldd --version | head -1
    ' >>"$LOGS/15-cleanroom-centos7.log" 2>&1
}

# --- stage 6: one real KF6 framework ---------------------------------------
stage_kf6() {
    in_image "
        source scl_source enable devtoolset-11
        export PATH=/spike/cmake-${CMAKE_VER}-linux-x86_64/bin:\$PATH
        export LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8 PYTHONUTF8=1
        cd /spike
        curl -fsSLO https://download.kde.org/stable/frameworks/${KF_VER}/extra-cmake-modules-${KF_VER}.tar.xz
        curl -fsSLO https://download.kde.org/stable/frameworks/${KF_VER}/syntax-highlighting-${KF_VER}.tar.xz
        tar xJf extra-cmake-modules-${KF_VER}.tar.xz; tar xJf syntax-highlighting-${KF_VER}.tar.xz
        mkdir -p ecm-build && cd ecm-build
        cmake ../extra-cmake-modules-${KF_VER} -G Ninja -DCMAKE_INSTALL_PREFIX=/opt/kf6 -DBUILD_TESTING=OFF && ninja install
        cd /spike && mkdir -p syntaxhl-build && cd syntaxhl-build
        cmake ../syntax-highlighting-${KF_VER} -G Ninja -DCMAKE_INSTALL_PREFIX=/opt/kf6 \
            -DCMAKE_PREFIX_PATH=\"/opt/qt6-gui;/opt/kf6\" -DBUILD_TESTING=OFF -DBUILD_QCH=OFF
        ninja; echo SHL_BUILD_EXIT=\$?
        LIB=\$(ls bin/libKF6SyntaxHighlighting.so.6.* | head -1)
        echo -n 'max GLIBC: '; readelf --dyn-syms --wide \$LIB | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1
    " >>"$LOGS/14-kf6-syntaxhighlighting.log" 2>&1
}

main() {
    start_container
    stage_toolchain
    stage_fetch
    stage_qtbase
    stage_widgets
    stage_cleanroom
    stage_kf6
    log "== done: see $LOGS =="
}

main "$@"
