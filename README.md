# WindowSmash NVN Vulkan

An experimental Windows variant that adapts [ssbu-recomp](https://github.com/OpenSmash-Ultimate/ssbu-recomp)'s
NVN-to-Vulkan graphics into [WindowSmash](https://github.com/OpenSmash-Ultimate/WindowSmash),
keeping WindowSmash's game runtime and networking, including its existing public lobby.
**Full credit goes to both projects' creators.** This is a free-time learning project
and a temporary alternative for people encountering graphics issues. Consider the
original projects' future fixes and releases; regular updates/support aren't guaranteed.

## 1. What you need

- A 64-bit Windows PC, an **AVX2-capable CPU**, and a GPU/driver supporting **Vulkan 1.3**.
- Enough RAM and free disk space for game preparation and compilation; the installer checks your PC.
- Your own game dump updated to **13.0.5**, console keys, and firmware.
- Your own DLC files, if you want DLC characters/content.

**No game files, keys, firmware, or prebuilt game executable are included.**
No separate emulator or ssbu-recomp download is needed.

## 2. Download

1. Open [this repository](https://github.com/bh109/nvn-test).
2. Click **Code → Download ZIP**.
3. Right-click the downloaded ZIP → **Extract All**.
4. Open the extracted folder containing **Install WindowSmash NVN.bat**.

Run the installer from the extracted folder, not from inside the ZIP.

## 3. Install

1. Double-click **Install WindowSmash NVN.bat**.
2. Let it find or install the required tools. Review the setup and license prompts.
3. In the setup window, select your game files, keys, and firmware.
4. Enter your player name, then click **Set up and build**.
5. Leave the installer open until it finishes. **The first build can take hours.**

If it stops, read **install-nvn.log**, fix the reported problem, and run the same
installer again. Keep the existing folder: completed work is reused.
[Detailed installation help](docs/INSTALL-NVN.md).

## 4. Play and choose graphics quality

1. Double-click **Play WindowSmash NVN.bat**.
2. Press **Esc** → **Graphics / performance**.
3. Choose **Performance (540p)**, **Balanced (810p)**, **Full resolution (1080p)**,
   or **4K supersampling**.
4. Click **Save resolution**.
5. Close and reopen the game, or use **More settings… → Game → Restart game**.

The default is Performance. Higher resolution is sharper but needs more GPU time
and memory. Lower it if the game runs slowly. First-time shader preparation can
make startup/loading slow and cause hitches; keep the cache between runs.

**Esc → More settings… → Display** has windowed/borderless/fullscreen, window size,
GPU selection and FPS options. Click **Save display settings**. Options marked
**next start**, and the GPU frame-ahead limit, need a restart. Leave advanced
presentation/frame-ahead choices at their defaults initially.

- **F11 / Alt+Enter:** switch fullscreen/windowed.
- **F1:** show/hide FPS. **F2:** toggle FPS details.

[Display settings explained](docs/INSTALL-NVN.md#display-controllers-and-game-actions).

## 5. Set up a controller

1. Connect your controller.
2. Press **Esc → More settings… → Controllers** and select it.
3. Adjust player assignment, button layout, stick dead zone, rumble or trigger threshold.
4. Click **Save controller settings**.

To remap a button, choose the game button → **Capture input** → release held
controls → press the controller button/trigger you want. **Esc** cancels capture.
**Default binding** restores that button; **Test rumble** checks vibration.

Keyboard player, background controller input and global rumble are on the same
page. Reconnecting may be needed after changing player assignment. The settings
window uses mouse/keyboard; advanced bindings and XInput fallback limits are
explained in the [settings guide](docs/INSTALL-NVN.md#display-controllers-and-game-actions).

## 6. Play online or host a lobby

Press **Esc** to open Online settings. Choose one of these:

- **Public lobby:** connect to the existing WindowSmash public server.
- **Private group:** choose **Custom server** and enter a server address followed
  by a space and a lobby code. Friends use the same server and code.
- **Custom server:** enter your friend's or regional server's address as `hostname:port`.
- **Host at home/LAN:** click **Start server on this PC** and keep its server console open.
  The host uses the filled-in `127.0.0.1:39421`; other PCs use the host's LAN IP plus `:39421`.

Set your player name, click **Save Online settings**, then restart the game.
The panel shows connection status and a direct-peer checkbox; direct connections
share your IP address with matched peers. Each server has its own player pool.

Internet hosting needs additional network setup. Hosting doesn't guarantee
lag-free matches, and this variant has **no rollback netcode**.
[Step-by-step networking and hosting guide](docs/ONLINE-NVN.md).

## 7. Update or uninstall

- **Update:** get the latest source into your existing project folder, then run
  **Install WindowSmash NVN.bat** again to rebuild. Keep saves, settings and caches;
  no uninstall is needed first. Back up personal data before replacing files.
- **Uninstall:** close the game, then double-click **Uninstall WindowSmash NVN.bat**
  and review its plan. Saves/settings stay unless you choose to remove them.
  Source and shared system Python/Visual C++ installations are retained.

## 8. Known limitations

- Experimental and tested on only a few machines; smooth play isn't guaranteed.
- First-use shader/pipeline preparation can cause long loading or hitches.
- The Intel development laptop still has unresolved full-resolution slowdowns
  and GPU device loss. The AMD laptop has run smoothly, even at 4K in user testing;
  that isn't a benchmark or a guarantee for other PCs.
- Occasional shutdown delays remain, particularly with Vulkan validation enabled.
- Some upstream features aren't carried over. No rollback netcode; online quality
  depends on both players' performance and the network connection.

## Getting help

- **Installer:** check **install-nvn.log** in the project folder.
- **Game:** check **build/nvn-opensmash-port/play.stdout.log** and **play.stderr.log**
  in the same folder.
- Include your CPU/GPU, driver version, resolution, steps to reproduce, and relevant
  log excerpts. Remove personal information before sharing.

Contributions and investigations are welcome. Start with the
[development guide](docs/DEVELOPMENT-NVN.md) and [AI handoff](docs/AI-HANDOFF.md).
See also [publishing rules](docs/PUBLISHING-NVN.md), [licenses](LICENSE),
and [credits/notices](NOTICE.md).
