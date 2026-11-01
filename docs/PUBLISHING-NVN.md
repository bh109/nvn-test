# Publishing the NVN source repository

Publish source, not a copy of the entire working folder. Players clone or download
the repository, supply their own supported 13.0.5 game files, keys and firmware,
then run **Install WindowSmash NVN.bat**. See [installation](INSTALL-NVN.md).

## Include

- `Install WindowSmash NVN.bat` and `Play Intel NVN Vulkan.bat`.
- `runtime/`, `tools/`, `third_party/`, `tests/`, `docs/` and `launchers/`.
- The source scripts and fixtures in `diagnostics/`. In particular,
  `Stage-OpenSmashGraphics.ps1` is required by the builder and
  `Test-OpenSmashPort.ps1` is required by the Play launcher. Do not exclude the
  entire diagnostics folder.
- `.gitignore`, `.gitattributes`, `requirements.txt`, `README.md`, `LICENSE`,
  `NOTICE.md`, `AGENTS.md` and the default `online_server.txt`.
- All vendored licenses, provenance manifests and contract metadata. Contract
  hashes and addresses are source metadata; generated game code and signature
  bytes belong only in local build outputs.

Keep the existing runtime and tool sources even where their names mention
Direct3D or the older backend: the NVN build still compiles shared support code.
Removing those files needs a separate dependency refactor and build test.

## Keep locally; do not commit

The root `.gitignore` excludes these. They can stay on the developer's PC:

| Files or folders | Purpose |
|---|---|
| `dump/`, game packages, keys, firmware, generated game code | Each player supplies/prepares their own inputs. |
| `build/`, `deps/`, `cache/` | Generated executables, objects, compiler/downloads and shader caches. Setup recreates these. |
| `save/`, `save_backups/` | Personal progress. |
| `screenshots/`, `reports/`, captures and logs | Personal images, diagnostics and crash dumps. |
| `controllers.ini`, `display.ini`, launcher settings | Per-PC preferences. |
| `online_name.txt`, `netplay_id.txt`, `netplay_address.txt` | Personal identity and network settings. |
| `version.txt`, module lists and generated version folders | Results of local game preparation. |
| `AI-HANDOFF*.txt`, dated diagnostic notes/report directories | Local development history; may include paths and private session details. |

The dated diagnostic directories include migration/removal inventories,
installer test results and the original-source audit archive. None is needed
by the installer. They may be archived outside the project or deleted after
review. Preserve working builds, saves and warm caches while developing.

Older comparison scripts in `diagnostics/` may expect a separate OpenSmash
checkout and developer-specific paths. They are optional research tools, not
part of the install/play path. A player needs no external OpenSmash checkout.

## Before the first push

Use Git to publish source; do not upload a ZIP of this entire working directory.
Ignore rules do not protect files that were already tracked. After staging,
review `git diff --cached --stat` and `git diff --cached --name-only`, then review
the staged diff for private paths, credentials and accidental game content.
Do not use `git add -f` for ignored outputs. If a private file was already
committed, removing it from the latest commit does not remove earlier history.

Keep upstream attribution and GPL license notices, including the OpenSmash NVN
source license and the Vulkan/DXC header notices. Document this as a modified
WindowSmash NVN variant; do not imply it is the upstream release.

Before calling a public release tested, run installation in a clean copy
containing only the proposed source files. Supply your own inputs through the
wizard and test install, controller input, Training, normal exit and a rebuild.
The existing machine has passed runtime rebuild and launch checks, but a full
cold installation on a fresh Windows machine has not yet been qualified.

Current limits: performance evidence is at 0.5 resolution scale on the tested
Intel laptop; validation-enabled shutdown can time out; online delay is not yet
classified by peer timing telemetry. Do not describe these as resolved.
