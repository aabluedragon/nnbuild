# VibeBuild32 Multiplayer / Netcode

VibeBuild32 is a NetDuke32 (EDuke32-family) fork. Multiplayer uses the classic
**mmulti** master/slave UDP transport under a **deterministic lockstep**
simulation. This document captures the architecture and the notable fixes so
the netcode can be worked on without reintroducing desyncs.

---

## 0. The golden rule: determinism

Every machine runs the **identical** simulation, tick for tick, from the shared
input stream. Any divergence in game state, the RNG, actor spawning, or sprite
allocation order between machines is a **desync**.

- Sync is verified every tick by a CRC over positions, angles, health and the
  RNG streams (`sync.cpp` / `Net_GetSyncStat`). **`Desynchronized!` / `Out Of
  Sync` in the logs is the authoritative failure signal.**
- The per-tick counter is **`movefifoplc`** (monotonic; incremented once per
  `G_DoMoveThings()` in `game.cpp`). All machines process the same ticks in the
  same order.
- To change world state mid-game you must apply the change **identically on
  every machine at the same `movefifoplc` tick**. A host-only mutation (spawn,
  kill, teleport) that peers don't replay at the same tick **will** desync.
- `krand()` (`randomseed`) is the synced game RNG. Anything that consumes it
  must consume it the same number of times on every machine. `wrand()` is a
  *non*-synced RNG for host-only choices (e.g. which map to rotate to) whose
  result is then broadcast.
- After a join, `g_syncCheckStartTick = MOVEFIFOSIZ` opens a grace window so
  in-flight packets stamped with the old tick numbering don't false-positive the
  detector.

**Determinism test hook:** `NETDUKE_SYNCDUMP=1` writes `syncdump.txt`.

---

## 1. mmulti transport (`source/build/src/mmulti.cpp`)

Reliable + ordered delivery, per peer slot:

- `icnt0[]` incoming delivered count, `ocnt0[]/ocnt1[]` outgoing acked/queued,
  `ipak/opak[MAXPLAYERS][FIFSIZ=512]` the packet rings.
- `0xaa/0xab` handshake (slave sends `0xaa`, master replies `0xab` with the
  slot + player count). `ic0 == -1` marks these special handshake packets.
- Each datagram packs the ack bitmap **plus as many queued ordered messages as
  fit in `MAXPAKSIZ`**, always starting from the oldest un-acked (`ocnt0`).

Notable settings / fixes:

- **`MAXPAKSIZ = 8192`** (was 2048). The sender only advances past the oldest
  un-acked message and a peer acks ~once per frame, so reliable throughput is
  `(messages-per-packet × frame-rate)`. At 2048 only one ~1 KB snapshot chunk
  fit per datagram, pinning the late-join transfer near 40 KB/s (~7 s). Bigger
  packets let each ack advance several chunks (~0.8 s). Gameplay packets stay
  tiny; only the rare big transfer grows.
- **Stale-ack clamp:** `if (ic0 > ocnt1[other]) ic0 = ocnt1[other];` before
  advancing `ocnt0`. A peer can't have received more than we sent; a larger
  `ic0` can only be a stale packet from a *previous* connection on this slot
  (same-slot rejoin) and would fast-forward our send cursor past packets the
  peer never got — a permanent reliable-stream deadlock.
- **Reconnect-notify callback** (`mmulti_setcallbacks(bancheck, joinnotify,
  reconnectnotify)`): a fresh `0xaa` on a slot that is *already a live in-game
  player* means that client restarted. The callback drops the stale slot and
  mmulti **withholds** the `0xab` so the client keeps handshaking and is
  re-admitted cleanly once the drop completes.
- `mmulti_dropslot(slot)` fully resets a slot's transport (`otherip = 0`,
  `icnt0/ocnt0/ocnt1 = 0`, `ipak` cleared). It must run whenever a player
  leaves — including a **clean** quit (see §3).
- `mmulti_setextraslot(slot)` services a slot's reliable channel before it's in
  the connect list (used to talk to a late joiner during the transfer).

