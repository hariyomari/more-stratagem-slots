# Loadout audit, part A: findings

Audited 2026-11-13 against `research/bin/game.dump.dll` using IDA MCP. All addresses below are RVAs; image base is `0x7FFCF6030000`. Read the complete brief, HANDOFF.md and README.md first. `idb_list` found the existing `gamedump` session, which supplied the initial analysis. After that worker exited, the saved database was reopened in an initialized worker. Function names and limit comments were added and `idb_save` returned success for `research/bin/game.dump.dll.i64`.

Coverage: **29 candidate hits, 28 containing functions, 25 real loadout functions, three dismissed functions**. The two hits at `0x6AE351` and `0x6AE469` share one function. The table reports containing-function starts and maps the original hits in notes.

`count` means iteration over the stored count without an independent 16/32 cap; it does not guarantee that an invalid count is safe. `other: unchecked append` means writes start at the current count with no remaining-capacity check: the physical record still holds only 32 entries. Limits on how many entries a producer adds are separate from a limit on the final total. Suggested descriptive names were applied to previously unnamed functions; subsystem interpretations are provisional where stated. Solo classifications are based on static paths, not a new game test. “Shared” means there is no remote-only restriction in the inspected helper; it is not proof that every registered entry point is invoked in a solo session.

| RVA | name | what | limit (count / 32 / 16 checked / 16 unchecked / other) | solo or network | notes |
|-----|------|------|------|------|-------|
| `0x4DBBF0` | `PlayerHistory_AppendTypeForAllPlayers` | Adds a supplied type to each matching player's history loadout and synchronizes it. | **16 checked** | Shared history mutation; registered entry point, solo invocation unproven | Hit `0x4DBC25`. At count >=16, writes index 15 and sets count to 16. Real history record: stride `0x1690`, embedded loadout +`0x38`; rbp here is a record pointer. |
| `0x4DBDD0` | `PlayerHistory_RemoveTypeForAllPlayers` | Finds and removes a supplied type, shifts later entries down, then synchronizes. | count | Shared history mutation; registered entry point | Hit `0x4DBDF0`. Search and shift follow +`0x788`; no local per-entry list. Its call to `0x66FBF0` inherits that function's 16-entry hazard. |
| `0x5DBCC0` | `Loadout_AppendMissionRuleStratagems` | Resolves mission-rule options and appends their stratagems, with conditional duplicate filtering. | other: unchecked append | Solo/shared: runner `0xB4EEC0` and card refresh `0x1895A20` | Hit `0x5DBE34`. Existing-entry search follows count. Two 128-byte u32 scratch lists hold rule choices, not a copy indexed by loadout count. Four source options are visited per selected rule; append at `0x5DBF64`, increment at `0x5DBF7F`, no final-total guard. |
| `0x66D650` | `Loadout_HasSpentEagleUses` | Tests whether an Eagle-linked entry has fewer remaining uses than its computed maximum. | count | Solo/shared gameplay helper | Count comparison `0x66D6CA`; no entry scratch list. Eagle-linked test uses info +`0xC8` == `0x31`. |
| `0x66D8C0` | `StratagemInput_MatchSequence` | Matches input against usable entries and chooses among matching types. | **16 unchecked** | Solo/shared local input | Count scans can append one candidate index per distinct matching type to a 64-byte stack list. Separate 12-u32 scratch space holds an input sequence, not loadout entries. See below. |
| `0x66E430` | `PlayerHistory_HasRearmCooldown` | Looks for type `0x31` with a cooldown timestamp later than the current time. | count | Shared gameplay helper; direct invocation unproven | Count at `0x66E51D`, loop comparison `0x66E562`. No fixed entry list; no direct code caller in the database. |
| `0x66E580` | `PlayerHistory_AllEagleUsesExhausted` | Checks a player's Eagle-linked entries for any remaining uses. | count | Solo/shared gameplay | Count comparison `0x66E6B0`; used by activation and other gameplay paths. |
| `0x66E6D0` | `StratagemInput_ActivateMatch` | Activates a matched entry and handles related Eagle/rearm behavior. | count | Solo/shared gameplay | Hit `0x66E75D`. Uses the supplied match index and scans rearm entries to count at `0x66EC36` / `0x66ED7B`. No count-sized local entry copy. |
| `0x66EFD0` | `PlayerHistory_ResetStratagems` | Clears the count and optionally rebuilds entries from a six-type preset. | other: at most 6 rebuilt; 16 checked append index | **Solo/shared**, including `0x1467790` | Hit `0x66F0CE`. Resets count at `0x66F0B2`, visits six source types, skips zero types. Clamp is present but unreachable in this bounded rebuild. Does not preserve the old list. |
| `0x66F190` | `PlayerHistory_AppendLoadout` | Appends nonzero source records, retaining explicit uses or calculating them. | **16 checked** | **Solo/shared**, including `0x1467790` | Hit `0x66F26A`. Further entries overwrite index 15 and leave count 16. Source traversal itself follows its supplied count. |
| `0x66F330` | `PlayerHistory_AppendEntries` | Appends source types/flags while deriving remaining uses from history. | **16 checked** | Shared registered helper; solo invocation unproven | Hit `0x66F405`. Same index-15 overwrite behavior; no direct code callers found. |
| `0x66F4C0` | `PlayerHistory_RemoveStratagem` | Finds a type, shifts later records down and decrements count. | count | Solo/shared history mutation | Hit `0x66F537`. No 16 cap in search/shift, but calls the constrained synchronizer `0x66FBF0`. |
| `0x66F8A0` | `PlayerHistory_UseEagleRearm` | Finds the rearm entry, uses it and reports its cooldown. | count | Solo/shared Eagle update | Type `0x31` search ends at count (`0x66F980`); caller `0x8A2990`. |
| `0x66FBF0` | `PlayerHistory_BroadcastStratagems` | Packs history entries into parallel lists, optionally applies locally and sends synchronization. | **16 unchecked** | **Solo and network** | Hit `0x66FD00`. Types/uses/u64 times/flags each have 16 payload elements. Local application is explicit for target -1; see below. |
| `0x670D50` | `PlayerHistory_CanUseStratagem` | Finds a type in history and checks its usability helpers. | count | Solo/shared gameplay | Search count at `0x670DB8`, comparison `0x670DDB`. No count-sized local copy. |
| `0x6AAB30` | `StratagemDeployment_HandleRequest` | Handles a deployment request, including checking the requested type's cooldown in history. | count | Shared gameplay/RPC path; solo execution not independently traced | Hit `0x6AB035`. Matching-type scan uses +`0x788` at `0x6AB074`, +`0x188` at `0x6AB099`, then count comparison. No per-entry scratch list. Deployment name is a descriptive interpretation of the surrounding handler. |
| `0x6AE1F0` | `Loadout_AppendMissionAndLinked` | Prepends mission-group entries and appends linked types while visiting the growing list. | other: unchecked prepend/append | **Solo/shared**, via `0xB4EEC0` | Both hits `0x6AE351`, `0x6AE469`. Up to six mission types are prepended, shifting existing entries; linked-type multiplicity is checked per type using info +`0xCC`, not against total capacity. No total-32 guard at shift `0x6AE3D6` or append `0x6AE520`. The `0x258`-byte scratch array is indexed by type ID. Dump has an external hook at `0x6AE35D`; its target is outside the dump, so original instructions there were not reconstructed. |
| `0x856DD0` | `Loadout_AppendFourSelectionBonuses` | Resolves four selected table rows and appends each row's nonzero stratagem bonus. | other: up to 4 added, unchecked total | **Solo/shared**, via `0xB4EEC0` | Hit is function start. Four append sites `0x856E57`, `0x856EC7`, `0x856F37`, `0x856FA7`. The comparisons with `0x15` bound selection-table indices, not loadout count. The exact selection subsystem is unconfirmed. |
| `0x8A2990` | `EagleState_UpdatePlayer` | Updates player state including rearm cooldown detection and triggering rearm. | count | Solo/shared gameplay update | History scan `0x8A2BE1`..`0x8A2C27`; calls `0x66F8A0`. Name reflects the inspected Eagle/rearm branch; function also updates other state. |
| `0x9F2630` | `Loadout_UpdateLocalType28Cooldown` | Finds local type `0x1C`, computes a cooldown and applies/reports it. | count | **Solo/shared local-player path** | Hit `0x9F278A`. History-relative count +`0x7C0`, entries +`0x1C0`, comparison `0x9F281B`. Calls `0xB9A6D0` at `0x9F283F`. No fixed entry list. |
| `0xB4EEC0` | `Loadout_RunAppendHooks` | Runs registered loadout augmenters, including inlined system `0xF6` and `0x104` additions. | other: unchecked append | **Solo/shared**, including `0x1467790` | Hit `0xB4EEFF`. `0x104` temporary buffer is `0x600` bytes = 32 records, populated by `0x12DE160`; these are appended after the existing count without checking remaining room (`0xB4EF8B`). `0xF6` adds one unchecked (`0xB4F06B`). Dispatches the other appenders listed here. |
| `0xB9A6D0` | `PlayerHistory_SetStratagemCooldown` | Updates one entry's cooldown, related Eagle entries and matching entries across player histories. | count | **Solo and network/RPC** | Hit `0xB9A718`. Solo/local call at `0x9F283F`; wrapper `0xBC1BD0` also calls it. Loops compare against history +`0x7C0` at `0xB9A962`, `0xB9AA0A`; no 16-element temporary list. |
| `0xB9E670` | `SyncedLoadout_ReceiveArrays` | Rebuilds a synchronized loadout from incoming types/uses and equipment data. | **16 unchecked source lists**, count-driven destination | Network/RPC receive; solo loopback **unproven** | Hit `0xB9E6B0`. Makes fixed `0x44`-byte copies of count+16-u32 type/use inputs and trusts the supplied type count while rebuilding. No receive-side 16/32 check. Sole direct caller is RPC argument adapter `0xBBF1E0`. |
| `0x1292300` | `Loadout_AppendSystemF6Selection` | Resolves the system's selected item and appends its stratagem with initial uses. | other: one added, unchecked total | Shared registered callback; **solo equivalent inlined in `0xB4EEC0`** | Hit `0x129234B`. Write `0x12923BB`, count increment `0x12923D3`; no capacity test. Standalone function has no direct code callers. |
| `0x12DE330` | `Loadout_AppendSystem104Entries` | Collects system `0x104` entries into scratch space and appends them to the existing record. | other: up to 32 added, unchecked total | Shared callback; **solo equivalent inlined in `0xB4EEC0`** | Hit is function start. `0x600`-byte scratch; producer `0x12DE160` asks its enumerator for at most 32 (`0x12DE1E6`). Append `0x12DE398`, increment `0x12DE3B4`; no check that old count + added count <=32. No direct code callers of standalone function. |

