# Realtime diagnostics for an included child module

The Qt regression imports a filelist and waits for the real slang module catalog to locate `alternate` in `include/alternate.svh`. The catalog identifies `lib/alternate.sv` as the owner. That source sets a local macro before including the header. The test then edits an open buffer to instantiate `alternate` directly, while the source on disk still instantiates `original`.

Against `b614a4af`, the pinned real Qt and slang-server run failed with CTest exit 8. The catalog found `alternate`, the session still opened `original.sv` and `top.sv`, and the active document had both `unknown module 'alternate'` and `unknown module 'genuinely_missing'`. [Baseline log](red.log)

The realtime scope uses the bundled HDL grammar to identify changed literal instantiations and the filelist catalog to select each owning source. The catalog still supplies dependencies originating in includes and macro expansions. Existing one-level limits and filelist exclusions apply. Slang-server 0.3.1 uses its independently indexed shallow analysis after `didChange`. Providing the owner file alone still reproduced the false unknown-module result. In an indexed build session, VEdit follows the change with the pinned server's native compilation refresh and waits for the ordered command response before collecting the rows. The server uses the open in-memory text for this refresh. The regression checks that the source on disk has its original bytes. The upstream message queue and compilation refresh are in `JsonRpcServer` and `ServerDriver`. [Pinned slang-server source](https://github.com/hudson-trading/slang-server/tree/v0.3.1)

After the fix the session uses `lib/alternate.sv` and `rtl/top.sv`. Slang reports the intentional bad port and the genuine unknown module. Correcting the port clears its diagnostic over subsequent UI updates. The test checks that no project lint pass started. [Independent verification](../verification/README.md)

The follow-up regression saves through the application's Save action, switches away, then reopens the clean file while the existing catalog still describes its previous instance. It also removes a literal instance from a second buffer that continues to instantiate the same module through a macro. Both operations reproduced `unknown module 'alternate'` after the initial fix. [Follow-up baseline log](provenance-red.log)

The catalog publishes the location of each direct instance so a literal change removes an owner only when every known site for that owner can be traced to a literal instance in the catalogued disk text. Sites from includes or macro expansions keep their owner. When a file is newer than the catalog, current literal references contribute their owners and ambiguous catalog edges remain eligible within the existing one-level limits. Clean files older than the catalog use its scope without parsing their text for dependencies.

Both new cases passed. The saved file compiled with the guarded owner, and the previous owner remained within scope until a fresh catalog can disambiguate the old edge. The edited macro case compiled exactly the active file and the guarded owner, preserved the genuine missing module and reported the remaining macro instance's bad port at its new line. The eight related Qt tests passed, including the existing scope caps of 64 and 128 and session reuse. The measured tab-switch p95 was 14 ms. [Follow-up passing log](provenance-green.log)

## Commands

The checkout is mounted at `/src` in the pinned `vedit-build:local` and `vedit-qt6-build:local` images. The Qt run uses the repository-pinned slang CLI (`VEDIT_SLANG=/src/build/slang/out/slang`), packaged slang-server (`VEDIT_SLANG_SERVER=/src/target/qa/bin/slang-server`) and pinned Verible. Login shell import is disabled for the disposable fixture (`VEDIT_NO_ENV_AUTOCAPTURE=1`).

```sh
cargo build --locked --release -p vd-ffi
cmake --build target/qa/qt --target vedit-realtime-context-e2e
ctest --test-dir target/qa/qt -V -R '^vedit-realtime-context-e2e$'
```
