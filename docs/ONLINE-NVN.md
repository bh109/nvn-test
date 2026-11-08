# Online settings and a nearby lobby

In the NVN build, press **Esc** to open the native **Online settings** window.
This restores networking controls independently of the GPU overlay. Display/controller/game controls are under **More settings…**; these use the NVN
implementation rather than the original DirectX overlay pages. Game input is
blocked while the panel is open; the game and network continue running.

The panel shows the current lobby connection, its ping and (while matched) each
introduced peer's `direct ... ms` or `relayed` status. Change the lobby address,
player name or direct-connections checkbox, click **Save Online settings**, then
close and reopen the game. Saving does not move an active match to another server.
Direct peer connections disclose your IP address to matched peers; disabling
them routes match traffic through the lobby instead.

Choose **Public lobby** or **Custom server** in the Server selector. Public
lobby uses the default address and disables address editing. Custom server lets
you enter an IPv4 address or hostname with an explicit port, such as
`192.168.100.10:39421`, optionally followed by a lobby code. `off` selects offline
mode. Selecting **Public lobby** restores `smash.recomps.wtf:39421`.

## Test two laptops at home

1. On the host laptop, click **Start server on this PC** in the Esc panel. This
   starts the server, selects Custom server and fills in `127.0.0.1:39421`. Keep the server
   console open. It prints candidate LAN addresses; use the Wi-Fi/Ethernet one,
   not a WSL/VPN/virtual adapter address.
2. Permit the server's Python process through the host firewall on the intended
   network. If adding rules manually, permit inbound **TCP and UDP 39421**.
   This tool does not change firewall rules or router settings automatically.
3. In the host game's Esc panel, enter a player name and save. The server button
   fills in the address but does not save settings automatically.
4. On the second laptop, select **Custom server**, enter the host's LAN address plus `:39421`, enter a
   distinct player name, and save. Both computers must reach each other on
   the home network; guest Wi-Fi isolation may prevent this.
5. Close and reopen both games using **Play WindowSmash NVN.bat**. Match through
   the game's Online mode as usual. Open Esc to check connection/peer status.

For server-only or advanced use, the standalone batch launcher is under
`launchers/Host WindowSmash NVN Lobby.bat`; normal players can start hosting
from the Esc panel instead.

Close the host console or press Ctrl+C there to stop hosting. No game files,
keys or firmware are needed on a server-only machine: the bundled
`tools/ssbu_online_lobby.py` uses Python's standard library. The launcher finds
the setup-provisioned Python or an existing OS Python. Hosting does not require
recompiling the game.

## A public Asia lobby

Run that same Online server on a reachable machine in the intended region and
open its chosen port for **both TCP and UDP**. An optional DNS name can point to
the server; players enter that name plus the port. The server can also run on
Linux using the existing Python/server deployment tools described in
[multiplayer](MULTIPLAYER.md#run-your-own-lobby).

Home hosting over the internet may require router forwarding and a public IP;
carrier-grade NAT can prevent inbound connections. A LAN test needs neither
internet port forwarding nor a domain. The default launcher listens on all
interfaces; only expose it on networks where you intend to offer a lobby.

Each server has its own player pool and account/rating state (by default under
`tools/nex/state/`, excluded from Git). Choosing another server does not combine
its rooms with the original public lobby. Back up server state before removing
it. The normal uninstaller preserves this server account/rating directory.

## What the latency tells us

A high ping to the public host establishes a slow round trip to that host, not
the latency between two players. Lobby login/matchmaking talks to the server;
match data may travel directly between peers. If peers are `relayed`, the remote
lobby adds a detour even when both laptops are on the same Wi-Fi. Compare the
panel's direct/relayed status and an actual match through the LAN server before
attributing every hitch to distance. Rendering performance remains separate.

Windows `ping` accepts a hostname without `:port`; it measures ICMP, not this
game protocol. `tracert` timeouts can simply mean routers do not answer trace
probes. Intermediate hop names/times alone do not prove the server's physical
location or packet loss during matches.
