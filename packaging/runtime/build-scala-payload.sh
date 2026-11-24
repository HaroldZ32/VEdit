#!/usr/bin/env bash
# Build the OFFLINE Scala payload that packaging/runtime/assemble-appdir.sh
# installs into the AppDir (T12 / AC-7.1, 7.2, 7.4, 7.11 packaging half):
#
#   JDK 21 (Temurin)          — GPLv2 + Classpath Exception, glibc 2.17+ build
#   scala-cli                 — the STATIC native image (no glibc requirement)
#   sbt 1.x                   — launcher scripts + lib/, with PREWARMED
#                               boot / ivy / coursier homes
#   coursier `cs`             — static binary, the Metals launcher and the
#                               offline resolver whose cache ships
#   Metals payload            — metals_2.13 + Bloop + mtags for 2.13.12 and
#                               2.13.18, resolved ONCE at build time into a
#                               coursier cache that travels with the AppDir
#   two example toolchains    — SpinalHDL 1.15.1 (Scala 2.13.12) and Chisel
#                               7.16.0 (Scala 2.13.18 + firtool), each warmed
#                               into its OWN home tree so the two can never
#                               resolve through each other (T18 / AC-7.6, 7.3,
#                               7.7; offline proof in 02-offline.log)
#
# Runs INSIDE the pinned Qt6 image (it has a JVM-free glibc and network):
#
#   docker run --rm -v "$PWD:/vedit-build" -w /vedit-build vedit-qt6-build:local \
#     bash packaging/runtime/build-scala-payload.sh
#
# Every input is pinned (scripts/versions-scala.env) and its sha256 verified; the
# result is a self-contained tree under packaging/runtime/out/scala-payload/ that
# the assembler copies without fetching anything.
set -euo pipefail

ROOT=${ROOT:-/vedit-build}
OUT="$ROOT/packaging/runtime/out/scala-payload"
CACHE="$ROOT/packaging/runtime/out/scala-cache"
LICENCES="$ROOT/packaging/runtime/assets/licenses"

while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT=${2:?}; shift 2 ;;
    --cache) CACHE=${2:?}; shift 2 ;;
    -h|--help) sed -n '2,26p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

log() { printf '[scala-payload] %s\n' "$*"; }

# shellcheck source=../../scripts/versions-scala.env
set -a; . "$ROOT/scripts/versions-scala.env"; set +a
: "${SCALA_JDK_URL:?}" "${SCALA_JDK_SHA256:?}"
: "${SCALA_CLI_URL:?}" "${SCALA_CLI_SHA256:?}"
: "${SBT_URL:?}" "${SBT_SHA256:?}"
: "${CS_URL:?}" "${CS_SHA256:?}"
: "${METALS_COORD:?}" "${BLOOP_COORD:?}" "${MTAGS_MAIN_COORD:?}" "${MTAGS_LEGACY_COORD:?}"
: "${SPINALHDL_CORE_COORD:?}" "${SPINALHDL_LIB_COORD:?}" "${SPINALHDL_SCALA_VERSION:?}"
: "${CHISEL_COORD:?}" "${CHISEL_PLUGIN_COORD:?}" "${CHISEL_SCALA_VERSION:?}" "${FIRTOOL_RESOLVER_COORD:?}"

mkdir -p "$CACHE" "$OUT" "$OUT/licenses"

# Pinned download with a hard sha256 check (same rule as the VHDL payload).
fetch() {
  local url=$1 sha=$2 dest=$3
  if [ -f "$dest" ] && [ "$(sha256sum "$dest" | cut -d' ' -f1)" = "$sha" ]; then
    log "cached $(basename "$dest") sha256=$sha"
    return 0
  fi
  rm -f "$dest"
  log "fetch $url"
  curl -sSL --retry 3 --retry-delay 2 -o "$dest" "$url" || {
    echo "ERROR: download failed: $url" >&2; return 1; }
  local actual
  actual=$(sha256sum "$dest" | cut -d' ' -f1)
  [ "$actual" = "$sha" ] || {
    echo "ERROR: sha256 mismatch for $dest: expected $sha got $actual" >&2; return 1; }
  log "sha256 OK $(basename "$dest")"
}

# --- JDK 21 (Temurin) --------------------------------------------------------
fetch "$SCALA_JDK_URL" "$SCALA_JDK_SHA256" "$CACHE/jdk.tar.gz"
rm -rf "$OUT/jdk"
mkdir -p "$OUT/jdk"
tar xzf "$CACHE/jdk.tar.gz" -C "$OUT/jdk" --strip-components=1
[ -x "$OUT/jdk/bin/java" ] || { echo "ERROR: jdk/bin/java missing" >&2; exit 1; }
# Build-only parts of the JDK: jmods/ is for jlink, src.zip for IDEs. Neither is
# used at runtime, and together they are ~150 MB of the AppDir.
rm -rf "$OUT/jdk/jmods" "$OUT/jdk/lib/src.zip" "$OUT/jdk/man"
log "JDK $("$OUT/jdk/bin/java" -version 2>&1 | head -1)"