## Limits below 32

Direct callers below are containing-function RVAs from code xrefs, with call-instruction RVAs in parentheses where useful. Data references are listed separately; they are not direct callers, and their presence does not establish an executable solo path. No xref results were truncated.

### `0x4DBBF0`: append index capped at 15

Instruction RVAs `0x4DBD10`, `0x4DBD16`; index-15 fallback is initialized at `0x4DBC2D` and refreshed at `0x4DBCAD`.

Evidence: `cmp esi, 10h` / `cmovnb esi, edi`, with edi = 15. The write uses index esi, and count becomes esi+1 at `0x4DBD4E`: overflow replaces the sixteenth entry.

Direct callers: **none found**. Data xrefs: `0x24020A8`, `0x24020B8`, `0x3FEFD44`. The body also invokes the unchecked packer at `0x4DBD61`.

### `0x66D8C0`: 16 candidate indices, unchecked

Instruction RVA **`0x66DCBE`**, increment `0x66DCC6`; enclosing scan compares to loadout count at `0x66DCD1`.

Evidence: `mov [rsp+r12*4+118h+var_78], r14d` / `inc r12d`. IDA's frame gives `var_78` size `0x40` (`u32[16]`), followed immediately by the cookie at `var_38`.

This is conditional on **more than 16 distinct matching candidate types**, not simply having count 17. Duplicate-type candidates can reuse an existing index, and unusable/nonmatching entries are skipped. The append has no capacity check, so raising the count to 32 leaves a real input-path stack-overflow risk when enough distinct types match.

