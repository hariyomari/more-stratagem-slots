# What the game ties to a list row

Findings behind 0.2.0's page changes (`more-stratagem-slots/src/stratagem_slots.c`, "the stratagem in hand"). Game
build 25480438; the addresses are RVAs in game.dll (the 2026-11-13 dump's base is 0x7FFCF6030000). Read from the dump
on 2026-11-17, with the mod's session logs of 0.1.4 where it says so. **None of 0.2.0's behaviour has been seen in
game yet**: what the mod does with these findings is at the end, with what a first session has to show.

## The problem

Until 0.1.4 a page change waited while a stratagem of the page was in hand or being called in (`page_hold`), because
"the stratagem being activated shows the wrong name until it lands". The question was what exactly goes wrong, and
whether a stratagem used in one row can be written over by another used in the same row on another page.

## After the activation nothing names a row

- 0x66E6D0 `StratagemInput_ActivateMatch(_, peer, {type, row}, where, _)` does everything by row in the one call: it
  reads the entry's type, and through 0xB9AAB0 `PlayerHistory_OnStratagemUsedSystems(peer, row, ...)` writes the
  entry's start (`+0x10`), its call-in's end (`+0x20`), takes a use (`+4`) and starts the cooldown (0x135C2C0,
  `+0x18`). It also sends the row to the peers (message 4137952335).
- The cooldown starters have no other caller that keeps a row: 0x135C2C0 is called by 0x135C5D0
  `PlayerHistory_OnStratagemUsed` and 0xB9AAB0 only; 0xB9A6D0 `PlayerHistory_SetStratagemCooldown` by
  0x9F2630 (which looks the row of type 28 up when it runs) and by a network handler.
- 0x66F4C0 `PlayerHistory_RemoveStratagem(_, player, type)` finds its entry by type.
- In the logs (28 activations) `used:` (the mod's hook on 0x135C2C0) has the timestamp of `activation:` every time.

So the uses, the cooldown and the call-in's end live in the entry (0x30 bytes), and the mod moves whole entries
between the list and its pages. Two stratagems used from the same row on different pages each keep their own.

## In hand: the in-hand record

- The system at `[game + 0x3326D98]` has, per stratagem ball, a record of 40 bytes (`+96`, count at `+28`) and a twin
  of 76 bytes (`+80`), at the same index.
- 0x6A3560 `(_, ball, {type, row, player})`, called by the input (0xA8EC20) when the code is complete and the
  stratagem is not one used at once: record `+16` the peer, `+24` the row, `+8` the type; twin `+0` the type, `+4` the
  row, `+8` the player's u32 id. 0x69FB40 `StratagemInHand_Clear` sets the type 0 and the row -1.
- Readers of the row: 0x69FC60 `StratagemInHand_NamesRow(list, peer, row)`, which the HUD row asks to show state 2
  ("in hand"), and 0x66D200, the input's check that keeps a row named in hand from being taken again. Both compare the
  record's row with a list row and never look at the type.
- So after a page change the row in hand is whatever entry is in it now: the wrong name, highlighted.
- The record is gone before the landing: of 28 activations in the logs, 24 had no record and 4 had one naming the
  next stratagem's row. So it is cleared between the hand and the landing (at the throw, most likely; what calls
  0x69FB40 was not read).

## Thrown: the ball carries its row

- The thrown ball's record (2280 bytes, `{type, row, player}` at `+984`) is what 0x6AA960 and 0xB8A2A0 hand to
  0x66E6D0 when it lands, and to 0x66EE10 when it ends without an activation. Both functions have more callers
  (six call sites each); all pass a `{type, row, ...}` of this shape, and only these two were read.
- 0x66EE10 `(_, player u32, {type, row, player})`: reads the type of the list's entry `row` (for an effect), stamps
  the time on that entry (`+0x28`, through 0xB9AD30) and sends the row (message 2445323563). The HUD row shows state
  6 while `now - entry[+0x28]` is under 2 s.
- 0.1.4 already pointed the activation's row at its type's entry. It did not for 0x66EE10, and `page_hold` never
  covered the flight (no record names the row then).

## The HUD row

0x1836510 `HudStratagemRow_Update`, per frame, for the entry of its index (`row + 14152`):

- the type (`+14156`) and the name are read from the entry; the name is set again when the type changes;
- the call-in time left (`+14104`) is the entry's `+0x20` less the clock; above 0 the state is 3 ("being called in");
- at 0 the state is, roughly, 2 when 0x69FC60 names the row, 4 on cooldown, 6 for 2 s after `+0x28`, 5 when it can't
  be used, else 1 (7 and 8 come from another system and were not read);
- the one thing kept in the row: in the frame the call-in ends it sets a flash of 1.5 s (`+14084`), and while the
  flash is above 0 the state is not worked out again. A row that gets another entry in that time shows it as being
  called in until the flash runs out.
- 0x1838CE0 sets the status text when the state changes, by the state alone.

So a call-in is not shown "by its row", as the mod's source said until 0.1.4: the row follows its entry. The wrong
name comes from the in-hand record (until the landing, when 0.1.4 changed back to the stratagem's page) and from the
flash.

## What the mod does with it (0.2.0, `page_lock = off`)

- **In hand**: on any rewrite of the list the entry the record names stays in it, in a row the page leaves free
  after its picks, and the record's row (and the twin's) is set to where the entry is now. A change with no free row
  for it is held back.
- **Thrown**: the activation and 0x66EE10 get the row of their type's entry; an entry out of the list is put in it
  first, the page as it is. Without a free row the page changes to the entry's own, as before.
- **Called in**: nothing to do but the flash, cleared on the rows whose entry changed (from the HUD list's update).
- An entry kept from another page leaves the list again when it is not in hand, not being called in, and 6 s past the
  throw.
- A pick missing from a full list (16) is not taken as used up: the game's appends write over row 16.
- `page_lock = on` is 0.1.4's behaviour, and what the mod falls back to when the in-hand code is not the one it
  knows.

## Seen in game (2026-11-17, HellForge 0.4.0-beta, solo, difficulties 1 and 11, P.S.S. active)

One session of the first 0.2.0 build, 139 activations, from the mod's log:

- The record keeps the row the mod writes: each of the 24 checks an update after a write to a record in hand found
  the row as written, the twin's row with it.
- The thrown ball takes the row written: after a page change in hand, the `activation:` line names the entry's new
  row (`slot 15 (now type 137 there)` after a move from row 10 to 15), so no lookup by type was needed.
- Opening the stratagem list again with a ball in hand puts the ball away (the game's own behaviour). So a page can
  change with a stratagem in hand only between the last arrow of its code and the list closing. That happened 18
  times in the session, and each time the entry stayed in the list on the new page.
- **The record shows stale for one update.** When the list opens, the local player's record is there for one update
  with the type and row of the last stratagem thrown; an update later it is gone (17 times) or holds the next code
  (3 times). The first build took it for a stratagem in hand whose row held another entry and put that stratagem in
  the list of the page shown, for 6 s. Fixed: such a record is believed only after 200 ms.
- **A row that gets an entry starts a flash** for an entry called in at any time before (state 3, 1.5 s, in its first
  update). The flash is cleared an update later, as for the rows that changed.
- **A row shows by itself after the list closes** when its entry on cooldown was replaced by one that is ready: to
  the HUD row that is "ready again", shown for 2 s (`row + 14080`, set by 0x1838CE0 on a change from state 4 to 1;
  3 s from 2 or 3 to 4). This is as old as the pages. Fixed with the flash: the rows that changed lose it too. Not
  seen in game after the fix yet.
- 0x66EE10 was not called for the local player in the session: putting a ball away in hand does not go through it.
  Its hook has not run in game.
- No page change was held back.

## After the mission

When the mission ends the game puts the profile's loadout (its first 16 picks, through 0x17514C0 called from
0x1088CD0) in the local player's list, 2 to 5 s before it resets the list and while HellForge still reports the
mission's state. The picks are the pages' own types, so the mod's update took the list for a page too full and cut
it down (16 to 14 after the first mission, 16 to 13 with another page's entries after the second). Versions before
0.2.0 did the same. Nothing was seen to go wrong after: the next mission, its stratagems picked again by the player,
got all 20 picks. From 0.2.0 the update leaves a list alone that holds none of the game's entries from ready-up
(Reinforce, Resupply...).

## Seen in game after the fixes (2026-11-17, HellForge 0.5.0 nightly, solo, two missions, no P.S.S.)

One session of the build as committed (the mod's source in the session is the repository's), from the mod's log and
the player's report:

- No stratagem showed by itself after a page change and the list closing. The rows that changed lost a linger of
  2 s 13 times (all state 1, the "ready again" case) and a flash once.
- The stale record did nothing: no stratagem was put in the list for it (0 times, 18 in the first session).
- 13 page changes with a stratagem in hand; all 27 checks of the record's row found it kept. Every entry kept from
  another page was out of the list again once its own page came back or it was called in; none was left in the list.
- After both missions the list was left alone (`pages: none of the game's 5 entries ...`; the list still had its 16
  entries when the game loaded its packages, 14 and 5 in the first session) and reset by the game 2 s later. The
  second mission had all 20 slots and stratagems, and its pages changed as in the first.
- No page change was held back, no activation needed its row looked up, no pick was taken as used up.
- 0x66EE10 was hooked alone in its chain in both sessions (17 mods loaded in the first).

## Still to see in game

- A thrown ball that ends without an activation (0x66EE10): its hook has not run.
- A page change held back for want of a free row, and a pick missing from a full list.
- Which phase showed the wrong name in 0.1.4 with `page_hold = off`. By the above it can only have been the moment
  between the code and the list closing, and the 1.5 s flash.

## Not looked at

- Co-op: the activation and 0x66EE10 send the row to the peers, whose copy of this player's list does not follow the
  pages (the mod writes the local list only).