# --- scala-cli (static) ------------------------------------------------------
fetch "$SCALA_CLI_URL" "$SCALA_CLI_SHA256" "$CACHE/scala-cli.gz"
rm -rf "$OUT/scala-cli"
mkdir -p "$OUT/scala-cli"
gzip -dc "$CACHE/scala-cli.gz" > "$OUT/scala-cli/scala-cli"
chmod 0755 "$OUT/scala-cli/scala-cli"
log "scala-cli $("$OUT/scala-cli/scala-cli" version --offline 2>/dev/null | head -1 || echo '<version offline>')"

# --- coursier `cs` (static) --------------------------------------------------
fetch "$CS_URL" "$CS_SHA256" "$CACHE/cs.gz"
rm -rf "$OUT/cs"
mkdir -p "$OUT/cs"
gzip -dc "$CACHE/cs.gz" > "$OUT/cs/cs"
chmod 0755 "$OUT/cs/cs"
log "cs $("$OUT/cs/cs" version 2>/dev/null | head -1)"

# --- sbt 1.x ----------------------------------------------------------------
fetch "$SBT_URL" "$SBT_SHA256" "$CACHE/sbt.tgz"
rm -rf "$OUT/sbt"
mkdir -p "$OUT/sbt"
tar xzf "$CACHE/sbt.tgz" -C "$OUT/sbt" --strip-components=1
[ -x "$OUT/sbt/bin/sbt" ] || { echo "ERROR: sbt/bin/sbt missing" >&2; exit 1; }
# sbtn is the thin CLIENT for attaching to a running sbt server. It is built
# against glibc 2.34 (readelf -V: GLIBC_2.34), so it cannot run on the CentOS 7
# baseline, and nothing in this product uses it (`sbt` without --client drives
# the launcher directly). Shipping a binary that cannot start would fail the
# symbol gate for no benefit.
rm -f "$OUT/sbt/bin/sbtn-"*
log "sbt launcher $(grep -m1 '^sbt.version\|^version' "$OUT/sbt/conf/sbtopts" 2>/dev/null || echo "$SBT_VERSION")"

JAVA_HOME="$OUT/jdk"
export JAVA_HOME
export PATH="$OUT/jdk/bin:$PATH"

# --- prewarm the sbt boot / ivy / coursier homes -----------------------------
# A throwaway project with the SAME sbt and Scala version the product lane uses.
# The three homes are redirected INTO the payload, which is exactly what the
# AppDir wrappers set at run time (`SBT_OPTS` + `-Dsbt.boot.directory` ...).
SBT_HOME="$OUT/sbt-home"
PREWARM="$CACHE/prewarm-sbt"
if [ -f "$SBT_HOME/.prewarmed" ]; then
  log "sbt homes already prewarmed at $SBT_HOME"
else
  rm -rf "$SBT_HOME" "$PREWARM"
  mkdir -p "$SBT_HOME/home" "$PREWARM/project" "$PREWARM/src/main/scala"
  printf 'sbt.version=%s\n' "$SBT_VERSION" > "$PREWARM/project/build.properties"
  printf 'scalaVersion := "%s"\n' "$SCALA_MAIN_VERSION" > "$PREWARM/build.sbt"
  printf 'object Hello { def main(args: Array[String]): Unit = println("hello") }\n' \
    > "$PREWARM/src/main/scala/Hello.scala"
  log "prewarming sbt (boot/ivy/coursier) — one online run, then offline forever"
  ( cd "$PREWARM" && HOME="$SBT_HOME/home" COURSIER_CACHE="$SBT_HOME/coursier" \
      "$OUT/sbt/bin/sbt" -batch -Duser.home="$SBT_HOME/home" \
      -Dsbt.boot.directory="$SBT_HOME/boot" \
      -Dsbt.ivy.home="$SBT_HOME/ivy" \
      -Dsbt.coursier.home="$SBT_HOME/coursier" \
      -Dsbt.global.base="$SBT_HOME/global" \
      -Dsbt.override.build.repos=false \
      "compile" > "$SBT_HOME/prewarm.log" 2>&1 ) || {
    echo "ERROR: sbt prewarm failed (log: $SBT_HOME/prewarm.log)" >&2
    tail -30 "$SBT_HOME/prewarm.log" >&2
    exit 1
  }
  : > "$SBT_HOME/.prewarmed"
  log "sbt homes consumed: $(du -sh "$SBT_HOME" | cut -f1)"
fi

