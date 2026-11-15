# More Stratagem Slots: research notes

Notes from building the mod (`more-stratagem-slots/`): a HellForge code mod that raises the loadout's stratagem slots
above four (default 8), for game build 25480438
(game.dll sha256 2E2C3B7C2500646DADD5F2B4C6E0504DBB7E7896139F64CDDC0D1813C718F51E, HellForge 0.4).

## Requirements

- The slot count is a setting, not hard-coded: 4 to 20, 8 by default, read from `mods\more-stratagem-slots.ini`.
- The loadout screen shows slots in rows of four: the first four as now, the next four in a row underneath.
- The in-mission HUD lists every picked stratagem.
- Co-op: every player runs the mod (it is a gameplay mod, so HellForge's modlist matching enforces that).
- HellForge itself stays unmodified, so its installer/updater keeps working.

## Research

- `research/tools/Dump-GameDll.ps1`: copies the unpacked game.dll out of a running session (the file on disk is
  packed with WinLicense). Output: `research/bin/game.dump.dll` (not committed).
- What HellForge already documents (`HellForge/native/mod-loader/loadout_scrub.c`): a PlayerHistory record's loadout
  (+0x38) holds stratagem slots of 0x30 bytes at +0x188 with their count at +0x788 (room for 32); the profile keeps
  32 {stable id, uses} pairs at +0x4C, copied into the slots capped at 32 (0x17513B2); a received loadout is read with
  the same layout (0x11E8315). So the 4 limit is likely in the loadout screen, not the data or the network.

### Findings in the dump (RVAs; the 2026-11-13 dump's base is 0x7FFCF6030000)

- 0x135D220 `PlayerHistory_DebugDump`: prints `loadout.stratagems[0..31]` against the count at +0x788. Confirms 32.
- 0x6AE1F0 `Loadout_AppendMissionAndLinked`: at mission start appends the mission group (HellForge's hook at
  0x6AE35D) and each slot's linked stratagem (StratagemInfo +0xC8, e.g. Eagle Rearm) after the picked ones, so the
  in-mission list is already variable-length.
- 0x18966A0 `LoadoutScreen_CommitSlotsToLoadout`: clears the record's slots, then copies **4** slot widgets of the
  loadout screen (`panel + 0xA14C`, stride 0x12A8; the count `mov ecx/r?, 4` at 0x18966D8..0x1896708) into the
  record, with each one's uses. This is the 4.
- About 30 functions in 0x1893F30..0x189EAD6 use the slot widgets' stride 0x12A8 (layout, input, drawing); a few more
  at 0x146C2F0..0x146F3B0. The widgets are fixed members of the screen object, so slots 5..8 need widgets the mod
  allocates and every one of those loops taught about them.
- 0x18D8710 `StratagemPicker_BuildGrid`, 0x18D1890 `StratagemPicker_MarkEquipped`: the picker already loops over
  the record's whole count (it greys out what is equipped), so it needs no change for more slots.
- 0x189D120 (the commit's main caller) copies the committed loadout into fixed local arrays of about 15 entries for
  a report: keep the picked count well under that.
- `Profile_SaveLoadout` (0x1751350) saves up to 32 slots into the profile, so extra slots are saved there too.

## Test mod (not in this repository)

Throwaway: hooks 0x18966A0 and appends four stratagems after the four picked, to check the mission side (HUD,
call-ins, Eagle Rearm, packages) with eight. No loadout screen change.

Result, 2026-11-13, solo: the mission list showed all eight after Reinforce, SOS and Resupply; all four extras called in
fine; no crash. The commit runs on every slot change, twice (both calls in 0x189D120). The test's uses came out as
unlimited for every extra (get_live at HF_EVENT_GAME_LOADED could not read the rows yet), so Eagle uses and Eagle Rearm
were not tested: the real mod must compute uses as the commit does (base uses + ship upgrades, 0x18966A0).
- Widget positions are floats set in code (e.g. 0x43C58000 = 395.0), not a UI resource: a second row is placed by
  code too.

### The loadout card (mapped 2026-11-13; names are in the IDA database)

The UI is a retained tree of nodes embedded in their owners (init `0x1446840(node, kind)`, size `0x1447160`,
position `0x14476A0`, add child `0x144C5C0(parent, child)`). Nothing is heap-allocated per widget.

- The lobby / loadout screen `S` holds 4 player cards at `S + 342648` (stride 126488). Each card's panel `P` is at
  `card + 23416` (so the local card's panel is `S + 366064`). `LoadoutScreen_CommitSlotsToLoadout(P, loadout)` is
  called on that panel.
- Panel layout (`LoadoutCard_BuildPanel` 0x1893F30, called once by `LoadoutCardOuter_Build` 0x1899430):
  - `P + 2136`: 7 equipment widgets (55x55), in `P + 272`, at x = 30 + i*(w + 5.8), y = -79.
  - `P + 35576`: the booster box (focus index 4).
  - `P + 36544`: the 4 stratagem slot widgets (80x80, stride 0x12A8), children of `P` itself, at
    x = 30 + i*(80 + 5), y = 30. The panel's own fields continue straight after them (`P + 55648`..), so the array
    cannot grow in place.
  - `P + 55648` the shown loadout, `+55656` hovered index, `+55660` focus index, `+55664` previous focus,
    `+55672` expanded flag (the local, editable card).
- Slot widget (0x12A8): `SlotWidget_Init(w, _, small)` 0x1892DB0; `+0x1288` u16 state bits (2 focused, 4 locked or
  hidden, 8 picking), `+0x128C` u32 stratagem type (`SlotWidget_SetType` 0x1893600 sets it and the icon),
  `SlotWidget_ApplyState` 0x18932F0 redraws from the state bits.
- `LoadoutCard_RefreshFromLoadout(P, loadout, expanded, force)` 0x1895A20: fills the 4 widgets from the record's
  slots (stops at 4), locks slots past the number unlocked.
- Focus indices: 0-3 stratagems, 4 booster, 5-11 equipment. `LoadoutCard_SetFocus` 0x1895770 maps an index to a
  widget with `i <= 4 ? strat[i] : equip[i - 5]`: an index of 12 or more would write past the equipment array, so
  extra slots need their own handling there. `LoadoutCard_HandleNav` 0x18950B0 moves focus (left/right within the
  row, up/down between rows, off either end to the next card).
- Picker: opening it (0x146E9D0) sets the picker mode `S + 10264` (10 stratagem when focus is 0-3, 5 booster) and the
  target slot `S + 10268` = the focus index. `LoadoutScreen_PickerConfirm` 0x146E0A0 (mode 10): backpack / support
  exclusivity checks over the 4 widgets, `LobbyCard_SetSlotStable(card, slot, stableId)` 0x189D050 (ignores
  slot >= 4), `LobbyCard_CommitAndBroadcast(card, -1)` 0x189D120, then moves focus to the next empty one of the 4
  or closes the picker, then `Profile_SaveLoadout`.
- Network: setting a slot sends `0xBE0800(-2/-4, player, slot, stableId)`; `Net_OnRemoteSlotSet` 0xB90260 applies a
  remote player's pick through the same setter (so slot >= 4 is ignored on a peer without the mod). The commit's
  caller 0x189D120 then sends the whole loadout in arrays of 16 (0xBE4370), with **no bounds check**: keep
  the picked count at 16 or fewer.
- The ready check 0x189C250 warns about empty slots among the 4 only.