---

## 2. Late join (`Net_MasterCheckLateJoin` / `Net_CompleteLateJoin`)

A client may join a match already in progress:

1. Master notices an empty slot completed the handshake (`!connected &&
   mmulti_slotconnected`). It **freezes** input flow (`g_lateJoinFreeze`), so
   every machine is paused on the same tick.
2. Master waits for the joiner's `LATEJOIN_READY` (the joiner only sends it once
   it's in its main loop reading packets — sending the snapshot earlier races
   the joiner's init and drops chunks).
3. Master snapshots the paused world (a savegame via `G_SavePlayer`) and streams
   it as `SNAPSHOT_CHUNK`s, throttled by `Net_DrainReliableWindow` (keeps the
   512-entry ring from overflowing).
4. Master broadcasts `LATEJOIN_GO` (slot, roster mask, auxiliary RNG seeds),
   integrates the joiner, and spawns it.
5. Every existing peer applies the same integration + spawn **at the same
   master-locked tick** (`Net_PeerCheckLateJoin` waits until
   `g_player[connecthead].movefifoend <= movefifoplc`). The joiner itself loads
   the streamed savegame and spawns.
6. All meet at the `Net_WaitForPlayers` ready barrier (`playerreadyflag`).

Deterministic spawn: `P_SpawnLateJoiner` runs on master, joiner and peers at the
identical frozen RNG state, so its `P_RandomSpawnPoint` (`krand()`) lands on the
same spot everywhere.

---

## 3. Rejoin (leave + rejoin), reliability fixes

Making leave/rejoin work **every time, regardless of timing**, took four
independent fixes — the transport, the barrier, the quit path and the roster:

1. **Transport clamp** (§1) — stops the stale-ack deadlock on same-slot rejoin.
2. **Reconnect-notify** (§1) — the host re-admits a hard-killed client that
   re-handshakes on a live slot.
3. **Ready-barrier fast-exit** — `Net_WaitForPlayers` skips any slot already
   scheduled for removal (`g_pendingKickSlot`). Otherwise, if a client left
   while the host was in the barrier integrating it, the host sat out the full
   10 s timeout before it could re-admit — the joiner saw "waiting for the state
   snapshot" hang.
4. **Clean-quit slot reset** — a normal in-game quit reaches
   `Net_CheckPlayerQuit` (via the `SK_GAMEQUIT` sync bit) which cleared the
   game presence but **not** the mmulti slot. The host must `mmulti_dropslot`
   there, or `otherip` stays set (host immediately treats the stale connection
   as a late joiner) and the sequence counters stay high (the relaunched client
   deadlocks). The kill/crash path already reset via reconnect-notify; the clean
   path was the gap.
5. **`numplayers` stability** — on a late-join host, `Net_CheckPlayerQuit` must
   **not** decrement `numplayers`/`ud.multimode`. The slot stays reserved for a
   rejoin; nothing ever grows the count back, so after a few leave/rejoin cycles
   it fell below 2 and `Net_WaitForPlayers` early-returned — the host stopped
   the ready handshake and the next joiner hung forever on "WAITING FOR
   PLAYERS".

---

## 4. Async vs lockstep

- **Lockstep** (default): every tick waits for all players' inputs. Sync-clean;
  slow clients stall everyone.
- **`-async`** (server-authoritative): the master substitutes late inputs so it
  never blocks; the slave feeds the master's canonical echo back into its own
  authoritative sim (feeding local input while the master substitutes desyncs
  the whole world). Input FIFO: `inputfifo[MOVEFIFOSIZ][MAXPLAYERS]`,
  `movefifoplc/movefifosendplc/movefifoend`.

---

## 5. Maps

- **Built-in episode maps ("ExLy")** ship in the GRP that every client has.
  `osdcmd_map` recognizes them and loads by **volume/level** (like the map
  rotation) — `boardfilename` stays empty, so nothing is transferred and the
  HUD shows "E1L3", not a file path.