# --- prewarm the scala-cli cache --------------------------------------------
# scala-cli resolves the compiler/library through a coursier cache; shipping that
# cache is what makes `scala-cli run --offline` work with no network.
SCALA_CLI_CACHE="$OUT/scala-cli-cache"
if [ -f "$SCALA_CLI_CACHE/.prewarmed" ]; then
  log "scala-cli cache already prewarmed"
else
  rm -rf "$SCALA_CLI_CACHE" "$CACHE/prewarm-scala-cli"
  mkdir -p "$SCALA_CLI_CACHE" "$CACHE/prewarm-scala-cli"
  printf 'object Hello { def main(args: Array[String]): Unit = println("hello") }\n' \
    > "$CACHE/prewarm-scala-cli/Hello.scala"
  printf '//> using scala %s\n' "$SCALA_MAIN_VERSION" \
    > "$CACHE/prewarm-scala-cli/project.scala"
  log "prewarming scala-cli cache (one online compile)"
  COURSIER_CACHE="$SCALA_CLI_CACHE" \
    "$OUT/scala-cli/scala-cli" compile --server=false \
      "$CACHE/prewarm-scala-cli" > "$SCALA_CLI_CACHE/prewarm.log" 2>&1 || {
    echo "ERROR: scala-cli prewarm failed (log: $SCALA_CLI_CACHE/prewarm.log)" >&2
    tail -30 "$SCALA_CLI_CACHE/prewarm.log" >&2
    exit 1
  }
  : > "$SCALA_CLI_CACHE/.prewarmed"
  log "scala-cli cache consumed: $(du -sh "$SCALA_CLI_CACHE" | cut -f1)"
fi

# --- Metals payload (metals + Bloop + mtags for both Scala versions) --------
CS_CACHE="$OUT/coursier-cache"
if [ -f "$CS_CACHE/.prewarmed" ]; then
  log "coursier cache already prewarmed"
else
  rm -rf "$CS_CACHE"
  mkdir -p "$CS_CACHE"
  export COURSIER_CACHE="$CS_CACHE"
  log "resolving the Metals payload (one online fetch)"
  # `--no-default` would leave NO repository (it removes Maven Central too); the
  # payload must come from one explicit, deterministic repository.
  "$OUT/cs/cs" fetch --repository central \
      "$METALS_COORD" "$BLOOP_COORD" "$MTAGS_MAIN_COORD" "$MTAGS_LEGACY_COORD" \
      > "$CS_CACHE/fetch.log" 2>&1 || {
    echo "ERROR: coursier fetch failed (log: $CS_CACHE/fetch.log)" >&2
    tail -30 "$CS_CACHE/fetch.log" >&2
    exit 1
  }
  # `cs fetch` resolves the classpath; `cs launch` (the exact command the AppDir
  # wrapper runs) resolves what a LAUNCH needs, and the two graphs can ask for
  # different versions of the same artifact (observed: unroll-annotation 0.3.0
  # vs 0.1.12). Prewarming the launch itself is what makes the shipped cache
  # sufficient — without it the offline launch fails on that missing pom.
  log "prewarming the Metals launcher exactly as bin/metals runs it"
  COURSIER_CACHE="$CS_CACHE" "$OUT/cs/cs" launch --repository central \
      "$METALS_COORD" -- --version > "$CS_CACHE/launch.log" 2>&1 || {
    echo "ERROR: Metals launcher prewarm failed (log: $CS_CACHE/launch.log)" >&2
    tail -30 "$CS_CACHE/launch.log" >&2
    exit 1
  }
  # And prove the SAME cache answers with no network, at build time.
  COURSIER_CACHE="$CS_CACHE" COURSIER_MODE=offline "$OUT/cs/cs" launch \
      --repository central "$METALS_COORD" -- --version \
      > "$CS_CACHE/launch-offline.log" 2>&1 || {
    echo "ERROR: the prewarmed cache is not enough for an offline Metals launch" >&2
    tail -30 "$CS_CACHE/launch-offline.log" >&2
    exit 1
  }
  : > "$CS_CACHE/.prewarmed"
  log "coursier cache consumed: $(du -sh "$CS_CACHE" | cut -f1)"
fi

