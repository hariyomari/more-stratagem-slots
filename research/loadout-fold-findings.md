# The loadout card's fold strip

Findings behind 0.3.0's fold strip (`more-stratagem-slots/src/stratagem_slots.c`, "the fold strip"). Game build
25480438; the addresses are RVAs in game.dll (the 2026-11-13 dump's base is 0x7FFCF6030000). Read from the dump on
2026-11-23, with the mod's session logs where it says so. The strip's first build (a filled bar with "+12" and
"-12") was run on the 0.5 nightly that day; what that session showed, and what is still to be seen, is at the end.
The outline, the "SHOW [+12]" text and the `collapse` setting came after it and passed a second test that day.
The rows' opening and closing over a moment, and the `collapse_animate` setting, came after that and ran in two more
sessions, on the nightly and on HellForge 0.4.0-beta ("The last sessions", at the end).

## The problem

Each row of extra slots makes the card's panel 85 px taller, and the equipment row is anchored to the panel's top, so
it and everything by it goes up with every row. Another mod, Dedicated Melee Slot, draws a pick box by the card's
EQUIPMENT button; with 16 slots and more the button is so high that the box leaves the screen (below). The fix:
start with the rows hidden behind a strip under the game's four, which shows them and hides them again.

## Dedicated Melee Slot 1.6.3 (vibeslacker), checked for conflicts

Read from its DLL (`melee_weapon_slot.dll`, MinGW, SDK 13) and its data patch.

- One hook: the weapon spawn function at 0xFD9710 (the DLL's only call through `hf->hook`, HfApi +88; no call
  through `write_u32_if` at +80 was found). It checks the bytes of two more that it calls and does not hook:
  0x12FE350 (packages) and 0x8BF870 (attach). None of the three is a function this mod hooks or checks, and it checks
  none of this mod's, so neither stops the other from starting (DiverKit does: it checks loadout screen code this
  mod hooks).
- It calls `hf->write` (HfApi +72) in three places, all read, each for 4 bytes of game data: two in the attach
  (0x180003AF0 in the DLL: a field of the melee's 48-byte record, found through the pointer at game.dll + 0x33271C0,
  and the "player word" it puts back after the game's attach) and one in its game tick (0x180002120: the diver's
  primary or secondary weapon field, for a swap). No code is written, and none of it is data this mod reads or
  writes (the loadout record, the player history, the HUD list, the in-hand records). Its `VirtualProtect` calls
  are the MinGW runtime's pseudo-relocation code; `VirtualAlloc` is in the spawn detour.
  (A first pass on 2026-11-23 looked for `write` at the wrong slot and reported "no memory writes"; that was wrong.)
- Keys: it reads the weapon slot keys (1, 2 and 4 by default, from `input_settings.config`) with `GetAsyncKeyState`
  and sends key presses with `SendInput`. This mod reads Q/E and the wheel, and only while the stratagem list is open.
- `data/9ba626afa44a3aa3.patch_0` is a Lua script for Bingus Shared Loader ("Dedicated Melee Slot picker 1.2.0"). It
  draws the PRI/SEC pick box with the game's own Gui and writes the two pick files; it changes no game data.
  - It finds the local card as DiverKit's screen check does: screen + 0x53A78 + i * 0x1EE18, the card whose
    `+0x1EDF0` points at the local payload. That is the place the cards have without a mod that moves them.
  - The box is placed from the widget at card + 0x1B820, which it calls the EQUIPMENT button: 82 card units above
    the button's bottom edge (`box_y`), the card being 420 units wide. So the box moves up with the panel.
  - It hides the box when the button is closer to the screen's top than 146 card units
    (`gh - (card.y + card.h) < 146 * s`), the guard SAPP and DiverKit have for the screen sliding in. This is what
    hides it with many rows.
  - "The Equipment page is showing when the button sits at or above this screen height" (`equip_y`, 370): a taller
    panel can trip this too; it only moves the box by 4 units.
  - The box and its list can be dragged, and the spot is saved in `Logs/MeleePicker.cfg`.