- **User maps** are transferred, but the client **declines the download** if it
  already has the identical file (`Net_HaveFile` = matching size + FNV-1a crc).
  Only a genuinely missing or different-version file is fetched.

---

## 6. Dedicated server

- `-dedicated`: headless (`g_forceHeadless` — dummy video, no draws, no sound),
  slot 0 is a non-playing spectator (`g_netServerDedicated`, hidden from the
  scoreboard/fragbar), stdin console (`g_netServerConsole`).
- The console reads lines and runs them through `OSD_Dispatch` (`Net_PollStdin`).
- Host-authoritative admin commands live in `osdcmds.cpp` /
  `oldnet.cpp`: `kick`, `ban`, `unban`, `listban`, `listplayers`, `map`,
  `nextmap`, `maplist`.
- **macOS note:** `Bgetappdir()` must not be called at `app_main` top — spinning
  up NSBundle/CoreFoundation before Cocoa init corrupts later SDL/Cocoa startup
  and crashes headless servers. Resolve such paths lazily.

---

## 7. Local test recipe

Build:

```
make vibebuild32
# Windows cross:
make PLATFORM=WINDOWS CROSS=i686-w64-mingw32- obj=obj-win32 HAVE_FLAC=0 vibebuild32
```

Two local instances (server + client) on E1L1:

```
# dedicated server (4 slots, start immediately)
./vibebuild32 -usecwd -name SERVER -dedicated -startnow -v1 -l1 -net /p23513 /n0:4
# client, joining self
./vibebuild32 -usecwd -ns -nm -name PLAYER -net /p23514 /n0 127.0.0.1:23513
```

- `-ns -nm` = no sound / no music (quieter test clients). `-usecwd` keeps
  config/log in the working dir (`vibebuild32.cfg`, `vibebuild32.log`).
- **Desync check:** `grep -iE "Desync|Out Of Sync" vibebuild32.log`. Zero = good.
- Feed the dedicated console from a script via a FIFO on stdin (signals like
  SIGTERM kill too hard to trigger the graceful `SK_GAMEQUIT` path).

---

## 7b. Masterserver, lobby & STUN

A standalone public directory + NAT-punch rendezvous, run with
`vibebuild32 -masterserver [port]` (default **23519**). Own UDP protocol,
independent of the game transport: 4-byte magic `"VBMS"` + 1 type byte.

- **Advertise** (`MS_ADVERTISE`/`_STOP`): a host started with `-advertise <addr>`
  pulses name/gameport/players/map from `Net_HostAdvertise` each frame; the
  master keys the entry off the packet source. `-advertise` must precede `-net`.
- **List** (`MS_LIST_REQ`/`_RESP`): the in-game **JOIN GAME** menu (menus.cpp
  case 20020) and the `serverlist` console command call
  `Masterserver_QueryList`.
- **STUN / hole-punch (IPv4 + IPv6)** (`MS_PUNCH_REQ` → `MS_PUNCH_NOTIFY` +
  `MS_STUN_RESP`, marker `"VBPH"`): when a client's direct connect stalls it
  sends `MS_PUNCH_REQ` **from its game socket** (`mmulti_sendraw_str`, so the
  master sees the real game endpoint), contacting the master over the **same
  family as the join target** (`MakePunchReq` picks v4/v6 to match). The master
  tells the server to punch toward the client (`Masterserver_HostPump` →
  `mmulti_punchto_str`) and tells the client the server's address; both address
  strings carry `[ipv6]` or `a.b.c.d`. The master sends each reply/notify out
  the socket matching the destination family (`ms_send_family` over `g_msSock4/
  6`). Both sides fire packets from the game socket to open the path; mmulti's
  normal handshake then connects. The join relaunch carries the master via
  `-masteraddr` so the connecting client punches through the same one.
- **Join flow**: menu join → `Net_LaunchAddress` relaunches the process as a
  client (`execv`) with `-net … host:port` + `-punchtarget host:port`. On
  repeated failure (~28s) `Net_LaunchMenu` relaunches back with `-joinerror
  <msg>`, which the JOIN menu shows in red.