# --- the T20 spike's gap preloads (#2-#5 of its network-point sweep) ---------
# Resolving the Metals coordinates alone does NOT bring in what the
# Metals <-> scala-cli BSP path resolves LATER, at session time; offline each of
# these used to fail with "Error downloading …"
# (evidence/batch-20261110b/scala-metals/spike/README.md §S5 #2-#5 and
# logs 10/12/13/14). They are resolved into the SAME shipped cache that the
# bin/metals wrapper exports (COURSIER_CACHE), so the offline session never
# asks Maven Central for them:
#
#   #2 com.sourcegraph:semanticdb-javac      — Java semanticdb, resolved by
#      Metals itself; the embedded-coursier fallback is then never needed.
#   #3 ch.epfl.scala:bloop-frontend_2.12:2.1.2 — the Bloop that scala-cli's BSP
#      launches (a DIFFERENT build than the bundled 1.5.18).
#   #4 org.scalameta:semanticdb-scalac_2.13.18 + org.scala-lang:scala2-sbt-bridge
#      — the Scala 2.13.18 compile bridge and its semanticdb plugin.
#   #5 org.scala-lang:scala3-compiler_3/scala3-sbt-bridge — the Scala 3 default
#      scala-cli picks for a project that pins no version (3.9.0 for scala-cli
#      1.18.0, measured in spike log 13).
#
# The versions live here (not in versions-scala.env, which is shared with other
# packaging tasks) and move with METALS_VERSION / SCALA_CLI_VERSION: they are
# the coordinates THOSE two versions ask for, not independent upgrades.
: "${SEMANTICDB_JAVAC_VERSION:=0.12.3}"
: "${BLOOP_BSP_VERSION:=2.1.2}"
: "${SEMANTICDB_SCALAC_VERSION:=4.17.4}"
: "${SCALA3_DEFAULT_VERSION:=3.9.0}"
GAP_COORDS=(
  "com.sourcegraph:semanticdb-javac:${SEMANTICDB_JAVAC_VERSION}"
  "ch.epfl.scala:bloop-frontend_2.12:${BLOOP_BSP_VERSION}"
  "org.scalameta:semanticdb-scalac_${SCALA_MAIN_VERSION}:${SEMANTICDB_SCALAC_VERSION}"
  "org.scala-lang:scala2-sbt-bridge:${SCALA_MAIN_VERSION}"
  "org.scala-lang:scala3-compiler_3:${SCALA3_DEFAULT_VERSION}"
  "org.scala-lang:scala3-sbt-bridge:${SCALA3_DEFAULT_VERSION}"
)
# The Bloop BSP launcher's OWN runtime pieces. They belong to a launcher graph,
# not to bloop-frontend's compile graph (its POM leaves them out), so a plain
# `cs fetch` downloads their POMs and NOT their jars — and without the jars
# scala-cli's BSP dies with `Error getting Bloop class path` before it can
# answer `build/initialize` (the exact failure of spike logs 12/13). They are
# therefore fetched `--intransitive`, which asks for the artifact itself.
GAP_RUNTIME_COORDS=(
  "org.scala-lang:scala-library:2.12.21"
  "org.scala-lang:scala2-sbt-bridge:${SCALA_LEGACY_VERSION}"
  "io.get-coursier:interface:1.0.28"
  "org.jline:jline-terminal:3.27.1"
  "org.jline:jline-terminal-jni:3.27.1"
  "org.jline:jline-native:3.27.1"
)
if [ -f "$CS_CACHE/.gaps-preloaded" ]; then
  log "T20 gap coordinates already present in $CS_CACHE"
else
  log "preloading the T20 gap coordinates (semanticdb-javac, BSP Bloop, both sbt bridges, Scala 3 default)"
  # Resolve into a FRESH cache, then merge it into the shipped one. Resolving
  # directly into a cache that already holds Metals/mtags lets conflict
  # resolution pick the versions ALREADY there and fetch only the POMs of these
  # (observed: scala-library 2.12.21 / jline 3.27.1 pom-only, no jar), which is
  # exactly the failure this block exists to remove. The spike preloaded into a
  # fresh cache for the same reason (logs/07, logs/08).
  GAP_CACHE="$CACHE/preload-gaps"
  rm -rf "$GAP_CACHE"
  mkdir -p "$GAP_CACHE"
  {
    COURSIER_CACHE="$GAP_CACHE" "$OUT/cs/cs" fetch --repository central \
        "${GAP_COORDS[@]}" > "$CS_CACHE/preload-gaps.log" 2>&1 || {
      echo "ERROR: gap preload failed (log: $CS_CACHE/preload-gaps.log)" >&2
      tail -30 "$CS_CACHE/preload-gaps.log" >&2
      exit 1
    }
    COURSIER_CACHE="$GAP_CACHE" "$OUT/cs/cs" fetch --repository central --intransitive \
        "${GAP_RUNTIME_COORDS[@]}" >> "$CS_CACHE/preload-gaps.log" 2>&1 || {
      echo "ERROR: gap runtime preload failed (log: $CS_CACHE/preload-gaps.log)" >&2
      tail -30 "$CS_CACHE/preload-gaps.log" >&2
      exit 1
    }
  }
  # Same URL-keyed layout as the shipped cache, so a plain merge is safe.
  ( cd "$GAP_CACHE" && tar cf - . ) | ( cd "$CS_CACHE" && tar xf - )
  : > "$CS_CACHE/.gaps-preloaded"