Direct caller: **`0xA900D0`** (call `0xA903CC`).

### `0x66EFD0`: reset to at most six preset entries; checked 16-index clamp

Instruction RVAs `0x66F0B2` (clear count), `0x66F0DD` / `0x66F150` (six iterations), **`0x66F109` / `0x66F10C`** (clamp).

Evidence: `cmp esi, 10h` / `cmovnb esi, r13d`, with r13d = 15. Because the count was just cleared and only six preset types are visited, this clamp cannot truncate a larger list within this rebuild; the reset already discarded that list.

Direct callers: **`0xAD9020`, `0xAD91C0`, `0xADBB50`, `0x1467790`, `0x1757B20`**. The solo loadout application caller is `0x1467790` (`0x1467906`).

### `0x66F190`: 16-entry append clamp

Instruction RVAs **`0x66F28E`, `0x66F291`**; fallback 15 is loaded at `0x66F276`.

Evidence: `cmp ebp, 10h` / `cmovnb ebp, r13d`. Destination index is ebp; `0x66F2DD` and `0x66F2EB` set count to ebp+1. Additional nonzero inputs repeatedly replace entry 15.

Direct callers: **`0x8568A0`, `0x856FE0`, `0xAD9020`, `0xAD91C0`, `0xADBB50`, `0x1467790`**. Solo application call is `0x1467A52` in `0x1467790`.

