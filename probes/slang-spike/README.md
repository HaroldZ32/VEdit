# slang-server / hierarchy spike probes (T4 → evidence/w2-spike/)

| file | role |
| --- | --- |
| `run-spike.sh` | one command: build → CentOS 7 `--version` → real `initialize` → elaboration → LSP shape dump |
| `build-sluserver-centos7.sh` | the source build inside a fresh `centos:7` (vault mirror, devtoolset-11, CMake 3.31.6, CPython 3.12.8, submodules, static libstdc++) |
| `gcc11_path_hash.h` | GCC 11.2 workaround (`-include`): `std::hash<std::filesystem::path>` only exists from libstdc++ 11.3 |
| `lsp_initialize.py` | the LSP client side of one `initialize` handshake (the server may run inside a container via `--wrapper`) |
| `lsp_hierarchy_probe.py` | opens the fixture, builds it via `slang.setBuildFile`, then calls the hierarchy/driver/load commands and prints every raw response |
| `elab_probe.py` | runs `slang --top top --ast-json --ast-json-source-info` and extracts hierarchy + drivers/loads + time/peak RSS |
| `buildfile_rule_probe.py` | measures the build-file rule: `.f` location vs entry style vs server cwd (only the cwd changes the answer) |
| `fixture/` | the AC-3.0 fixture: `top` instantiates `mid` twice, `top.link` is driven in `mid` and loaded in `top` |

See `evidence/w2-spike/README.md` for the measured results and `docs/BUILD.md` §13 for the
build/run recipe.

## The one rule that matters (measured, see `evidence/w2-spike/07-buildfile-rule.txt`)

Start `slang-server` with **cwd = the project root** and announce a `rootUri` that
**exists in that process**. The `.f` may live anywhere; its entries are best written
relative to the project root (absolute entries also resolve when the cwd is right).
If the cwd is not the project root, `getDrivers/GetLoads` return `location.uri`
values resolved against the wrong base (a sibling of the project root): the module
tree still looks correct and a double-click opens the wrong file.
