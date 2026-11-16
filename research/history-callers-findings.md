# Remaining player-history callers

What followed (0.1.2): the mod no longer hooks `0x13620A0` at any slot setting. Its `ProfileLoad` hook now gives the three callers that feed the player history 16 picks at most, told apart by return address, so the "hook stays for 17-20" conclusion below was overtaken. The line numbers cited for `ProfileLoad` are from before that change.

Both callers pass a fresh, full stack copy of `Profile_LoadLoadout`'s result. Neither adds stratagems to that copy or runs append hooks. Under the handoff's assumptions and the current mod, neither can supply more than 16 entries when `slots <= 16`; the conditional history hook can stand.

## Evidence and scope

Read directly from `research/bin/game.dump.dll`, build 25480438, with LLVM's x86-64 disassembler; file offset = RVA. SHA-256: `46cc682b7c47e5c5b117b0d5e1558f2f83b8955ab335a5a06b00b7fc5fd3faca`. Addresses below are RVAs (subtract image base `0x7FFCF6030000` from LLVM's VAs). No game, mod or IDA database changes were made. Function names and record layout follow the handoff note this was written from (not kept).

Reproduction, in PowerShell (second function: start `0x7FFCF739B6C0`, stop `0x7FFCF739BD88`):

```powershell
& 'C:/Program Files/LLVM/bin/llvm-objdump.exe' --disassemble --start-address=0x7FFCF70B50F0 --stop-address=0x7FFCF70B5828 --x86-asm-syntax=intel research/bin/game.dump.dll
```

## `0x10850F0`, history call at `0x108542C`

**Observed record source:** `r8` is a stack copy of the returned profile loadout, not a PlayerHistory entry or an incoming network record:

```asm
10853A9  lea rcx, [rsp+48h]
10853AE  call 17514C0             ; Profile_LoadLoadout
10853B3  lea rcx, [rsp+0A40h]     ; destination record
10853BB  mov rdx, rax             ; returned record
10853BE  mov r8d, 9F0h            ; all 2544 bytes, including count
10853C4  call 20988F0             ; copy
; ... PlayerHistory lookup and flag checks ...
108541D  xor r9d, r9d
1085420  lea r8, [rsp+0A40h]
1085428  lea rdx, [r9-3]          ; peer argument = -3
108542C  call 13620A0
```

1. **Record:** the full profile result copied to `rsp+0xA40`, as above.
2. **Append hooks earlier:** no call to `0xB4EEC0` anywhere in `0x10850F0..0x1085827`.
3. **Count bound:** no local count comparison or clamp. The intervening loop (`0x10853C9..0x108541B`) checks the number of PlayerHistory players at `history+0x2D200`, their peer ids and flag `entry+0x2B`; it never reads the copied record's count (`rsp+0x11C8`). This controls whether to call, not the entry count.
4. **Appends/count writes:** none on the copied record. After its copy, no mutating helper receives its pointer before `0x13620A0`, and there is no explicit write to its count. Later copies (`0x1085660` onward) concern a separate ring of 256-byte objects at `this+0x530`, not this record.
5. **Purpose:** client join/synchronization acceptance. The log at `0x1085293..0x10852A1` references `"Join accepted and synced!"` (string RVA `0x2262FE8`) under `"ClientSynchronizer"` (`0x2262B00`).

**Inference:** this path inherits the profile loader's count bound. The PlayerHistory lookup does not change that count or append mission/linked stratagems.

## `0x136B6C0`, history call at `0x136BD20`

**Observed record source:** another full copy of the profile loadout, this time at `rbp+0xB90`:

```asm
136BCE0  lea rcx, [rsp+30h]
136BCE5  call 17514C0             ; Profile_LoadLoadout
136BCEA  lea rcx, [rbp+0B90h]     ; destination record
136BCF1  mov rdx, rax
136BCF4  mov r8d, 9F0h
136BCFA  call 20988F0             ; copy
136BCFF  mov rax, [347CEF0h]      ; RIP-relative network pointer
136BD06  mov rdx, [rax+0B398h]    ; local peer id
136BD0D  call 135BF00             ; peer exists in PlayerHistory?
136BD12  test al, al
136BD14  je 136BD25
136BD16  xor r9d, r9d
136BD19  lea r8, [rbp+0B90h]
136BD20  call 13620A0
```

1. **Record:** the full profile result copied to `rbp+0xB90`, as above.
2. **Append hooks earlier:** no call to `0xB4EEC0` anywhere in this function. The code ends at `0x136BD57`; `0x136BD58..0x136BD87` is its 12-entry switch table, not further instructions.
3. **Count bound:** no local count comparison or clamp (copied count is at `rbp+0x1318`). Earlier gates check a state change and the result flag from `0x1750870` (`0x136BCB7..0x136BCDE`). The helper `0x135BF00..0x135BF2C`, read separately, only searches PlayerHistory entries by peer id and returns a Boolean; it neither receives the record pointer nor mutates it. These gates do not bound stratagem count.
4. **Appends/count writes:** none on the copied record. Between copying it and the history call, the only helper is the read-only peer lookup above. The earlier state-machine buffer clears/writes target other structures; they do not append to this subsequently created copy.
5. **Purpose:** a progression/profile loading state machine. Dispatch at `0x136B6EB..0x136B73B` uses states 1 through 12; state 12 goes to `0x136BCB7`, the path above. Earlier branches construct requests using `"%s/Progression/ItemPackages"` (`0x22C1248`, referenced at `0x136B78B`), `ProgressionPackages` (`0x22C1180`), `items` (`0x22C1120`), `levelspec` (`0x22C1280`), `Items/Customize/v2` (`0x22C0D28`), and `"%s/Mission/RewardEntries"` (`0x22C1078`). The exact source-level function name is not established.

**Inference:** its final state refreshes the local player's history after profile/progression loading, using the profile result without any additions on this path.

## Count conclusion

The safety bound is upstream, not in either caller. Native `Profile_LoadLoadout` reads the record count at `0x17521B8`, appends and increments it at `0x17521DB..0x17521FA`, then stops at 16 with `cmp r8d,10h; jae 0x1752223` at `0x1752207..0x175220B`. The current mod's [ProfileLoad](../more-stratagem-slots/src/stratagem_slots.c#L1996) calls that loader and only extends its result to `n`, where its scan condition is `n < g_slots`; its count write is at source line 2033. Consequently, at `g_slots <= 16`, both the native result and any extension are at most 16.

Combining that bound with the observed copies and absence of subsequent additions answers **no for both callers** at `slots <= 16`. At 17–20, either copy can carry the extended profile result above 16, so this evidence supports retaining the history hook for those settings. This is a static path conclusion under the handoff's record/loader assumptions and current mod behavior, not a claim about arbitrary externally corrupted records or a live-game test.