### `0x66F330`: 16-entry append clamp

Instruction RVAs **`0x66F420`, `0x66F424`**; fallback 15 is set at `0x66F40A`.

Evidence: `cmp r15d, 10h` / `cmovnb r15d, r13d`. Count is set to r15d+1 at `0x66F472`; excess entries replace index 15.

Direct callers: **none found**. Data xrefs: `0x24305B8`, `0x24305F0`, `0x400CCE8`. Its body has no remote-only gate, but a solo invocation of this registered helper was not established.

### `0x66FBF0`: parallel 16-element lists, unchecked, including local application

First write instruction **`0x66FD24`**; other payload writes `0x66FD43` (uses), `0x66FD99` (u64 remaining time), `0x66FDA9` (flag); count loop `0x66FDB0` / `0x66FDB2`.

Evidence: `mov dword ptr [rbp+rsi*4+330h+var_110+4], ecx` / `cmp edi, eax`. eax comes from record +`0x788`; no comparison against 16 precedes these writes.

The types object starts at frame offset `0x328`: 4-byte count plus 64-byte payload, ending at `0x36C`; the uses object similarly starts at `0x2D8` and ends at `0x31C`. The time object holds 128 payload bytes and the flag object 16. Alignment gaps beyond payloads do not extend the list contract: index 16 is already outside its payload, and later copies transport only 16 values.

**Solo relevance is explicit:** target -1 passes the branch at `0x66FE91` and calls local `ShipInventory_RpcSyncStratagems_Local` (`0xB87A70`) at **`0x66FFC7`**, before network sends. That receiver clears the history entry arrays and applies the packed lists. The reset/append callers use target -1, including the path reached from local loadout application. This function cannot be excluded as network-only.

Direct callers: **`0x4DBBF0`, `0x4DBDD0`, `0x66EFD0`, `0x66F190`, `0x66F330`, `0x66F4C0`, `0x670750`, `0x6707A0`, `0xB48C20`, `0xB5EA90`**.

### `0xB9E670`: incoming 16-value lists copied locally, then count trusted

Instruction RVAs **`0xB9E971`, `0xB9E980`**, loop at `0xB9E98B`, destination count at `0xB9E99C`.

Evidence: `mov eax, dword ptr [rbp+rdx+60h+var_C0+4]` / `mov eax, dword ptr [rbp+rdx+60h+var_70]`. Types and uses were copied as fixed count+16-u32 objects (`0x44` bytes), but the loop is controlled by the incoming type count.

At count >16 this reads beyond those copied payloads; at count >32 it also exceeds the destination's record storage. Sole direct caller: **`0xBBF1E0`** (call `0xBBF478`), an RPC argument adapter that itself builds fixed lists while trusting incoming counts. Treat this as a network/RPC boundary; a solo loopback path to this specific handler was not established in part A.

### Capacity implications of the other appenders

No additional hard total limit below 32 was found in the remaining real candidates. However, **none of the inspected destination append/prepend paths enforces final count <=32**. Mission/linked additions, rule bonuses, the four selections and systems `0xF6`/`0x104` can push a nearly full record past its own storage. A 32-entry temporary source buffer in `0xB4EEC0` / `0x12DE330` is not a remaining-capacity guard for an already populated destination.

Part A therefore does not establish that 17–32 entries are safe after fixing ready-up alone. The input candidate list, local history packing and append clamps remain separate obstacles. The known `0x13620A0`, `0x189D120` and related part B/C functions were background references, not re-audited here. No game code bytes, mod code or HellForge repository files were changed.

## Dismissed

- `0x917151`: **unrelated structure** — containing function `0x917120` writes byte flags at object +`0x188` and +`0x788` (`0x917273`, `0x91731B`); these are not a 0x30-stride record and u32 loadout count.
- `0xB4DC4A`: **unrelated structure** — containing function `0xB4DBA0` toggles a byte reentrancy flag at system +`0x788` (`0xB4DC64`, `0xB4E329`); nearby `0x188` occurrences belong to stack addressing.
- `0x1019440`: **stack displacement** — containing function `0x10193C0` establishes rbp from rsp (`0x10193CA`); the matching displacement is frame-relative. Its separate +`0x188` accesses concern another structure, with no loadout count/entry pairing or 0x30-stride loadout copy.