fi

# #5's run lane: scala-cli's OWN cache must answer the same default too, so an
# offline `scala-cli run` of a project with no `using scala` directive works.
# Compiling the unpinned project IS the prewarm — no version is assumed here.
if [ -f "$SCALA_CLI_CACHE/.scala3-prewarmed" ]; then
  log "scala-cli Scala-3 default already prewarmed"
else
  rm -rf "$CACHE/prewarm-scala-cli3"
  mkdir -p "$CACHE/prewarm-scala-cli3"
  cat > "$CACHE/prewarm-scala-cli3/Hello3.scala" <<'SCALA'
// Deliberately NO `using scala` directive: this project must resolve whatever
// Scala 3 version scala-cli defaults to, which is exactly the offline gap.
object Hello3 { def main(args: Array[String]): Unit = println("hello3") }
SCALA
  log "prewarming the scala-cli Scala-3 default (one online compile)"
  COURSIER_CACHE="$SCALA_CLI_CACHE" \
    "$OUT/scala-cli/scala-cli" compile --server=false --jvm system \
      "$CACHE/prewarm-scala-cli3" > "$SCALA_CLI_CACHE/prewarm-scala3.log" 2>&1 || {
    echo "ERROR: scala-cli Scala-3 prewarm failed (log: $SCALA_CLI_CACHE/prewarm-scala3.log)" >&2
    tail -30 "$SCALA_CLI_CACHE/prewarm-scala3.log" >&2
    exit 1
  }
  : > "$SCALA_CLI_CACHE/.scala3-prewarmed"
fi

# Record WHICH mtags each Scala version really got (the two pin at different
# Metals versions on purpose — mtags is published per Scala version).
{
  echo "# Metals payload (resolved at build time)"
  echo "$METALS_COORD"
  echo "$BLOOP_COORD"
  echo "$MTAGS_MAIN_COORD"
  echo "$MTAGS_LEGACY_COORD"
  echo "# T20 gap preloads (#2-#5), resolved into the same cache"
  printf '%s\n' "${GAP_COORDS[@]}"
  echo "# jars in the coursier cache"
  find "$CS_CACHE" -name '*.jar' -printf '%f\n' | sort | sed -n '1,400p'
} > "$OUT/METALS-PAYLOAD.txt"

# --- two PREWARMED example toolchains (T18 / AC-7.6, AC-7.3, AC-7.7) --------
# The two hardware-construction stacks the product promises to run offline:
#
#   examples/spinalhdl  SpinalHDL 1.15.1 on Scala 2.13.12
#   examples/chisel     Chisel 7.16.0 on Scala 2.13.18 + the firtool backend
#
# Each example owns a COMPLETE home tree of its own (sbt boot/ivy/coursier/
# global, its own coursier cache and, for Chisel, its own firtool cache), and
# the build FAILS if a jar from the other stack — or the other Scala compiler —
# turns up in it. That is the "prewarmed separately, never contaminating" half
# of the requirement; the offline half is `-Dsbt.offline=true` with no network,
# exercised by evidence/batch-20261110c/scala/02-offline.log.
#
# A user project that needs a dependency these two do not carry fails with the
# coordinate names it could not find; PREWARM-SCALA.txt records the exact
# re-warm command (one online run) that makes the same project work offline.
EXAMPLES="$OUT/examples"
PREWARM_HOMES="$OUT/prewarm-homes"
mkdir -p "$EXAMPLES" "$PREWARM_HOMES"

# --- the SpinalHDL example (2.13.12) ----------------------------------------
mkdir -p "$EXAMPLES/spinalhdl/src/main/scala"
cat > "$EXAMPLES/spinalhdl/build.sbt" <<SBT
ThisBuild / scalaVersion := "${SPINALHDL_SCALA_VERSION}"
ThisBuild / organization := "vedit.examples"

lazy val root = (project in file(".")).settings(
  name := "spinalhdl-counter",
  libraryDependencies ++= Seq(
    "com.github.spinalhdl" %% "spinalhdl-core" % "${SPINALHDL_VERSION}",
    "com.github.spinalhdl" %% "spinalhdl-lib" % "${SPINALHDL_VERSION}"
  )
)
SBT
cat > "$EXAMPLES/spinalhdl/src/main/scala/CounterMain.scala" <<'SCALA'
import spinal.core._

class Counter(width: Int) extends Component {
  val io = new Bundle {
    val en = in Bool ()
    val q  = out UInt (width bits)
  }
  val cnt = RegInit(U(0, width bits))
  when(io.en) { cnt := cnt + 1 }
  io.q := cnt
}