- The master and lobby handle **IPv4 and IPv6** (separate `AF_INET`/`AF_INET6`
  sockets; exact per-family `addrlen` — macOS rejects `sockaddr_storage` len for
  v4). A server advertises over **both** families when it can: `SetAdvertise`
  resolves the master's A + AAAA records, opens a v4 and v6 advertise socket, and
  tags each pulse with a random `serverId` so the master merges the two into one
  listing carrying both endpoints. IPv4 is mandatory; IPv6 is optional.
- **The game transport (mmulti) itself is dual-stack** — see §7c. The lobby row
  connects over IPv4 by default (always reachable), or IPv6 when `cl_preferipv6`
  is set / it's the only address published.

## 7c. IPv4 + IPv6 game transport (mmulti)

The reliable UDP transport carries **both** families over a single dual-stack
`AF_INET6` socket (`IPV6_V6ONLY` off), falling back to a plain `AF_INET` socket
(`mysock_v4only`) where IPv6 is unavailable.

- Every peer is identified by its full socket address (`sockaddr_in6`), not a
  32-bit IPv4 + port. IPv4 peers are stored as **IPv4-mapped** addresses
  (`::ffff:a.b.c.d`) so one socket handles both; `sin6_family == 0` marks an
  empty slot. Helpers: `addr_valid/addr_eq/addr_eq_ip/addr_set4/addr_get4/
  addr_to_str/netsendto`.
- Peer specs (`/n0 host`, manual entry) resolve via `getaddrinfo` (`resolve_peer`):
  `a.b.c.d[:port]`, `[ipv6::addr]:port`, bare IPv6 literals, and hostnames; an
  IPv6 result is preferred when a name has both.
- The IPv4-oriented ban/natfree/`getslotip` code keeps its 32-bit view through
  `addr_get4` (returns 0 for a real IPv6 peer); `mmulti_getslotaddrstr` gives the
  full address for logs / the connected-players list.
- This changes **addressing only**, not the reliable-delivery sequencing, so it
  is determinism-neutral (verified: IPv4 and native `[::1]` connects both play
  with 0 desync).

Files: `source/vibebuild32/src/masterserver.cpp` / `.h` (protocol + rendezvous),
`oldnet.cpp` (advertise/pump/launch), `menus.cpp` (Host/Join UI).

---

## 8. Key files

| File | Role |
|------|------|
| `source/vibebuild32/src/oldnet.cpp` | netcode core: handshake glue, late-join, rejoin, ready barrier, file transfer, admin |
| `source/vibebuild32/src/masterserver.cpp` | masterserver protocol, lobby list, STUN/NAT hole-punch rendezvous |
| `source/build/src/mmulti.cpp` | reliable UDP transport (dual-stack IPv4+IPv6) |
| `source/vibebuild32/src/sync.cpp` | per-tick desync detection |
| `source/vibebuild32/src/game.cpp` | sim loop `G_MoveLoop` / `G_DoMoveThings`, `movefifoplc` |
| `source/vibebuild32/src/premap.cpp` | `P_SpawnLateJoiner`, `P_RandomSpawnPoint` |
| `source/vibebuild32/src/osdcmds.cpp` | console commands (`map`, admin) |

---

## 9. Rules of thumb before touching the netcode

1. Will this run **identically on every machine at the same tick**? If not, it
   desyncs. Broadcast an event tied to a `movefifoplc` tick, don't mutate on the
   host alone.
2. Does it touch `krand()`? Every machine must consume it the same way. Use
   `wrand()` + broadcast for host-only randomness.
3. Reliable-transport changes: respect `FIFSIZ = 512` (window `< 512`) and never
   let a big transfer outrun the ring (`Net_DrainReliableWindow`).
4. After any change, run the two-instance test and confirm **0** desyncs across
   a join, sustained play, and a leave/rejoin cycle.