- Tested together on 2026-11-23: no conflict. The box going out of sight with 16 slots and more
  was the one thing found.

## What the card is made of

`LoadoutCard_BuildPanel` (0x1893F30) and `SlotWidget_Init` (0x1892DB0) show how the game makes each kind of node.
`UiNode_Init(node, kind)` is 0x1446840; the kind is in bits 18-21 of the node's flags (node + 0).

- Kind 1 is a plain box (272 bytes): the panel itself, the equipment row's container (P + 272, 480 x 103, pivot and
  anchor (0, 1): the panel's top left), the booster box (P + 35576, 80 x 80).
- Kind 5 is a filled rectangle (280 bytes): init, then the u32 at node + 272 set to -1, then size, alpha, colour and
  layer. The slot widget's background is one: 70 x 70 at (5, 5) in the widget's 80 x 80, alpha 1.0, colour 0.141,
  layer 12. So a slot draws 70 x 70 and the gap seen between two slots is 15. The header bar's black background
  (P + 816) is another, sized to its parent with `0x14490B0(node, 6)` instead of a size.
- The header's text (P + 1440) is a text node of another kind than the mission list's (init 0x143AFA0, its string
  by id with 0x143BF90, font with 0x11F79B0(node, 4, 0, 0, 1.0, 0)), on layer 14 with colour 0.694. The strip uses
  the kind the page label already uses (kind 9, `UiText_Init` 0x143D390, text at node + 284), since that one takes a
  string of the mod's own.
- `UiText_SetFont` (0x11F7340): `(node, style, variant, size class, scale, flag)`. With scale 1.0 and no flag the
  font is looked up in the node's UI context (context + 8256 + 8 * (font + 48 * size class)); any other scale takes
  a longer path that makes a material. The label's call, `(3, 1, 1, 1.0, 0)`, is the one known to work, so the strip
  makes the same one. **How large that font is on the loadout screen is not known.**
- A slot widget's frame (widget + 1944) is 0.4 grey, white when hovered, and (1.0, 0.914, 0) when focused
  (`SlotWidget_ApplyState` 0x18932F0). The strip's frame and text take the same three colours.
- That frame is a widget of its own, 2456 bytes, made by `0x17909F0(frame, layer)`; the slot's call gives layer 15,
  then sets the widget's size on it (80 x 80: the frame is 5 px outside the 70 x 70 background) and its colour, and
  adds it as a child. Eight other functions make one the same way.
  - It is a kind 1 box with six image nodes of 344 bytes (frame + 272, 616, 960, 1304, 1648, 1992; init 0x143EAB0,
    material with 0x144FAF0), all of one material that the init looks up itself (resource 0x7D4E055E2F6F38DD in the
    UI's current set, kept at frame + 2448).
  - frame + 2432 is how thick the lines are (3.0 after init) and frame + 2436 the gap in the middle of the left and
    the right side (16.0). `SlotWidget_ApplyState` writes both, 2.0 for a plain slot and 3.0 for a hovered or
    focused one, always with a gap of 16.0, and then calls the frame's layout, 0x1791380.
  - The layout (0x1791380): the top and bottom lines are (width - 2 x thickness) x thickness, at x = thickness (the
    top one has pivot and anchor (0, 1)); the four side pieces are thickness x (height - gap) / 2, one in each
    corner. So a slot's outline has a 16 px opening in the middle of each side, and a frame lower than the gap
    would get side pieces of a negative height.
  - The box has a callback (frame + 256 the frame, frame + 264 the function 0x17909D0) that runs the layout and then
    0x1790DC0, which gives each image a part of the material's texture picked at random (the game's random number
    at 0x3483C38), once (frame + 2440).
  - The frame's alpha is 1.0 on a slot that is not greyed out; its colour set on the box is what the images show.
- Pivot is node + 44 and anchor node + 60, both (0, 0) after init: the bottom left, y upwards.
- After each `UiNode_AddChild` the game calls 0x144C2C0 on the parent. The mod never has (extra slots, page label),
  and the strip does not either.

## The cursor and the select button

