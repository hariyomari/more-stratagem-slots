# Loadout audit, part A (to run in a separate session)

Research only: no mod code and no changes to HellForge. Write the results to
`research/audit-part-a-findings.md` (format at the end).

## Why

The stratagem slots mod (`stratagem-slots/`) lets the loadout hold more than 4 stratagems. The game's loadout record has
room for 32, but some of the game's code copies it into lists of 16. With 17 or more entries the game crashed at
ready-up, so the mod now trims the record to 16. To go past 16 (solo play only), we need every place that would break
with 17-32 entries. The audit is split in three: parts B (player history) and C (loadout screen, profile, in-mission
list) ran in the original session. This is part A.

## Setup

- Read the working notes (environment) and `research/README.md` (sections "Research" and
  "Findings": known functions and RVAs).
- IDA database: `research/bin/game.dump.dll.i64` (analysis of the memory dump
  `game.dump.dll`). With the IDA MCP tools: `idb_list` first; if a session on `game.dump.dll` is listed (id
  `gamedump`), use it, else `idb_open` the dump.
- Address math: IDA address = 0x7FFCF6030000 + RVA. Report RVAs.
- Comments and names in the database are welcome (descriptive names for real loadout functions still named sub_xxx,
  a comment at each real limit). Save the database at the end.

## The data

The loadout record is 2544 bytes:

- stratagem entries of 0x30 bytes each, starting at +0x188: u32 type at +0x188, u32 uses at +0x18C, the rest mostly zero;
- the u32 count at +0x788;
- room for 32 entries.

A PlayerHistory entry (5776 bytes, one per player) holds the same record at +0x38, so there the entries start at +0x1C0
and the count is at +0x7C0.

Known so far:

- 0x13620A0 (player history, run at ready-up) copies the count's worth of entries into lists of 16 without checking.
  This was the crash.
- 0x189D120 and 0xBE4370 also use lists of 16.
- 0x135D220 `PlayerHistory_DebugDump` loops to 32.
- 0x6AE1F0 `Loadout_AppendMissionAndLinked` adds the mission's stratagems and each pick's linked stratagem (such as
  Eagle Rearm) after the picks.
- 0xB4EEC0 runs the game's additions on the record before 0x13620A0. The mod hooks it to trim to 16.

## Candidates

These are RVAs of code chunks found by a byte-level scan. Each reads +0x788 near +0x188..0x1B7, or +0x7C0 near
+0x1C0..0x1EF. A chunk can start in the middle of a function, so look up the containing function first. Many are false
positives: another structure that happens to use these offsets, or stack variables addressed through rbp. Dismiss those
quickly from the instructions around the hit.

0x4DBC25, 0x4DBDF0, 0x5DBE34, 0x66D650, 0x66E430, 0x66E580, 0x66E75D, 0x66F8A0, 0x670D50, 0x6AB035, 0x856DD0,
0x8A2990, 0x917151, 0x9F278A, 0xB4DC4A, 0xB9A718, 0xB9E6B0, 0x1019440, 0x129234B, 0x12DE330

Likely real: 0x66D650..0x670D50 (PlayerHistory helpers next to the ones already done), 0x4DBC25/0x4DBDF0 (they
call `PlayerHistory_BroadcastStratagems`), 0x856DD0, 0x129234B and 0x12DE330 (these increment the count).

Already done in the original session, no need to redo (names are in the database):

- 0x66D8C0 `StratagemInput_MatchSequence`;
- 0x66EFD0 `PlayerHistory_ResetStratagems`;
- 0x66F190 `PlayerHistory_AppendLoadout`;
- 0x66F330 `PlayerHistory_AppendEntries`;
- 0x66F4C0 `PlayerHistory_RemoveStratagem`;
- 0x66FBF0 `PlayerHistory_BroadcastStratagems`;
- 0x6AE1F0 `Loadout_AppendMissionAndLinked`;
- 0xB4EEC0 `Loadout_RunAppendHooks`.

For context, those showed:

- PlayerHistory's stratagem list stops at 16 (the appenders clamp, so an extra entry overwrites entry 15).
- 0x66FBF0 copies it into lists of 16 without checking.
- The input matcher keeps a candidate list of 16 without checking.

A function from your list that adds to PlayerHistory without the 16 clamp, or copies its list somewhere smaller, is the
most useful thing to find.

## For each real loadout function

1. What it does, in one line, and a suggested name.
2. Its limit on the number of entries:
   - loops to the record's count;
   - stops at 32;
   - stops at 16;
   - copies into a fixed-size list, with or without a check. Watch for local lists of 16 filled in a loop over the
     count: 64-byte lists of u32 or 128-byte lists of u64.
3. Whether it runs in solo play or only for networking (networking is out of scope, but note it).
4. If its limit is below 32: its direct callers (RVAs).

## Findings file format

```
# Loadout audit, part A: findings

| RVA | name | what | limit (count / 32 / 16 checked / 16 unchecked / other) | solo or network | notes |
|-----|------|------|------|------|-------|

## Limits below 32
(for each: the instruction RVA, one or two lines of evidence, direct callers)

## Dismissed
(RVA: two-word reason)
```

Keep decompiled code out of the findings except a line or two of evidence per limit.
