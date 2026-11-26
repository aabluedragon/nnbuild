# How multiplayer works

This page explains, in plain language, what happens between the computers in
a match. You do not need it to play or to host. It is here for players and
server owners who want to know why NNBuild behaves the way it does.

It describes the Duke Nukem 3D family. The technical version, written for
people who work on the code, is
[NETCODE.md](https://github.com/aabluedragon/nnbuild/blob/multiplayer/source/vibebuild32/NETCODE.md).

## Every computer runs the whole game

In many online games one server works out what happens and tells everyone the
result. NNBuild works differently. Every computer in the match runs the
complete game: every monster, every door, every rocket. The computers do not
send each other where things are. They only send what each player pressed.

The game moves forward in small steps, about 30 each second. For each step the
host collects the input of every player and sends the full set to everyone.
Each computer then works out that step from the same starting point and the
same inputs, and so arrives at the same result. This way of working is called
**lockstep**.

Lockstep has one hard rule: nobody can work out a step until the input of
every player for that step has arrived. That is why one slow connection can
hold up the whole match. NNBuild softens this with a countdown and a vote, and
with a no-wait mode in which the host fills in for a late player. Both are
described in [Hosting a match](hosting.md#lockstep-and-no-wait-mode).

## Why everyone needs identical data

Because every computer works the game out for itself, every computer must
start from the same material. The maps, the game's rule scripts and the art
all take part in the result. If one player's copy of a map has one extra
monster, that monster exists on that computer only, and from then on that
computer is playing a different game.

The same is true for chance. When the game rolls dice, for example to decide
where a shot lands, every computer must roll the same numbers in the same
order. NNBuild keeps one shared sequence of random numbers for this. Anything
that uses a number from it on one computer must use it on all of them.

This is why NNBuild checks files so carefully when you join. It compares the
name, size and checksum of every mod pack and custom map with the host's, and
downloads what is missing or different. It is also why each player needs the
same edition of the base game as the host.

It also explains what happens when a server changes its mod pack. A pack can
replace the game's rules, and the rules are read once, when the game starts.
So the server starts again with the new pack, and every player's game
restarts, fetches the pack and joins the new match.

## What a desync is

A **desync** is the moment when one computer's game stops matching the others.
It may start small: a monster one step to the left. But every later step
builds on the one before, so the difference grows until the players are no
longer in the same match.

NNBuild watches for this constantly. On every step each computer sums up its
view of the game in a few short check values: the players' positions and
state, the monsters and other objects, the projectiles, the map and the random
numbers. Each player's values are compared with the host's.

A copy of the game that has drifted cannot be steered back, because nobody
knows which earlier step went wrong. So NNBuild does the reliable thing. The
player's game says that it has lost sync, restarts itself and joins the match
again as a new arrival, which brings it a fresh copy of the host's game. The
player's score is given back afterwards.

## How joining a running match works

A new player needs the entire game as it is at one exact moment. NNBuild gets
that moment by pausing. When the host notices a new connection, it stops
handing out input. Every computer runs out of steps at the same place and
waits there.

While everyone waits, the host and the newcomer compare files and the missing
ones are sent. Then the host saves the game as it stands, in the same way a
saved game is made, and sends that to the newcomer piece by piece. This is the
progress bar on the joining screen.

Finally the host tells every computer to add the new player. They all do it on
the same step and in the same way, so the newcomer's starting point is chosen
identically everywhere. All computers confirm that they are ready, and the
match continues. The pause lasts as long as the transfer takes.

The saved game is also the reason for one restriction. A saved game written by
a 64-bit build of NNBuild cannot be read by a 32-bit build, and the other way
round. A player can therefore join a running match only with the same kind of
build as the host's.

## How coming back works

Coming back uses the same path as joining. The only extra question is whose
score the returning player should get.

When the host removes a player, for example because the connection went
silent, it frees the slot and notes the score under that player's ID. The ID
was created by the player's own copy of NNBuild and is kept in its settings
file. A nickname is not used for this, because anybody can type any nickname.
A player who quits on purpose is not noted and starts from zero next time.

When a player joins, the host looks for a saved score with the same ID and, if
it finds exactly one, gives it back and tells everyone. If two saved scores
carry the same ID, the host uses the player's address to choose between them,
and if that does not settle it either, the player starts from zero. The saved
scores are thrown away when a new game starts, so a score never follows a
player into a different match.

## Why some changes must be host commands

Suppose a host could simply add a monster to its own copy of the game. The
monster would exist on the host and nowhere else, and the match would desync
at once. The same goes for removing a player or clearing all monsters.

So changes like these go through commands that every computer carries out
together. The host picks a step a short time in the future, and tells every
computer "on that step, do this". The step is far enough ahead that the
message arrives everywhere first. Then all computers make the same change on
the same step and stay identical.

This is the reason for the console commands `spawn` and `monsters`, and why
kicking a player takes effect a moment after you type it. It is also why a
server script should change the world through such commands and not directly.
See [Administering a server](server-admin.md#change-the-match-while-it-runs).
