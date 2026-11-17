# When the game shows and takes a list entry

Findings behind 0.1.4's parking (`more-stratagem-slots/src/stratagem_slots.c`, "parking"). Game build 25480438; the
addresses are RVAs in game.dll (the 2026-11-13 dump's base is 0x7FFCF6030000). Read from the dump on 2026-11-17, and
checked in game on HellForge 0.4.0-beta and 0.5.0-nightly.20261116 where it says so.

## The problem

HellForge's Hell Forge difficulty (level 11, `examples/mods/hell-forge` in the SDK) runs two missions in one. Its
loader mixes two mission rows, so a mission starts with every objective's stratagem in the list. One ready-up with
20 picks, from the mod's log:

```
record ...: 20 picks (20 found again) and 11 the game added (types 124 145 33 128 105 49 70 19 42 28 79):
no room for pages; left out: 15 extra pick(s), 16 kept
```

A mission of the game's own had 4 or 5 of those (124 145 33 128, and 49 with an Eagle). The list holds 16
(`audit-findings.md`), so 11 left 5 rows for the picks, fewer than a page with its two spare rows needs, and the old
guard trimmed the loadout to 5 picks.

| Type | Stratagem | In a mission of the game's own |
|---|---|---|
| 124, 145, 33, 128 | Reinforce, SOS Beacon, Resupply, Upload Discovery | yes |
| 49 | Eagle Rearm | yes, with an Eagle in the loadout |
| 105 | M-102 Gunner FRV (the level's `mission_stratagem`) | no |
| 70, 19, 42, 28, 79, 72 | Prospecting Drill, Seismic Probe, Dropoff Hellbomb, SEAF Gun, Carry Data, Dropoff Bug Plug | not seen at ready-up |

The HUD showed 9 of the 16: the others are in the list and hidden.

## The game never adds or removes these entries

Nothing appends an objective's stratagem when its objective starts, and nothing takes it out after. The entry is in
the list from ready-up. Each frame the game asks whether it can be used, and hides it when not.

- 0x66C920 `(stratagems, list, row) -> 1`: the entry can be used where the player is now. `stratagems` is the object
  at `[game + 0x33266D0]`; `list` is a PlayerHistory list (the record layout); it reads the entry's type
  (`list + 0x188 + row * 0x30`) and the list's player id (`list + 0x9E8`), and nothing else of the list except for
  type 49.
- What it decides, by type:
  - a type in the object's own list of blocked types (`+144`, count `+184`, on with `+20`): no;
  - 5 (Orbital Illumination Flare): by a float of the game state;
  - 145 (SOS Beacon): by the lobby's state (among it: not with 4 or more players);
  - 49 (Eagle Rearm): only when 0x66D650 `Loadout_HasSpentEagleUses(_, list)` says an Eagle in the list is below
    its full uses;
  - 128 (Upload Discovery): by 0x6F24D0;
  - 77 and 90 (the Health Pack Racks): not in the special mode (0x66D6F0, which asks 0x11DF110);
  - 28 (SEAF Gun): by the state of its objective (the records at `[game + 0x3326E68] + 7360`, 16 bytes each, count at
    `+7352`, kind 101);
  - any type whose info row has an area kind at `+124`: the player's position (the 776-byte player record at
    `[game + 0x3326508] + 104`, position at `+736`) must be inside an area of that kind (0x5D9A40). Kind 1 is counted
    another way first (4 or more of something already there: no).
- The info rows' `+128` (loadout flags) and `+124` (area kind) of the entries above, from the mod's log:
  `124:1:0 145:1:0 33:1:0 128:1:0 105:3:0 49:1:0 70:1:7 19:1:8 28:1:0 42:1:1 72:1:2`. A loadout stratagem has bit 2 of
  the flags (105: 3); the mission's own have 1.
- Lookup of the player record (read from the code, not seen happen): the function guards an unknown player id only
  with the invalid-id constants. A valid id that is not in the record's map would read far out of bounds. The game
  relies on it being there, so the mod calls the function only from the HUD list's update, while that update has
  someone to show.

Callers:

- 0x1836510 `HudStratagemRow_Update` calls it at 0x183764B for its row, when the row's entry index is below the
  list's count and the player id is valid. The object comes from the same global.
- 0x66C880, the input's check of an entry: the entry's remaining uses (0x66D3D0) are not 0, and 0x66C920.
- 0x66D200, the other half of the input's check, true when the entry can't be taken now: its cooldown has not ended
  (`entry + 0x18` against the clock; Reinforce reads a global instead), a record of the in-hand system names its row,
  or one of three further checks of the player and the type fails (0x9E2130, 0xF74D30, 0x66A1440; not read in full).

In game, 2026-11-17: the Seismic Probe, the SEAF Gun and the Dropoff Hellbomb each went from 0 to 1 at their
objective (the SEAF Gun once loaded, and it stayed 1), and back to 0 after leaving the area.

## Uses

0x66D3D0 `Stratagem_GetRemainingUses(_, list, row)`: the entry's uses (`+4`), except for a type shared by the team
(info `+148` set and uses not unlimited): those come from a table of 16 {type, uses} at `PlayerHistory + 290576`.
So a shared stratagem's uses do not live in the entry, and an entry kept outside the list loses nothing.

## The input

- 0xA900D0 adds a direction to the input state and calls 0x66D8C0 `StratagemInput_MatchSequence`. The state: `[0]`
  the directions so far (16 at most), bytes from `+4` the directions (1 up, 2 right, 3 down, 4 left), `[5]` the type
  matched, `[6]` its row, `[7]` the player.
- A match is an entry that can be taken (0x66D200 false, 0x66C880 true) whose code is the input. Without one, the
  input stays as long as some entry that can be taken still starts with it, and is cleared (`[0] = 0`) when none
  does. So a wrong direction clears the input.
- The mod never writes this state.

## The HUD list

- 0x1834A10 `HudStratagemList_Update(list, float, shown, float)`: its third argument is compared with a global first
  (`cmp r8d, [rip + ...]` at 0x1834A1E) and it branches on that. The mod takes it as "no one to show" and does not
  ask then; what the game's other branch does was not read.
- 16 rows, each showing the list entry of its index. The layout places the rows whose entry has the byte at `+9` set
  first, then the others. 0x4DBBF0 (`PlayerHistory_AppendTypeForAllPlayers`) sets that byte on what it appends. This
  is why an entry placed after the page's picks still shows with the mission's own.
- `list + 227100` and `+ 227104` are rows with a timer under 5 seconds, not a selection.
- Row states seen in the log (`row + 14168`): 1 at rest, 2 in hand, 3 being called in; 4 on the Eagles after one was
  used.

## What the mod does with it

From 8 entries of the game's at ready-up, in a loadout that needs pages:

- The entries that are neither a loadout stratagem nor one of 124, 123, 145, 33, 128, 49 are taken out of the record
  and kept by the mod with their state, like a pick on another page.
- The HUD list's update asks 0x66C920 about each, ten times a second, with a copy of the local list that holds them
  instead.
- One that can be used goes back in the list after the page's picks, so no pick's row moves. It goes out again when
  it can't.
- They take the two rows a page leaves free first. A page gets smaller only with three or more in at once.
- When the game activates a stratagem whose entry is out (in hand since before), the entry goes back in first.

With 11 entries of the game's, 20 picks go in 3 pages of 8.

## HellForge holds the mod files read-only

`docs/mods.md`, 0.4.0-beta and the nightly: "While a session runs, its mod files are held read-only, so nothing the
game runs can change a mod that loads as yours next time." That includes `mods\more-stratagem-slots.ini`. Opening it
for writing fails with a sharing violation (error 32) from the mod's start until the session ends; creating it works
because a file that is not there is not held. So the mod can write its settings file once, when it is missing, and can
never add to it.
