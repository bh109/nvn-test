# WindowSmash NVN development workspace

This folder is the active workspace for NVN development and tests. No sibling
WindowSmash or OpenSmash checkout is required by the install/build/play path.

- Install/build: Install WindowSmash NVN.bat (tools/build_nvn.py).
- Play: Play Intel NVN Vulkan.bat.
- Runtime rebuild: tools/build_nvn.py --runtime-only after preparing common objects.
- Keep AI-HANDOFF-2026-10-28.txt updated after meaningful changes and tests.
- Read docs/AI-HANDOFF.md for portable continuation context, especially the AMD
  laptop's offline jitter. Keep that summary current without private data.
- Preserve the warm NVN cache, saves and working/rollback executable when testing.
- Use Mario versus Mario, normal Battlefield Training, 1x speed, CPU Stop, both
  0%, starting platforms and menu closed for comparable battle measurements.
- Let the user navigate to that scene after launching; avoid changing fighters
  or treating intro/menu FPS as a battle benchmark.
- The validation-enabled shutdown timeout and online network timing remain
  unresolved; do not claim they were fixed by the installer or folder migration.