- `LoadoutCard_Hover` (0x1894BC0) tests the booster box with `0x144D320(node, point, 0)` directly; a slot widget's
  own test (0x1893550) goes through `0x178F970(cursor, node, 0)`, which ends in the same function. So `cursor_over`
  works on any sized node, a plain box included. The widget's test returns "not hovered" while the node's flag 4 is
  set (set when a node or its child changes, until its layout); the strip's test does the same.
- With the virtual cursor, or the cursor off, the game only draws hover and sets no hover index: a press then acts
  on the focus, not on what the cursor is over.
- `LoadoutCard_HandleNav` (0x18950B0) returns: 0 nothing, 1 open the picker on the focus (controller), 2 open it
  on what was clicked, 3 moved, 4 and 5 left the card to the left and right. Its callers (`LoadoutScreen_HandleInput`
  0x146D9B0 and 0x18CBFC0) open the picker for 1 and 2, go to the next card for 4 and 5, pass a 0 on to the card's
  other buttons, and for 3 only report the input as handled. So 3 is what a press on the strip returns.
- The select button (action 0xA00000000) is read whether or not navigation is on, and only on the card with
  `PANEL_EXPANDED` (P + 55672: the local, editable card): the mouse's goes to the hover index when it is under 5,
  the controller's to the focus when it is under 5. A click with the mouse over nothing of the game's returns 0.
- Directions (`0x178E020`): 1 is up (to the equipment row), 2 down, 3 left, 4 right. Down from a stratagem slot
  does nothing in the game.

## When the cards are built

- `LoadoutCard_BuildPanel` has one caller, `LoadoutCardOuter_Build` (0x1899430), whose caller 0x18CAE00 is called
  from the screen's build (0x1466170, the function that holds the cards' `lea` at 0x1466AE2).
- The panel's shown loadout (P + 55648) is written in two places only: zeroed in BuildPanel (0x1893FA3) and set in
  the refresh (0x1895A8D). So "shown for the first time" is always "since a build".
- From the mod's logs: every session with one visit to the loadout screen has four `panel build` lines, at one
  moment. The one session with two visits (HellForge release, 2026-11, 20 slots) has eight: four at 18:19:54 and
  four at 18:30:42, at other addresses. So each visit builds its cards anew, and a build is where the fold starts
  again.
- The card's update (0x18999F0) calls the hover function every frame for a card in the loadout view, so a hook on
  it sees every live panel soon after the fold changes.

## What 0.3.0 does with this

- `per_row` is gone: rows are of four. An old file's line is not read, and the log says so.
- `collapse` (on by default) and `collapse_rows` (3 by default, 2 to 5): with `collapse` on, a loadout of that many
  rows or more gets the strip, so 8 slots have none and 9 to 20 have one. (The first build had no `collapse`; 0 rows
  was "never" there.)
