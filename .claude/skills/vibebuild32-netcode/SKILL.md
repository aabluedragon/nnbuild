---
name: vibebuild32-netcode
description: Use when working on VibeBuild32 (NetDuke32/EDuke32 fork) multiplayer/netcode — the mmulti reliable transport, deterministic lockstep sim, late-join, rejoin, sync/desync, dedicated server, or map/file transfer. Encodes the determinism rules and the local test recipe so changes don't reintroduce desyncs.
---

# VibeBuild32 netcode

Full architecture: `source/vibebuild32/NETCODE.md`. Read it first for anything
non-trivial. This skill is the short, actionable version.

## The one rule that matters: determinism

Multiplayer is **deterministic lockstep** — every machine runs the identical
sim from the shared input stream. Desync = any state/RNG/spawn divergence
between machines.

Before writing code that changes the world mid-game, ask:

1. **Same on every machine, same tick?** A host-only mutation (spawn, kill,
   move, set flag) that peers don't replay at the same `movefifoplc` tick WILL
   desync. Broadcast an event and apply it on all machines at an agreed future
   tick; don't mutate on the host alone.
2. **Does it touch `krand()`?** The synced game RNG (`randomseed`). Every
   machine must consume it the same number of times. For host-only randomness
   use `wrand()` (non-synced) and broadcast the *result*.
3. **Reliable transport?** Ring is `FIFSIZ = 512`; keep the window `< 512`;
   never let a big payload outrun the ring — pump `Net_DrainReliableWindow`.

`movefifoplc` (in `game.cpp`, `++` once per `G_DoMoveThings`) is the monotonic
per-tick counter all machines agree on. Tie synced events to it.

## Where things live

- `source/build/src/mmulti.cpp` — reliable UDP transport (`icnt0/ocnt0/ocnt1`,
  `ipak/opak[512]`, `0xaa/0xab` handshake, `MAXPAKSIZ=8192`, `mmulti_dropslot`,
  reconnect-notify callback).
- `source/vibebuild32/src/oldnet.cpp` — late-join, rejoin, ready barrier
  (`Net_WaitForPlayers`), file transfer (`Net_SendFile`/`Net_HaveFile`), admin.
- `source/vibebuild32/src/sync.cpp` — per-tick desync detection.
- `source/vibebuild32/src/game.cpp` — `G_MoveLoop` / `G_DoMoveThings`.
- `source/vibebuild32/src/osdcmds.cpp` — console commands (`map`, admin).

## Landmines (already fixed — don't reintroduce)

- **Same-slot rejoin** needs all of: the `ic0 > ocnt1` stale-ack clamp
  (mmulti), reconnect-notify (mmulti), the ready-barrier fast-exit (skip
  `g_pendingKickSlot`), the clean-quit `mmulti_dropslot` in
  `Net_CheckPlayerQuit`, and **not** decrementing `numplayers` on a late-join
  host.
- **Snapshot transfer speed** = messages-per-packet × ack-rate; it lives on
  `MAXPAKSIZ`, not the send rate.
- **macOS headless crash**: never call `Bgetappdir()` at `app_main` top
  (CoreFoundation before Cocoa init corrupts startup). Resolve lazily.
- **Late-join spawn**: must run on master + joiner + peers at the frozen tick;
  it uses `krand()`, so all three stay aligned.

## Build

```
make vibebuild32
make PLATFORM=WINDOWS CROSS=i686-w64-mingw32- obj=obj-win32 HAVE_FLAC=0 vibebuild32
```

The make target and the output exe share the name `vibebuild32`, so make prints
a harmless "circular dependency dropped" note on macOS/Linux and links normally.

## Test EVERY netcode change (2 local instances)

```
# dedicated server, 4 slots, E1L1, start now
./vibebuild32 -usecwd -name SERVER -dedicated -startnow -v1 -l1 -net /p23513 /n0:4
# client joining 127.0.0.1
./vibebuild32 -usecwd -ns -nm -name PLAYER -net /p23514 /n0 127.0.0.1:23513
```

Then confirm **zero** desyncs across: a fresh join, ~15 s of sustained play
(enemies moving), and a leave/rejoin cycle:

```
grep -ciE "Desync|Out Of Sync" vibebuild32.log   # must be 0
```

Notes: `-usecwd` puts `vibebuild32.log`/`.cfg` in the cwd. To drive the
dedicated console from a script, feed commands to its **stdin via a FIFO**
(`mkfifo`; a background `( sleep N; echo "map e2l1"; sleep M ) > fifo &`);
SIGTERM/SIGINT kill too hard to exercise the graceful `SK_GAMEQUIT` quit path.
Kill leftover instances with `pkill -9 -f vibebuild32`.

## Definition of done for a netcode change

Builds on both platforms, and the 2-instance test shows 0 desyncs through
join + sustained play + rejoin. If it can't pass that, it isn't done.
