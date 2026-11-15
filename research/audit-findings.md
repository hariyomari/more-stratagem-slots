# Loadout audit: what breaks above 16 stratagems (solo)

Done 2026-11-13 on the dump of game build 25480438. All addresses are RVAs; the dump's base is 0x7FFCF6030000.

Method: a byte-level scan for code that reads the loadout count next to its entries, at +0x788/+0x188 (the record) or
+0x7C0/+0x1C0 (the same record inside a PlayerHistory entry, at +0x38). That gave 95 candidates. Each real one was
decompiled. Parts B and C are below. Part A (the low range) is in `audit-part-a-findings.md`. It adds:

- 0x4DBBF0 `PlayerHistory_AppendTypeForAllPlayers`: one more clamp to 16. It is a registered entry point with no code
  callers; whether it runs in solo play is unknown.
- `SyncedLoadout_ReceiveArrays` 0xB9E670: network.
- Every appender (0x5DBCC0, 0x856DD0, 0x1292300, 0x12DE330, 0x6AE1F0, 0xB4EEC0) adds without checking the total
  against 32.
- Everything else loops to the count.

The scan can miss code that reaches the entries through a pointer computed elsewhere, so this list is a strong base,
not proof that nothing else exists. The IDA database has names for the functions below and comments at each limit,
tagged LIMIT, LIMIT16 or LIMIT20.

## The two lists

- **Loadout record** (2544 bytes): entries at +0x188, count at +0x788, room for 32. The loadout screen, the profile
  and the additions (mission group, linked stratagems) work on it.
- **PlayerHistory entry** (5776 bytes, one per player): the record again at +0x38, so entries at +0x1C0 and count at
  +0x7C0. This is the in-mission list: the HUD, arrow input and call-ins read it. A second full copy of the record at
  +0xA28 is a snapshot taken when the stratagems' packages are loaded (`PlayerHistory_LoadLoadout` 0x1364C00).
- Per entry (0x30 bytes): type +0, uses +4, a flag byte at +9, the cooldown end time at +0x18 (+0x10/+0x20/+0x28 are
  other times).

There are no per-stratagem arrays of 16 inside PlayerHistory outside the record. The limits are all in the code that
copies the lists around.

## Limits on the solo path

| # | RVA | name | limit | effect above 16 |
|---|-----|------|-------|-----------------|
| 1 | 0x13620A0 | PlayerHistory_UpdateFromLoadout | stack arrays of 16 (types, uses, flags, u64 times), loop to the count, no check | overflow, the ready-up crash. 12 callers, incl. 0x1467790 `LoadoutScreen_ApplyLocalLoadout` (ready-up, twice) |
| 2 | 0x66FBF0 | PlayerHistory_BroadcastStratagems | stack arrays of 16, loop to the count, no compare at all | overflow into saved registers. Runs after **every** add, remove or reset of the in-mission list. It then applies the arrays locally through 0xB87A70, which wipes all 32 entries and rebuilds from them, and sends them (out of scope) |
| 3 | 0x66F190 | PlayerHistory_AppendLoadout | index clamped to 15 (`cmp ebp,10h; cmovnb` at 0x66F28E) | entries past 16 overwrite entry 15, count stays 16. Ready-up and 5 other callers |
| 4 | 0x66F330 | PlayerHistory_AppendEntries | clamp to 15 (0x66F424) | same |
| 5 | 0x66EFD0 | PlayerHistory_ResetStratagems | clamp to 15 (0x66F10C) | harmless (adds at most 6) |
| 6 | 0x66D8C0 | StratagemInput_MatchSequence | candidate list of 16 on the stack, no check (0x66DCBE) | overflow when more than 16 of the available stratagems match the arrows typed so far |
| 7 | 0x1834840 / 0x1834A10 | HudStratagemList_Init / _Update | **16 fixed row widgets** (stride 0x3760 at +0x110), row i shows entry i; the layout pass loops **32** times over them (0x1834DC2, again at 0x1834E80) | rows 17+ are never shown; with an entry 17+ present, the layout writes past the 16 rows |
| 8 | 0x135E4D0 | PlayerHistory_SetSharedUses | table of 16 {type, shared uses} | the 17th distinct type with shared uses is dropped silently (no crash) |
| 9 | 0x17514C0 | Profile_LoadLoadout | stops when the slots' summed cost (StratagemInfo+0xBC) passes 4 (0x17521CD), and at 16 entries (0x1752207). Its 150-type lookup (0x17521AE) is patched by HellForge itself when stratagem mods are installed (`stratagem_bounds.h`) | after a game restart only 4 slots came back from the profile. **Fixed in the mod** (hook `ProfileLoad`, 2026-11-13, untested) |
| 10 | 0x6AE1F0, 0xB4EEC0 | Loadout_AppendMissionAndLinked, Loadout_RunAppendHooks | no check against 32 | only matters near 32: entry 33 would overwrite the count |
| 11 | 0x1340E60, 0x1348F10 | icon/uses list from a player's history | output capped at 20 | capped, no crash; shows 20 at most (UI elsewhere) |

Fine up to 32: the call-in path (0x66E6D0 → `PlayerHistory_OnStratagemUsed` 0x135C5D0, `_StartStratagemCooldown`
0x135C2C0), `Loadout_ApplyStratagemArrays` 0x11E82D0 (reads as many as the source holds), remove, clear inactive,
package cost and collection, the JSON writers, the debug dump, `Profile_SaveLoadout`, the picker.

Another limit to keep in mind: the arrow code buffer holds 12 arrows, and the HUD draws at most 10 arrow icons per row.

## Network only (out of scope)

`PlayerHistory_SyncAllToPeer` 0x135EA10 and `_SyncPeerToPeer` 0x1360970 use stack arrays of 16 without a check.
Every stratagem message (0xBEE770, 0xBEC910, rpc_sync_stratagems 0x670530, rpc_sync_stratagem_changes 0x135C810)
carries {count, x[16]}. `SyncedLoadout_OnReceive` 0x13819B0 copies a received count without a cap.

## What it means for going past 16

"Copy 16, then write 17+ straight into the PlayerHistory entry" does not work: the next add, remove or reset runs #2,
which rebuilds the entry from 16-element arrays (or overflows them). The workable route is to replace the packers:

- #1 and #2: hooks that do the same packing with arrays of 32 and call the same store/apply functions, and send only
  the first 16 to the network (or nothing, solo).
- #3, #4: change the clamp from 16 to 32 (one byte each).
- #6: a replacement matcher with a list of 32.
- #7: keep the 16 row widgets and show a page of the list (Q/E or mouse wheel picks the page), fixing the 32-pass
  loop so it never reaches past row 16.
- #8: decide whether 16 distinct shared-use types is enough (likely: only some stratagems share uses).
- #9: done (the profile load hook).
