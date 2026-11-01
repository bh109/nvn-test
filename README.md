# WindowSmash NVN Vulkan

An experimental Windows build using WindowSmash with ssbu-recomp's NVN/Vulkan graphics.
This is a free-time learning project. Full credit goes to the creators of
[WindowSmash](https://github.com/OpenSmash-Ultimate/WindowSmash) and
[ssbu-recomp](https://github.com/OpenSmash-Ultimate/ssbu-recomp).

## 1. Get your files ready

You need a 64-bit Windows PC with AVX2 and Vulkan support, plenty of free disk
space, and your own game dump updated to **13.0.5**, console keys, and firmware.
Have your DLC files ready too if you want to use them.

**No game files, keys, firmware, or prebuilt game executable are included.**

## 2. Download and extract

1. Open [this repository](https://github.com/bh109/nvn-test).
2. Click **Code → Download ZIP**.
3. Right-click the ZIP → **Extract All**.
4. Open the extracted folder containing **Install WindowSmash NVN.bat**.

Run everything from the extracted folder, not from inside the ZIP.

## 3. Install

1. Double-click **Install WindowSmash NVN.bat**.
2. Let it find or install the required tools. Review any setup and license prompts.
3. Select your game files, keys, and firmware in the setup window.
4. Enter your player name, then click **Set up and build**.
5. Leave the installer open until it finishes. The first build can take hours.

If installation stops, keep the folder and run the same installer again—it
resumes completed work. No separate emulator or ssbu-recomp download is needed.

## 4. Play and choose graphics quality

1. Double-click **Play WindowSmash NVN.bat**.
2. Press **Esc** and find **Graphics / performance**.
3. Choose **Performance (540p)**, **Balanced (810p)**, **Full resolution (1080p)**,
   or **4K supersampling**.
4. Click **Save resolution**.
5. Close the game and reopen **Play WindowSmash NVN.bat** to apply it.

The default is Performance. Higher resolution needs more GPU time and memory.
Lower it if your PC struggles. First-time shader preparation can cause temporary
loading or stutters.

For online play or a local/LAN lobby, use the same Esc settings window.
See the [online guide](docs/ONLINE-NVN.md) for details.

## Update or uninstall

- After updating the source, run **Install WindowSmash NVN.bat** again to rebuild.
  You do not need to uninstall first. Keep your existing folder and personal data.
- To remove generated files, close the game and run **Uninstall WindowSmash NVN.bat**.
  Saves and settings stay unless you explicitly choose to remove them.

## Getting help

- Installer problem: check **install-nvn.log**.
- Game problem: check **build/nvn-opensmash-port/play.stdout.log** and
  **play.stderr.log** in the same folder.
- Report your CPU/GPU, driver, resolution, steps to reproduce, and relevant log
  excerpts. Remove personal information before sharing.

Performance varies. The second development laptop has successfully run 4K in
user testing; the Intel laptop still has unresolved slowdowns and GPU device
loss. That does not establish the cause. Treat this as an experimental alternative
and consider the original projects' future fixes and releases.

## For contributors

[Development guide](docs/DEVELOPMENT-NVN.md) · [AI handoff](docs/AI-HANDOFF.md) ·
[Detailed installation](docs/INSTALL-NVN.md) · [Publishing rules](docs/PUBLISHING-NVN.md)

Licenses and credits: [LICENSE](LICENSE) · [NOTICE.md](NOTICE.md).
