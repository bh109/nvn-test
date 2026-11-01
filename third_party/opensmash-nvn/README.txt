OpenSmash handwritten NVN/Vulkan graphics source used by the WindowSmash NVN port.

Imported from the user's OpenSmash source snapshot on 2026-11-01. Exact per-file
SHA256 values are in upstream-manifest.json. LICENSE and individual source-file
notices are retained. No game ELF, generated game C, keys, firmware or compiler
binaries are included. game-contracts.json contains verified 13.0.5 segment hashes,
sizes and the five reviewed guest address mappings only.

runtime/ files remain unadapted. diagnostics/Stage-OpenSmashGraphics.ps1 verifies
their hashes, stages the include closure and applies explicitly recorded changes.
Review changes before updating this pinned source; do not mix arbitrary revisions
with the existing SDK and guest-address adapter.

runtime/vk_glslang_shim.c is the upstream shader compiler interface. The installer
builds it against SHA256-pinned Khronos glslang 16.6.0 release libraries, retaining
the downloaded license files under deps/nvn-glslang-16.6.0/release/.