- The strip is a box of 335 x 30, a child of the panel at (30, 30), in the widgets' allocation: a rectangle of
  325 x 20 at (5, 5) (from the first slot's drawn left edge to the fourth's right), a text, and a frame of the
  game's as large as the box, as a slot widget has them. The frame's lines are 2 thick and 0.4 grey, 3 and white or
  yellow with the cursor or the focus on the strip; its side gap is 6 (a slot's 16 in 80, for 30). The frame has its
  own code check (0x17909F0 and 0x1791380): without it the strip has no outline. With a strip every row is 35
  higher. (The first build's strip was the rectangle alone, at (35, 35), and the rows 30 higher.)
- Folded, the rows under the game's four are past the card's right edge (as the picker's layout puts the rows it
  does not show) and the panel is 140 + 35 high (+ 113 expanded). The text is "SHOW [+N]" folded and "HIDE [-N]"
  unfolded, N the slots under the game's four. (The first build's was "+N" and "-N".)
- One fold for every card; `BuildPanel` folds. Each panel lays itself out in the refresh and hover hooks, which are
  given a live panel.
- Mouse: a click on the strip of the local card. Controller: down from a column's last shown slot gives the strip
  the focus (the game's focus parked on its slot 4, as for an extra slot), select toggles, up goes back into the
  column, left and right leave the card.
- The picker's move on to an empty extra slot unfolds, and the card stays unfolded when the picker closes.

## Animating the fold (looked at on 2026-11-23; built the second way)

The rows can open and close over a moment instead of at once, in two ways.

### The game's own tweens

- Every node can have a tween on its position, size, colour and alpha. The tweens are in one table of 2048 entries
  of 112 bytes (the pointer at 0x347CD88: count and first free index, then the entries from + 16). A node's flags
  say which of its properties has one (0x400000 position, 0x800000 size, 0x4000000 colour, 0x8000000 alpha), and
  node + 168 and + 176 hold the entries' indices, 11 bits each.
- Property numbers: 0 position (node + 4), 1 size (node + 12), 4 colour (node + 72), 5 alpha (node + 68); 2 and 6
  are node + 28 and node + 164, not looked at.
- To start one: position 0x1447440, size 0x1446DD0, colour 0x14483F0, alpha 0x1448870, all
  `(node, target, seconds, kind, repeats, smooth, delay, curve*, curve_arg)`. Kind 0 runs once; 1 goes back and
  forth; 2 to 6 are random flickers; a curve of four floats that are not all zero makes it kind 7. `smooth` 1 is
  smoothstep, 0 linear. The game's calls: alpha `(node, 1.0 or 0.0, 0.2, 0, 0, 0, 0, &zeros, 0.0)` (0x1029000),
  size `(node, size, 0.2, ?, 0, 1, 0, &zeros, 0.0)` (0x1294130).
- There are wrappers that set at once when the time is 0 (0x144EAE0 size, 0x144EB50 position, 0x144EE10 colour),
  and others with fewer arguments (0x1451BB0 size: `(node, from, to, smooth, seconds, delay, curve*)`).
- The plain setters (set size, position, alpha, colour) end the property's tween.
- The loadout card uses one itself: its update (0x18999F0) sizes the card's box and its background with 0x1451BB0
  over 0.2 s, smooth, when the card changes between the stratagem panel and the equipment page, and does nothing
  more until that tween has ended. So 0.2 s and smoothstep are the card's own pace.
- The tick (0x12ECD50, every frame) goes through the table: it advances each tween (0x14397A0), writes the value
  into the node, and marks the node and its parents (node + 224, node + 240) as changed.
- So a tween holds its node, and reaches the node's parents, until it ends. The game's nodes are safe with that as
  the game's own tweens are. The mod's (the extra slots, the strip) are in the mod's memory and are never torn down
  by the game: if the loadout screen closes within a tween on one of them, the tick may walk from the mod's node to
  a panel that is gone. How the game ends a node's tweens when a screen closes was not looked at.

### From the mod, in the hover hook

- The card's update calls the hover function every frame while the card shows its stratagem panel, and the hook is
  given a live panel. So the mod can lay the panel out every frame for a moment after the fold changes: the panel's
  height and the places of the game's four, the booster box and the rows, at a part of the way that it works out
  from the time passed (QueryPerformanceCounter; smoothstep is one line).
- No new game function. `PlaceSlots` already works out every place from the number of rows shown; it would take a
  part of a row too.
- The rows have nothing that cuts them off (the rows not shown are only moved far to the right), so a row cannot
  slide out from behind the strip. It can stay at its place and fade in once the game's row has gone past it: alpha
  on the widget's box. `SlotWidget_ApplyState` sets that alpha itself (1.0, or 0.5 for a slot that is greyed out) at
  every change of state, so the mod sets it again each frame and calls `apply_state` when the fold has ended.
- Each change of size or position marks the node as waiting for its layout, and such a node is not tested for the
  cursor: the slots do not take hover for the moment the fold moves.
- Dedicated Melee Slot's box is placed from the EQUIPMENT button every frame, so it moves with the card.
- Not known: that the card's frame follows the panel's height every frame (it follows a change at once, in the
  first session's screenshots), and how a faded slot looks (its images and its frame taking the box's alpha).

### Which

From the mod: about as much code as the strip's outline took, in one place, and nothing that outlives the hook.
The game's tweens would be less code for each frame, but need three more functions of nine arguments checked and
called, and an answer to what happens to a tween on a node of the mod's when the screen closes.

### What was built

- From the mod. Each panel has how far its rows are open, 0 to 1 (`fold_pos`). `SyncFold`, at the end of the hover
  hook (and in the refresh hook), moves it towards the fold by the time since its last step (0.15 s for the whole
  way, `FOLD_SECONDS`; 0.1 s at first) and lays the panel out. The first call after the fold changed only starts
  the clock.
- The layout takes the eased value (smoothstep): the panel's height, the game's four and the booster box are that
  part of the way up. A row under them is at its place from when the game's four have passed half of it, with its
  alpha going from 0 to 1 until they have passed all of it; before that it is far to the right, as when folded.
- The alpha is the widget's box's, times 0.5 for a locked slot as `SlotWidget_ApplyState` has it (state bit 4), and
  only set on a panel with a strip (the alpha function is the label's code, which the strip needs anyway).
- No move while the picker has the card (the rows are at the fold at once) and none at a panel's build; none at all
  with `collapse_animate = off` (on by default).
- The fold itself (the strip's text, where the focus can go) changes at the press, as before; only the layout
  takes the time.

## The first session (2026-11-23, 0.5 nightly, 16 slots, with Dedicated Melee Slot)

The first build; session `b079617999e64f4291432bbe107757d9`, and two screenshots.

- The four panels each got their strip at the build, folded (`fold strip linked, folded`, four lines at one moment).
- The text shows as soon as the screen opens and fits the 20 px bar: "+12" and "-12" are about 12 px high. So the
  label's font call works in the loadout screen's UI context.
- A click unfolds and folds: at least 36 times in the session (the log's lines for the fold stop at their limit of
  40, four of them the build's). The card's frame follows the panel's height at once: the
  screenshots show the card's header, the equipment row and the READY UP button in their places both ways.
- Controller and keyboard: the strip takes the focus and the select button toggles it (seen in the session, not in
  the log).
- The picker's move on: `picker moves on to extra slot 5` while folded, and the next press on the strip logged
  `folded`, so the move on had unfolded the card.
- Dedicated Melee Slot's box is in sight while folded, and with 16 slots unfolded too (it is under the card's
  header then).
- The filled bar with no outline is easy to miss against the card: hence the frame.

## The last sessions (2026-11-23 and 24, 20 slots, the code as released in 0.3.0)

- 0.5 nightly, session `e6e52f8e41b44ae18b782f2f10d3d347`, 13 mods, Complex Operations among them (eight cards):
  - `fold strip on`; all eight panels got their extra slots and their strip, folded.
  - The eight were built a second time 66 s later, at the same addresses, and were folded again: a build folds,
    whatever the fold was.
  - 21 presses on the strip, the quickest 0.1 s apart (inside the 0.15 s the rows take): no line of the log is out
    of the ordinary after them, and the picker's move on and the commits went on as before.
  - A full difficulty 10 mission after it; the game exited with code 0 and no problem was seen.
- HellForge 0.4.0-beta, session `5ca5e32d89b0447f90162fda18c55c1b`, six mods:
  - `fold strip on`; four panels with their strips; the picker's move on through the hidden rows; a difficulty 11
    mission (the list's parking ran). An old settings file's `per_row` and `page_hold` were named in the log and
    not read.
  - The session ended in a crash of the game's, not the mod's: an access violation at helldivers2.exe + 0x626296
    on a thread of the graphics driver (D3D12Core and nvwgf2umx on its stack), 2.7 s after the game opened its menu
    in the mission. Three sessions of 2026-11-16 and 17 on this HellForge, with 0.1.1, 0.1.4 and 0.2.0, ended at
    the same address with the same registers, two of them after the same menu.

## Still to be seen

1. Another player's card (co-op): its strip follows the local one and takes no input.
2. 8 and 9 slots; `collapse = off`; `collapse_rows`; `collapse_animate = off`.
3. Dedicated Melee Slot's box moving with the card (it was off in the last sessions).
