# NVN developer checks

Players only need the root Install, Play and Uninstall BAT files. These optional
checks are for contributors; start with the [development guide](../docs/DEVELOPMENT-NVN.md).

| Tool | Purpose / prerequisites |
|---|---|
| `Rebuild-NvnRuntime.ps1` | Runtime rebuild after common objects are prepared. |
| `Test-NvnVulkanValidation.ps1` | Core/sync validation; needs a build and portable Khronos layer. Not an FPS benchmark. |
| `Test-NvnBridge.ps1` | Native bridge fixture with the local Windows compiler. |
| `Test-OpenSmashContext.ps1` | Context ABI equivalence against bundled upstream headers. |
| `Test-OpenSmashHost.ps1` | Host-adapter fixture against bundled upstream source. |
| `Test-OpenSmashDisplay.ps1` | Display/clock adapter fixture. |
| `Verify-OpenSmashSdk.py`, `Verify-OpenSmashGame.py` | Contract utilities used by the build; retain these. |
| `test_*.cpp`, `test_*.c`, `test_verify_opensmash_sdk.py` | Supporting fixtures and regression checks. |
| `Make-ZstdWorkerFixture.py`, `fixtures/` | Synthetic worker fixture; no game data. |

Upstream names identify the ABI/source being checked. Context and host checks
default to `third_party/opensmash-nvn`; `-ReferenceRoot` can select another checkout.
These scripts need the project's Windows compiler/SDK prerequisites.

Obsolete external-renderer probes and wrappers depending on the removed LLE test
harness were removed. Their history remains in Git. Ignored dated evidence folders
are not required to install or play.
