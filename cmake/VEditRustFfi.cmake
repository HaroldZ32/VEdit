# Rust service layer (vd-ffi) — Slice 0, task T4.
#
# The Rust product services are built as one static library by
# `bash scripts/build-qt.sh` BEFORE configure runs, in the pinned Rust/CentOS 7
# image (the Qt6 build image has no Rust toolchain, and the Rust image is the one
# whose glibc floor the Rust-only product already uses). This module turns the
# produced `libvd_ffi.a` plus the cbindgen header directory into an imported
# target the C++ side links:
#
#   vd_ffi    imported static library, INTERFACE include dir = the header dir
#
# Why a hard error instead of an optional target: a product binary that silently
# links no service layer would pass every GUI check while the seven services do
# not exist in it — exactly the false green docs/plans/v2-slices.md §1.2 is about.
# `VEDIT_BUILD_RUST_FFI=OFF` exists only for a deliberately reduced probe tree;
# the default build requires the library and the ABI selftest.

set(VEDIT_RUST_FFI_LIB "" CACHE FILEPATH
  "libvd_ffi.a produced by `cargo build -p vd-ffi --release`")
set(VEDIT_RUST_FFI_INCLUDE "" CACHE PATH
  "Directory containing the cbindgen-generated vd_ffi.h")
option(VEDIT_BUILD_RUST_FFI "Require and link the Rust service layer (vd-ffi)" ON)

if(NOT VEDIT_BUILD_RUST_FFI)
  message(STATUS "VEdit: VEDIT_BUILD_RUST_FFI=OFF: the Rust service layer is NOT linked")
  return()
endif()

if(NOT VEDIT_RUST_FFI_LIB OR NOT EXISTS "${VEDIT_RUST_FFI_LIB}")
  message(FATAL_ERROR
    "VEdit: the Rust service library is missing (VEDIT_RUST_FFI_LIB='${VEDIT_RUST_FFI_LIB}').\n"
    "       Run `bash scripts/build-qt.sh`: it builds libvd_ffi.a in the pinned Rust image\n"
    "       and passes -DVEDIT_RUST_FFI_LIB=<path> -DVEDIT_RUST_FFI_INCLUDE=<dir> here.")
endif()
if(NOT VEDIT_RUST_FFI_INCLUDE OR NOT EXISTS "${VEDIT_RUST_FFI_INCLUDE}/vd_ffi.h")
  message(FATAL_ERROR
    "VEdit: the cbindgen header is missing under VEDIT_RUST_FFI_INCLUDE='${VEDIT_RUST_FFI_INCLUDE}'.\n"
    "       `cargo build -p vd-ffi` regenerates crates/vd-ffi/include/vd_ffi.h.")
endif()

add_library(vd_ffi STATIC IMPORTED)
set_target_properties(vd_ffi PROPERTIES
  IMPORTED_LOCATION "${VEDIT_RUST_FFI_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${VEDIT_RUST_FFI_INCLUDE}")

# The Rust standard library is statically inside libvd_ffi.a; what it still needs
# from the system is the C runtime, pthreads, libm and the dynamic loader. The
# deliverable supplies all of them from its private runtime (docs/BUILD.md §12).
find_package(Threads REQUIRED)
target_link_libraries(vd_ffi INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)

message(STATUS "VEdit: Rust service layer vd-ffi -> ${VEDIT_RUST_FFI_LIB}")
message(STATUS "VEdit: Rust C header           -> ${VEDIT_RUST_FFI_INCLUDE}/vd_ffi.h")