object CounterMain extends App {
  val report = SpinalConfig(targetDirectory = "rtl").generateVerilog(new Counter(8))
  val paths = report.rtlSourcesPaths.toSeq
  require(paths.nonEmpty, "SpinalHDL generated no RTL file")
  require(report.getRtlString.contains("module Counter"),
    "generated Verilog does not declare module Counter")
  println(s"SPINALHDL-OK width=8 rtl=${paths.mkString(",")}")
}
SCALA

# --- the Chisel example (2.13.18 + firtool) ---------------------------------
mkdir -p "$EXAMPLES/chisel/src/main/scala"
cat > "$EXAMPLES/chisel/build.sbt" <<SBT
ThisBuild / scalaVersion := "${CHISEL_SCALA_VERSION}"
ThisBuild / organization := "vedit.examples"

lazy val root = (project in file(".")).settings(
  name := "chisel-counter",
  libraryDependencies += "org.chipsalliance" %% "chisel" % "${CHISEL_VERSION}",
  scalacOptions ++= Seq("-language:reflectiveCalls", "-deprecation", "-feature"),
  addCompilerPlugin("org.chipsalliance" % "chisel-plugin" % "${CHISEL_VERSION}" cross CrossVersion.full)
)
SBT
cat > "$EXAMPLES/chisel/src/main/scala/CounterMain.scala" <<'SCALA'
import chisel3._
import circt.stage.ChiselStage

class Counter(width: Int) extends Module {
  val io = IO(new Bundle {
    val en  = Input(Bool())
    val out = Output(UInt(width.W))
  })
  val cnt = RegInit(0.U(width.W))
  when(io.en) { cnt := cnt + 1.U }
  io.out := cnt
}

object CounterMain extends App {
  val sv = ChiselStage.emitSystemVerilog(new Counter(8))
  val out = new java.io.File("rtl/Counter.sv")
  out.getParentFile.mkdirs()
  java.nio.file.Files.write(out.toPath, sv.getBytes("UTF-8"))
  require(sv.contains("module Counter"),
    "generated SystemVerilog does not declare module Counter")
  println("CHISEL-OK width=8 rtl=rtl/Counter.sv")
}
SCALA

# One ONLINE run per example warms everything that run needs: the sbt launcher,
# the Scala compiler, the whole library graph, the Chisel compiler plugin and
# (for Chisel) the firtool native binary. After it, the example runs with no
# network at all.
prewarm_example() {
  local name=$1 home="$PREWARM_HOMES/$1" dir="$EXAMPLES/$1"
  if [ -f "$home/.prewarmed" ]; then
    log "example $name: cache already prewarmed ($(du -sh "$home" | cut -f1))"
    return 0
  fi
  rm -rf "$home" "$dir/target" "$dir/rtl" "$dir/project/target" "$dir/project/project"
  mkdir -p "$home/home"
  log "prewarming example $name (one online run, then offline forever)"
  ( cd "$dir" && \
    HOME="$home/home" COURSIER_CACHE="$home/coursier" \
    CHISEL_FIRTOOL_CACHE="$home/firtool-cache" \
    JAVA_HOME="$OUT/jdk" PATH="$OUT/jdk/bin:$PATH" \
    "$OUT/sbt/bin/sbt" -batch -Duser.home="$home/home" \
      -Dsbt.boot.directory="$home/boot" \
      -Dsbt.ivy.home="$home/ivy" \
      -Dsbt.coursier.home="$home/coursier" \
      -Dsbt.global.base="$home/global" \
      -Dsbt.override.build.repos=false \
      -Dsbt.offline=false \
      run > "$home/prewarm.log" 2>&1 ) || {
    echo "ERROR: example $name prewarm failed (log: $home/prewarm.log)" >&2
    tail -30 "$home/prewarm.log" >&2
    exit 1
  }
  : > "$home/.prewarmed"
  log "example $name cache consumed: $(du -sh "$home" | cut -f1)"
}

# The build-time contamination check: each home must carry ITS stack and only
# its stack. A hit means one example resolved through the other's jars, which
# would make the offline proof describe a cache the requirement forbids.
assert_example_isolation() {
  local sp="$PREWARM_HOMES/spinalhdl" ch="$PREWARM_HOMES/chisel" bad=0
  local hit
  hit=$(find "$sp" -name 'chisel_2.13-*.jar' -o -name 'chisel-plugin_2.13.18-*.jar' | head -3)
  [ -z "$hit" ] || { echo "ERROR: SpinalHDL cache contains Chisel jars:" >&2; echo "$hit" >&2; bad=1; }
  hit=$(find "$ch" -name 'spinalhdl-core_2.13-*.jar' -o -name 'spinalhdl-lib_2.13-*.jar' | head -3)
  [ -z "$hit" ] || { echo "ERROR: Chisel cache contains SpinalHDL jars:" >&2; echo "$hit" >&2; bad=1; }
  find "$sp" -name 'scala-compiler-2.13.12.jar' | grep -q . \
    || { echo "ERROR: SpinalHDL cache has no Scala 2.13.12 compiler" >&2; bad=1; }
  find "$ch" -name 'scala-compiler-2.13.18.jar' | grep -q . \
    || { echo "ERROR: Chisel cache has no Scala 2.13.18 compiler" >&2; bad=1; }
  find "$sp" -name 'scala-compiler-2.13.18.jar' | grep -q . \
    && { echo "ERROR: SpinalHDL cache carries the Chisel-era compiler 2.13.18" >&2; bad=1; }
  find "$ch" -name 'scala-compiler-2.13.12.jar' | grep -q . \
    && { echo "ERROR: Chisel cache carries the SpinalHDL-era compiler 2.13.12" >&2; bad=1; }
  [ -x "$(find "$ch/firtool-cache" -type f -name firtool 2>/dev/null | head -1)" ] \
    || { echo "ERROR: the Chisel cache carries no executable firtool binary" >&2; bad=1; }
  [ "$bad" -eq 0 ] || exit 1
  log "example caches isolated: SpinalHDL=2.13.12 only, Chisel=2.13.18+firtool only"
}

prewarm_example spinalhdl
prewarm_example chisel
assert_example_isolation

{
  echo "# Two prewarmed example toolchains (T18 / AC-7.6, AC-7.3, AC-7.7)"
  echo "# each home is COMPLETE and carries ONE stack; see test/isolation above."
  echo "SPINALHDL_VERSION=$SPINALHDL_VERSION"
  echo "SPINALHDL_SCALA_VERSION=$SPINALHDL_SCALA_VERSION"
  echo "SPINALHDL_COORDS=$SPINALHDL_CORE_COORD,$SPINALHDL_LIB_COORD"
  echo "SPINALHDL_HOME=prewarm-homes/spinalhdl"
  echo "SPINALHDL_BYTES=$(du -sb "$PREWARM_HOMES/spinalhdl" | cut -f1)"
  echo "SPINALHDL_FILES=$(find "$PREWARM_HOMES/spinalhdl" -type f | wc -l)"
  echo "CHISEL_VERSION=$CHISEL_VERSION"
  echo "CHISEL_SCALA_VERSION=$CHISEL_SCALA_VERSION"
  echo "CHISEL_COORDS=$CHISEL_COORD,$CHISEL_PLUGIN_COORD"
  echo "CHISEL_HOME=prewarm-homes/chisel"
  echo "CHISEL_BYTES=$(du -sb "$PREWARM_HOMES/chisel" | cut -f1)"
  echo "CHISEL_FILES=$(find "$PREWARM_HOMES/chisel" -type f | wc -l)"
  echo "FIRTOOL_RESOLVER_COORD=$FIRTOOL_RESOLVER_COORD"
  echo "FIRTOOL_CACHE=prewarm-homes/chisel/firtool-cache"
  echo "FIRTOOL_BINARY=prewarm-homes/chisel/firtool-cache/$(find "$PREWARM_HOMES/chisel/firtool-cache" -type f -name firtool | head -1 | sed "s|$PREWARM_HOMES/chisel/firtool-cache/||")"
  echo "# offline run of ONE example (no network at all). The shipped sbt launcher"
  echo '# starts `java`, so the payload JDK must be on PATH (T40 finding F1 -'
  echo '# without these two exports the command printed here exits 127):'
  echo '#   export JAVA_HOME=<payload>/jdk'
  echo '#   export PATH="$JAVA_HOME/bin:$PATH"'
  echo '#   HOME=<payload>/prewarm-homes/<name>/home \'
  echo '#   CHISEL_FIRTOOL_CACHE=<payload>/prewarm-homes/chisel/firtool-cache \'
  echo '#   <payload>/sbt/bin/sbt -batch -Dsbt.offline=true \'
  echo '#     -Dsbt.boot.directory=<payload>/prewarm-homes/<name>/boot \'
  echo '#     -Dsbt.ivy.home=<payload>/prewarm-homes/<name>/ivy \'
  echo '#     -Dsbt.coursier.home=<payload>/prewarm-homes/<name>/coursier \'
  echo '#     -Dsbt.global.base=<payload>/prewarm-homes/<name>/global \'
  echo '#     -Dsbt.override.build.repos=false run'
  echo '# (when the payload is mounted READ-ONLY, e.g. inside an AppImage, add'
  echo '#  -Dsbt.boot.lock=false; bin/scala-example passes it for you)'
  echo '# a dependency these caches do NOT carry fails with its coordinate; re-warm'
  echo '# it with ONE online run (same JDK exports), then the offline command works:'
  echo '#   <payload>/sbt/bin/sbt -batch -Dsbt.offline=false update'
} > "$OUT/PREWARM-SCALA.txt"

# --- licences ----------------------------------------------------------------
# Temurin ships the GPLv2 + Classpath Exception texts inside legal/.
mkdir -p "$OUT/licenses"
if [ -f "$OUT/jdk/legal/java.base/LICENSE" ]; then
  install -m 0644 "$OUT/jdk/legal/java.base/LICENSE" "$OUT/licenses/temurin-GPL-2.0.txt"
fi
if [ -f "$OUT/jdk/legal/java.base/ASSEMBLY_EXCEPTION" ]; then
  install -m 0644 "$OUT/jdk/legal/java.base/ASSEMBLY_EXCEPTION" \
    "$OUT/licenses/temurin-Classpath-Exception.txt"
fi
install -m 0644 "$LICENCES/Apache-2.0.txt" "$OUT/licenses/scala-cli-Apache-2.0.txt"
install -m 0644 "$LICENCES/Apache-2.0.txt" "$OUT/licenses/sbt-Apache-2.0.txt"
install -m 0644 "$LICENCES/Apache-2.0.txt" "$OUT/licenses/coursier-Apache-2.0.txt"
install -m 0644 "$LICENCES/Apache-2.0.txt" "$OUT/licenses/metals-Apache-2.0.txt"
install -m 0644 "$LICENCES/Apache-2.0.txt" "$OUT/licenses/bloop-Apache-2.0.txt"
install -m 0644 "$LICENCES/Apache-2.0.txt" "$OUT/licenses/scala-Apache-2.0.txt"

# --- source offer (GPLv2+CE requires one) ------------------------------------
cat > "$OUT/SOURCES.txt" <<SRC
# Source offer for the GPLv2 + Classpath Exception component (Temurin JDK 21).
# The AppDir ships binaries only; the corresponding sources are the upstream
# OpenJDK/Temurin source releases named below, available for at least three
# years from the URLs on the same page as the binary.
JDK=${SCALA_JDK_VERSION}
JDK_BINARY_URL=${SCALA_JDK_URL}
JDK_SOURCE_URL=https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.13%2B9/OpenJDK21U-jdk_x64_linux_hotspot_21.0.13_9.tar.gz
JDK_SOURCE_OFFER="the Temurin/OpenJDK 21 source tree at the tag of this release (jdk-21.0.13+9), obtainable from Adoptium/OpenJDK for at least three years"
SRC

# --- manifest ---------------------------------------------------------------
{
  echo "SCALA_JDK_VERSION=$SCALA_JDK_VERSION"
  echo "SCALA_JDK_SHA256=$SCALA_JDK_SHA256"
  echo "SCALA_CLI_VERSION=$SCALA_CLI_VERSION"
  echo "SCALA_CLI_SHA256=$SCALA_CLI_SHA256"
  echo "SBT_VERSION=$SBT_VERSION"
  echo "SBT_SHA256=$SBT_SHA256"
  echo "CS_VERSION=$CS_VERSION"
  echo "CS_SHA256=$CS_SHA256"
  echo "METALS_VERSION=$METALS_VERSION"
  echo "BLOOP_VERSION=$BLOOP_VERSION"
  echo "METALS_COORD=$METALS_COORD"
  echo "BLOOP_COORD=$BLOOP_COORD"
  echo "MTAGS_MAIN_COORD=$MTAGS_MAIN_COORD"
  echo "MTAGS_LEGACY_COORD=$MTAGS_LEGACY_COORD"
  echo "SCALA_MAIN_VERSION=$SCALA_MAIN_VERSION"
  echo "SCALA_LEGACY_VERSION=$SCALA_LEGACY_VERSION"
  echo "PAYLOAD_BYTES=$(du -sb "$OUT" | cut -f1)"
  echo "PAYLOAD_FILES=$(find "$OUT" -type f | wc -l)"
} > "$OUT/PAYLOAD.env"

# The size/file lines are SELF-REFERENTIAL: writing PAYLOAD.env changes both the
# byte count and the file count it records, so a value measured before the file
# exists is 44 bytes short (T40 finding F2). Measure AFTER the write, rewrite,
# and repeat until the number is stable.
for _ in 1 2 3; do
  bytes=$(du -sb "$OUT" | cut -f1)
  files=$(find "$OUT" -type f | wc -l)
  sed -i "s/^PAYLOAD_BYTES=.*/PAYLOAD_BYTES=$bytes/; s/^PAYLOAD_FILES=.*/PAYLOAD_FILES=$files/" "$OUT/PAYLOAD.env"
  [ "$(du -sb "$OUT" | cut -f1)" = "$bytes" ] && break
done

log "payload: $OUT bytes=$(du -sb "$OUT" | cut -f1) files=$(find "$OUT" -type f | wc -l)"
cat "$OUT/PAYLOAD.env"
