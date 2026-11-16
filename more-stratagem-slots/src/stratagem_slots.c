/* More Stratagem Slots: more than four stratagem slots in the HELLDIVERS 2 loadout.
 *
 * Loadout: every loadout card shows slots 5..N beside (or under) the game's four, filled from the loadout record, and
 * the loadout screen's commit keeps them in the record with the uses the game itself computes. So a loadout of more
 * than four shows, saves and goes into the mission.
 * Picking: the extra slots take focus (controller/keyboard: right from the booster box; mouse: hover and click), the
 * picker opens on them and writes into them, and after a pick the picker moves on to an empty extra slot once the
 * game's four are full. The game's own focus index never leaves the values it knows (0-11): an extra slot's focus is
 * this mod's, with the game's focus parked on slot 4 (index 3) so the picker opens in stratagem mode, and the picker's
 * target slot (screen + 10268) is then set to 4 + k.
 * Pages: the game's in-mission list holds 16 in all (its lists of 16, research/audit-findings.md). Past that, the
 * picks take turns in it a page at a time (Q/E while the list is open); the rest wait in this mod with their uses and
 * cooldowns. See "pages".
 *
 * game.dll 25480438; research and RVAs in research/README.md ("The loadout card"):
 *   panel P = card + 23416. P + 36544: the 4 slot widgets (0x12A8 each, type at +0x128C). The panel's fields continue
 *   right after them, so the extra widgets live in memory this mod allocates (never freed: the widget tree points at
 *   them), as children of P like the game's own.
 *   LoadoutCard_BuildPanel(P) 0x1893F30 builds the panel; LoadoutCard_RefreshFromLoadout(P, loadout, expanded, force)
 *   0x1895A20 fills the four from the record; LoadoutScreen_CommitSlotsToLoadout(P, loadout) 0x18966A0 clears the
 *   record's slots and writes the four back, each with its uses (base + ship upgrades). */
#include <string.h>
#include <windows.h>
#include "hellforge_mod.h"

/* ---- the game -------------------------------------------------------------------------------------------------- */

#define RVA_REFRESH 0x1895A20u
#define RVA_BUILD_PANEL 0x1893F30u
#define RVA_COMMIT 0x18966A0u
#define RVA_WIDGET_INIT 0x1892DB0u
#define RVA_WIDGET_SET_TYPE 0x1893600u
#define RVA_NODE_SET_POSITION 0x14476A0u
#define RVA_NODE_SET_PIVOT 0x144F160u
#define RVA_NODE_SET_ANCHOR 0x144F0D0u
#define RVA_NODE_ADD_CHILD 0x144C5C0u
#define RVA_NODE_SET_SIZE 0x1447160u
#define RVA_SPECIAL_MODE 0x11DF110u /* the refresh skips types 77 and 90 while this is true */
#define RVA_STRATAGEM_INFO 0x37CB600u /* StratagemInfo* [type] */
/* focus and picking */
#define RVA_HOVER 0x1894BC0u          /* (P, _, cursor off): per frame, which widget the cursor is over -> P + 55656 */
#define RVA_NAV 0x18950B0u            /* (P, input, _, nav on): directions and the click; 2/1 open the picker */
#define RVA_SET_FOCUS 0x1895770u      /* (P, index) */
#define RVA_SET_SLOT 0x189D050u       /* (card, slot, stable id): ignores slot >= 4 */
#define RVA_OPEN_PICKER 0x146E9D0u    /* (screen): mode screen + 10264, target slot screen + 10268 = the focus */
#define RVA_CLOSE_PICKER 0x146F3B0u   /* (screen, _) */
#define RVA_PICKER_INPUT 0x146E0A0u   /* (screen, input): a pick in stratagem mode sets the slot, commits, advances */
#define RVA_STABLE_TO_TYPE 0x11F2490u
#define RVA_PLAY_SOUND 0x1327F50u     /* (_, sound id) */
#define RVA_INPUT_INDEX 0x585B80u     /* (action) -> index into the input state's 32-byte entries at +808 */
#define RVA_NAV_DIR 0x178E020u        /* (input) -> 1 to the equipment row, 2 back, 3 left, 4 right */
#define RVA_CURSOR_OVER 0x178F970u    /* (cursor, node, 0) */
#define RVA_APPLY_STATE 0x18932F0u    /* (widget): redraws from its state bits */
#define RVA_IS_HOVERED 0x1893550u     /* (widget): updates the hover bit from the cursor, returns it */
#define RVA_G_INPUT_MODE 0x347CF18u   /* pointer; +924372 the mouse is in use, +924373 the virtual cursor */
#define RVA_G_CURSOR 0x347CE38u       /* pointer; +1520 the cursor */
/* The game's limit of 16 */
#define RVA_AUGMENT 0xB4EEC0u         /* (record): the game's systems append their entries after the picks (linked
                                         types like Eagle Rearm, mission stratagems...), unchecked */
#define RVA_PROFILE_LOAD 0x17514C0u   /* (record) -> record: fills it from the profile at boot, but only up to 4 slots'
                                       * cost (StratagemInfo+0xBC summed, 0x17521CD) and 16 entries */
#define RVA_PROFILE_KEEP_A 0x11F507Bu   /* the return addresses of the load's two callers that keep the loadout; its */
#define RVA_PROFILE_KEEP_B 0x1754122u   /* other three (0x10853B3, 0x1088CD0, 0x136BCEA) feed the player history */
#define RVA_PROFILE_LOAD_PROFILE 0x17515E6u /* mov r14, [game + RVA_G_PROFILE]; cmp [r14 + 0x3C], dil */
#define RVA_G_PROFILE 0x347CDD8u      /* pointer: the profile; +0x3C a loadout is saved, +0x4C 32 {stable id, uses} */
#define RVA_BROADCAST_COMMIT 0x189D24Du /* LobbyCard_CommitAndBroadcast: lea rcx, [rbx + 0x5B78]; call commit (a local copy
                                         * it then packs into lists of 16, unchecked); returns to RVA_BROADCAST_RETURN */
#define RVA_BROADCAST_RETURN 0x189D259u
#define RVA_LOAD_ASSETS 0x1364C00u    /* PlayerHistory_LoadLoadout(entry, stage, ack, peer, _): requests the packages of
                                       * the entry's list by type, keeps a copy at +0xA28 that the release walks */
#define RVA_ACTIVATE 0x66E6D0u        /* StratagemInput_ActivateMatch(_, peer, {type, slot}, where, _): uses that slot */
#define RVA_RESET 0x66EFD0u           /* PlayerHistory_ResetStratagems(_, peer u32, preset, _): the list's count to 0 */
#define RVA_LIST_OPEN 0x1835630u      /* HudStratagemList_SetOpen(hud, open): the in-mission list (held stratagem key) */
#define RVA_G_HISTORY 0x347CE50u      /* pointer: PlayerHistory, entries of 5776 bytes, their count at +184832 */
#define RVA_LOCAL_PEER_READ 0x189D231u /* mov rax, [game + RVA_G_NETWORK]; ...; mov rcx, [rax + 0xB398] */
#define RVA_G_NETWORK 0x347CEF0u      /* pointer; +0xB398 u64 the local player's id (PlayerHistory entries' +0) */
#define NETWORK_LOCAL_PEER 0xB398u
#define RVA_COPY_LOADOUT 0x1361F40u   /* PlayerHistory_CopyLoadout(_, out, peer) -> out: out[0] = found, out + 8 the
                                       * player's list as a record (the lobby takes the loadouts back after a mission) */
#define RVA_HISTORY 0x13620A0u        /* (_, peer, record, _): copies the record's entries into stack lists of 16,
                                         unchecked; more crash in 0x11E82D0 (ready-up, mission start) */
/* The page label: a text node of the game's own kind under the in-mission list's first row, set up the way the row
 * sets up its name (HudStratagemRow_Init 0x18358D0, at row + 8232) and given text the way the row update does */
#define RVA_LIST_UPDATE 0x1834A10u    /* HudStratagemList_Update(list, float, u32, float): every frame */
#define RVA_TEXT_INIT 0x143D390u      /* (node): a text node (kind 9) */
#define RVA_TEXT_FONT 0x11F7340u      /* (node, style, 1, 1, scale, 0): the list's names are style 3 at 1.0 */
#define RVA_NODE_LAYOUT 0x144E1A0u    /* (node): a text node measures its text (its size, for the pivot) */
#define RVA_TEXT_CHANGED 0x143DE70u   /* (node): its text changed */
#define RVA_TEXT_RELEASE 0x12ED700u   /* (_, cache): frees a text node's cache (node + 272), on a change of text */
#define RVA_NODE_SET_LAYER 0x14491F0u /* (node, u16): the names are 553 */
#define RVA_NODE_SET_ALPHA 0x1448AD0u /* (node, float) */
#define RVA_NODE_SET_COLOR 0x1448690u /* (node, float rgb[3]) */
#define RVA_NODE_SET_VISIBLE 0x14508E0u /* (node, u8): flag 0x10 */

/* Eagles (the game's rule): type 49 is Eagle Rearm, and an Eagle is a type whose linked type (info + 200) is 49. A
 * cooldown starting on an Eagle gives every other Eagle in the list the same end; one starting on Eagle Rearm also
 * refills their uses. */
#define RVA_COOLDOWN 0x135C2C0u       /* PlayerHistory_StartStratagemCooldown(history, peer, slot, set start, seconds) */
#define RVA_COOLDOWN_TABLE 0x135C312u /* lea r13, [the stratagem info table]: HellForge moves the table for stratagem
                                       * mods and patches this lea, so the table is read from it */
#define RVA_COOLDOWN_TIME 0x135C351u  /* mov rax, [game + RVA_G_CLOCK]; mov rcx, [rax + 0x18] */
#define RVA_G_CLOCK 0x3326348u        /* pointer; +0x18 u64 the game's clock in microseconds */
#define RVA_COOLDOWN_SYSTEMS 0xB9A6D0u /* PlayerHistory_SetStratagemCooldown(peer, slot, set start, seconds): the same
                                       * for every PlayerHistory system (a network message, type 28's) */
#define RVA_EAGLES_SPENT 0x66E580u    /* PlayerHistory_AllEagleUsesExhausted(_, player u32) -> 1: no Eagle in the list
                                       * has uses left; an Eagle's use then starts Eagle Rearm by itself */
#define RVA_EAGLES_USED 0x66D650u     /* Loadout_HasSpentEagleUses(_, list) -> 1: an Eagle in the list is below its
                                       * full uses */
#define RVA_ARMED_QUERY 0x69FC60u     /* (list, peer, row) -> 1: a record {+16 peer, +24 row, +1 0} of the system at
                                       * [game + RVA_G_ARMED] names that row; the HUD row then shows state 2. The row
                                       * is a list index (the stratagem in hand, and perhaps thrown), not a type */
#define RVA_G_ARMED 0x3326D98u        /* pointer; +28 u32 count, +96 records of 40 bytes */

static const struct { uint32_t rva; const char* pattern; const char* name; } kCode[] = {
    { RVA_REFRESH, "40 55 56 41 54 41 55 48 8D AC 24 78 F0 FF FF B8 88 10 00 00 E8 ?? ?? ?? ?? 48 2B E0", "refresh" },
    { RVA_BUILD_PANEL, "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC B0", "build panel" },
    { RVA_COMMIT, "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 50 0A 00 00 48 8B 05 ?? ?? ?? ?? 48 33", "commit" },
    { RVA_WIDGET_INIT, "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55 41 56 41 57 48 8B EC 48", "slot widget init" },
    { RVA_WIDGET_SET_TYPE, "48 89 5C 24 08 57 48 83 EC 20 8B FA 48 8D 99 80 03 00 00 89 B9 8C 12 00", "slot widget set type" },
    { RVA_NODE_SET_POSITION, "48 89 5C 24 18 48 89 6C 24 20 48 89 54 24 10 56 57 41 57 48 83 EC 20 F3", "node position" },
    { RVA_NODE_SET_PIVOT, "48 83 EC 28 F3 0F 10 41 2C 4C 8B C9 48 89 54 24 30 0F 2E 44 24 30 7A 10", "node pivot" },
    { RVA_NODE_SET_ANCHOR, "48 83 EC 28 F3 0F 10 41 3C 4C 8B C9 48 89 54 24 30 0F 2E 44 24 30 7A 10", "node anchor" },
    { RVA_NODE_ADD_CHILD, "40 53 48 83 EC 20 4C 8B CA 48 8B D9 48 8B 91 E0 00 00 00 48 85 D2 74 54", "node add child" },
    { RVA_NODE_SET_SIZE, "48 89 5C 24 18 48 89 6C 24 20 48 89 54 24 10 56 57 41 57 48 83 EC 20 F3", "node size" },
    { RVA_SPECIAL_MODE, "40 53 48 81 EC 50 08 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4", "special mode" },
    { RVA_HOVER, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 41 0F B6 E8 48 8D 99 F8", "hover" },
    { RVA_NAV, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B EA 48 8B F9 45 84", "navigation" },
    { RVA_SET_FOCUS, "48 89 5C 24 08 57 48 83 EC 20 8B 81 6C D9 00 00 48 8B F9 8B DA 3B C2 0F 84", "set focus" },
    { RVA_SET_SLOT, "83 FA 04 0F 83 BC 00 00 00 48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 41 8B D8 8B", "set slot" },
    { RVA_OPEN_PICKER, "40 57 48 81 EC A0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 70 80 B9 08 28", "open picker" },
    { RVA_CLOSE_PICKER, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 0F B6", "close picker" },
    { RVA_PICKER_INPUT, "48 89 5C 24 10 48 89 6C 24 18 56 57 41 55 41 56 41 57 48 83 EC 50 48 8B F9 48 8B EA", "picker input" },
    { RVA_STABLE_TO_TYPE, "85 C9 74 65 BA 01 00 00 00 4C 8D 0D ?? ?? ?? ?? 44 8B C2 4D 8B D1", "stable id to type" },
    { RVA_PLAY_SOUND, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 8B DA", "play sound" },
    { RVA_INPUT_INDEX, "83 F9 0C 77 1D 4C 8D 05 ?? ?? ?? ?? 8B C1 41 8B 94 80", "input index" },
    { RVA_NAV_DIR, "40 53 48 83 EC 20 48 8B D9 48 B9 00 00 00 00 04 00 00 00 E8", "navigation direction" },
    { RVA_CURSOR_OVER, "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 4C 8B 0D ?? ?? ?? ?? 48 8B", "cursor over" },
    { RVA_APPLY_STATE, "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 50 48 8B 05 ?? ?? ?? ?? 48 33", "slot widget state" },
    { RVA_IS_HOVERED, "40 53 48 83 EC 20 48 8B D9 0F B7 89 88 12 00 00 0F B6 C1 C0 E8 02 A8 01", "slot widget hovered" },
    { RVA_AUGMENT, "48 89 5C 24 18 55 56 41 56 48 81 EC 60 06 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4", "loadout additions" },
    { RVA_HISTORY, "48 8B C4 48 89 58 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 D8 F5 FF FF 48 81 EC 80 0B 00 00",
      "player history" },
    { RVA_PROFILE_LOAD, "48 89 5C 24 18 55 57 41 56 48 8D AC 24 C0 EB FF FF B8 40 15 00 00 E8", "profile loadout load" },
    { RVA_PROFILE_LOAD_PROFILE, "4C 8B 35 EB B7 D2 01 41 38 7E 3C", "profile loadout load's profile" },
    { RVA_BROADCAST_COMMIT, "48 8D 8B 78 5B 00 00 E8 47 94 FF FF 48 8B 93 F0 ED 01 00", "lobby broadcast's commit" },
    { RVA_LOAD_ASSETS, "40 55 56 41 57 48 83 EC 30 48 8B F1 4C 89 4C 24 28 45 84 C0 4C 89 74 24 60 48 8D 0D",
      "player history load" },
    { RVA_ACTIVATE, "40 55 53 41 54 41 55 41 56 48 8D 6C 24 D1 48 81 EC D0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 "
      "89 45 FF 4C 8B 15 56 E7 E0 02", "stratagem activation" },
    { RVA_RESET, "48 89 5C 24 20 48 89 4C 24 08 55 56 57 41 54 41 55 48 83 EC 20 3B 15", "player history reset" },
    { RVA_LIST_OPEN, "48 8B C4 56 57 48 83 EC 68 0F B6 F2 48 8B F9 38 91 10 77 03 00 0F 84", "stratagem list open" },
    { RVA_LOCAL_PEER_READ, "48 8B 05 B8 FC BD 01 48 8D 95 F0 02 00 00 48 8B 88 98 B3 00 00", "local player id" },
    { RVA_COPY_LOADOUT, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 66 0F 6F 05 ?? ?? "
      "?? ?? 48 8D 8A 50 01 00 00 48 8B 35 E0 AE 11 02", "player history copy" },
};
#define CODE_COUNT (sizeof kCode / sizeof kCode[0])

/* The page label's code: without it the pages still work, with no label. */
static const struct { uint32_t rva; const char* pattern; const char* name; } kLabelCode[] = {
    { RVA_LIST_UPDATE, "40 53 55 56 57 41 56 48 81 EC C0 00 00 00 44 3B 05 ?? ?? ?? ?? 41 8B D8", "stratagem list update" },
    { RVA_TEXT_INIT, "48 89 5C 24 08 57 48 83 EC 20 BA 09 00 00 00 48 8B D9 E8 ?? ?? ?? ?? 33 FF C7 83 18 01 00 00 FF FF "
      "FF FF", "text node init" },
    { RVA_TEXT_FONT, "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 30 8B F2 4C 8D 35", "text font" },
    { RVA_NODE_LAYOUT, "40 53 48 83 EC 70 48 8B D9 8B 09 8B C1 C1 E8 12 83 E0 0F 83 F8 0B", "node layout" },
    { RVA_TEXT_CHANGED, "48 83 EC 28 8B 91 B8 00 00 00 4C 8B C9 8B C2 83 C8 02 89 81 B8 00 00 00", "text changed" },
    { RVA_TEXT_RELEASE, "4C 8B 0D ?? ?? ?? ?? 41 8B 89 10 80 03 00 85 C9 0F 84", "text cache release" },
    { RVA_NODE_SET_LAYER, "48 83 EC 28 4C 8B C9 66 39 91 BC 00 00 00 75 07 32 C0", "node layer" },
    { RVA_NODE_SET_ALPHA, "40 57 48 83 EC 20 F3 0F 10 41 44 48 8B F9 0F 2E C1", "node alpha" },
    { RVA_NODE_SET_COLOR, "48 89 5C 24 10 48 89 6C 24 18 56 57 41 57 48 83 EC 20 F3 0F 10 41 48 48 8B F2 0F 2E 02",
      "node colour" },
    { RVA_NODE_SET_VISIBLE, "48 83 EC 28 44 8B 01 4C 8B D1 41 8B C0 45 8B C8 83 E0 EF 41 83 C9 10 84 D2", "node visible" },
};
#define LABEL_CODE_COUNT (sizeof kLabelCode / sizeof kLabelCode[0])

/* The Eagles' code: without it the pages still work, and Eagle Rearm reaches only the page shown. */
static const struct { uint32_t rva; const char* pattern; const char* name; } kEagleCode[] = {
    { RVA_COOLDOWN, "40 53 41 54 41 57 48 83 EC 40 48 8B D9 45 8B E0 33 C9 4C 8B FA 44 8B 93 00 D2 02 00",
      "stratagem cooldown" },
    { RVA_COOLDOWN_TABLE, "4C 8D 2D ?? ?? ?? ?? 48 C1 E7 04", "stratagem cooldown's info table" },
    { RVA_COOLDOWN_TIME, "48 8B 05 F0 9F FC 01 48 8B 48 18 48 89 8C 1F D0 01 00 00", "stratagem cooldown's clock" },
    { RVA_COOLDOWN_SYSTEMS, "48 8B C4 44 88 40 18 89 50 10 56 41 54 41 55 48 81 EC C0 00 00 00 48 8B 35",
      "stratagem cooldown (systems)" },
    { RVA_EAGLES_SPENT, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 3B 15",
      "Eagle uses spent" },
    { RVA_EAGLES_USED, "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 20 33 DB 48 8B F2 39 9A 88 07 00 00",
      "Eagle uses below full" },
    { RVA_ARMED_QUERY, "48 8B 05 31 71 C8 02 45 33 C9 44 8B 50 1C 45 85 D2 74 2F 4C 8B 58 60", "stratagem in hand by row" },
};
#define EAGLE_CODE_COUNT (sizeof kEagleCode / sizeof kEagleCode[0])

/* The panel */
#define PANEL_SLOTS 36544u       /* the game's 4 slot widgets */
#define PANEL_SHOWN 55648u       /* u64: the loadout record the panel shows */
#define PANEL_EXPANDED 55672u    /* u8 */
#define PANEL_FOCUS 55660u       /* u32 focus index: 0-3 stratagems, 4 booster, 5-11 equipment, -1 none */
#define PANEL_PREV_FOCUS 55664u
#define FOCUS_BOOSTER 4u
#define FOCUS_EQUIPMENT 5u
#define PANEL_BOOSTER 35576u     /* the booster box node, at (-35, 30) */
#define BOOSTER_X -35.0f
#define NODE_SIZE_X 12u          /* floats: a node's size */
#define NODE_SIZE_Y 16u
#define PANEL_HEIGHT 140.0f      /* the panel's height as the game sets it, 113 more when expanded */
#define PANEL_HEIGHT_EXPANDED 113.0f
#define NODE_CONTEXT 248u        /* u64: the UI context a node takes when initialised */
#define WIDGET_SIZE 0x12A8u
#define WIDGET_STATE 0x1288u     /* u16 state bits */
#define STATE_HOVER 1u
#define STATE_FOCUS 2u
#define STATE_LOCKED 4u
#define WIDGET_TYPE 0x128Cu      /* u32 stratagem type */

/* The card and the loadout screen */
#define CARD_PANEL 23416u
#define SCREEN_PICKER_MODE 10264u   /* 10: stratagems */
#define SCREEN_PICKER_SLOT 10268u
#define SCREEN_PANEL0 366064u       /* the local card's panel: screen + 342648 + 23416 */
#define SCREEN_CARD0_MODE 455312u   /* 0: the loadout panel (1: another view) */
#define PICKER_STRATAGEMS 10u
#define MODE_MOUSE 924372u
#define MODE_VIRTUAL_CURSOR 924373u
#define CURSOR 1520u
#define INPUT_ENTRIES 808u
#define ACTION_SELECT 0xA00000000ull
#define NAV_TO_EQUIPMENT 1
#define NAV_LEFT 3
#define NAV_RIGHT 4
#define NAV_MOVED 3u             /* HandleNav's results */
#define NAV_OPEN_CLICK 2u
#define NAV_LEAVE_LEFT 4u
#define NAV_LEAVE_RIGHT 5u
#define NAV_DOWN 2
#define SOUND_MOVE 3716460101u
#define SOUND_SELECT 2736588357u
#define COMMIT_TYPES 0xA14Cu     /* where the commit reads the four types: PANEL_SLOTS + WIDGET_TYPE */
#define SLOT_PITCH 85.0f         /* 80 px widget + 5 px gap, as the game lays out its four */
#define SLOT_ORIGIN 30.0f

/* The loadout record */
#define LOADOUT_SLOTS 0x188u
#define LOADOUT_SLOT 0x30u
#define LOADOUT_COUNT 0x788u
#define LOADOUT_MAX 32u
#define LOADOUT_SIZE 2544u       /* the screen keeps its records 2544 bytes apart; the commit reads up to +0x9E8 */

/* The profile */
#define PROFILE_HAS_LOADOUT 0x3Cu /* u8: 0 means the game builds the default loadout instead */
#define PROFILE_PAIRS 0x4Cu       /* 32 {u32 stable id, u32 uses}; the game skips a pair whose uses are 0 */
#define PROFILE_PAIR_COUNT 32u

#define STRATAGEM_TYPES 0x96u
#define INFO_FLAGS 128u          /* the refresh shows a type only with bit 2 set here */

#define GAME_SLOTS 4u
/* The game holds the in-mission list in lists of 16, unchecked (research/audit-findings.md), and adds its own entries
 * to the picks (mission stratagems in front, linked ones like Eagle Rearm after). So the list never gets more than 16:
 * past that the picks go in pages that take turns in it (see "pages"). The record holds 32 in all, picks and the
 * game's additions, and nothing in the game checks that: 20 picks leave room for 12 additions (the mission group up to
 * 6, linked types, boosters' and the mission's). */
#define MAX_SLOTS 20u
#define MAX_EXTRA (MAX_SLOTS - GAME_SLOTS)
#define GAME_LIST_MAX 16u        /* entries the game's lists hold: picks plus its own additions */
#define LIST_SPARE 2u            /* rows a page leaves free for what the mission grants later (its appends clamp at 16) */
#define MIN_PAGE 4u              /* fewer picks a page than this: no pages, the guard trims */
#define MAX_PANELS 16u

/* PlayerHistory: one entry per player, the in-mission list at +56 (the record's layout) */
#define HISTORY_COUNT 184832u
#define HISTORY_STRIDE 5776u
#define HISTORY_LIST 56u
#define HISTORY_MAX 32u
#define HUD_LIST_OPEN 227088u    /* u8, what SetOpen sets */
#define HUD_ROW0 272u            /* the list's 16 rows (14176 bytes each); row 0 shows entry 0, at the top */

/* UI nodes (the label) */
#define NODE_FIRST_CHILD 224u    /* u64; the children are linked by NODE_NEXT, in address order */
#define NODE_NEXT 232u
#define NODE_PARENT 240u
#define TEXT_NODE_SIZE 1152u     /* the row's name node: row + 8232 to row + 9384 */
#define TEXT_CACHE 272u          /* u64 */
#define TEXT_BUFFER 284u         /* UTF-8, 805 bytes; the game writes at most 804 */
#define TEXT_MAX 804u
#define TEXT_STYLE_ROW 3u
#define LAYER_ROW_TEXT 553u

typedef void (*RefreshFn)(uint8_t* panel, uint8_t* loadout, uint8_t expanded, uint8_t force);
typedef uint32_t (*BuildPanelFn)(uint8_t* panel);
typedef void* (*CommitFn)(uint8_t* panel, uint8_t* loadout);
typedef uint64_t (*WidgetInitFn)(uint8_t* widget, uint64_t unused, uint8_t small);
typedef uint64_t (*WidgetSetTypeFn)(uint8_t* widget, uint32_t type);
typedef void (*NodeVec2Fn)(uint8_t* node, uint64_t xy);
typedef uint64_t (*NodeAddChildFn)(uint8_t* parent, uint8_t* child, uint64_t unused);
typedef uint8_t (*SpecialModeFn)(void);
typedef uint64_t (*HoverFn)(uint8_t* panel, uint64_t unused, uint8_t cursor_off);
typedef uint64_t (*NavFn)(uint8_t* panel, uint8_t* input, uint64_t unused, uint8_t nav_on);
typedef uint64_t (*SetFocusFn)(uint8_t* panel, uint32_t index);
typedef void (*SetSlotFn)(uint8_t* card, uint32_t slot, uint32_t stable);
typedef double (*OpenPickerFn)(uint8_t* screen, uint64_t unused);
typedef uint64_t (*ClosePickerFn)(uint8_t* screen, uint64_t unused);
typedef uint8_t (*PickerInputFn)(uint8_t* screen, uint64_t input);
typedef uint32_t (*StableToTypeFn)(uint32_t stable);
typedef void (*PlaySoundFn)(uint64_t unused, uint32_t sound);
typedef uint32_t (*InputIndexFn)(uint64_t action);
typedef int32_t (*NavDirFn)(uint8_t* input);
typedef uint8_t (*CursorOverFn)(uint8_t* cursor, uint8_t* node, uint64_t zero);
typedef void (*ApplyStateFn)(uint8_t* widget);
typedef uint8_t (*IsHoveredFn)(uint8_t* widget);
typedef void (*AugmentFn)(uint8_t* record);
typedef uint8_t* (*ProfileLoadFn)(uint8_t* record);
typedef void (*LoadAssetsFn)(uint8_t* entry, uint32_t stage, uint8_t ack, uint64_t by, uint32_t extra);
typedef uint64_t (*ActivateFn)(uint64_t unused, uint64_t peer, uint32_t* armed, void* where, uint8_t flag);
typedef uint64_t (*ResetFn)(uint64_t unused, uint32_t peer, uint8_t preset, uint32_t group);
typedef void (*ListOpenFn)(uint8_t* hud, uint8_t open);
typedef uint8_t* (*CopyLoadoutFn)(uint64_t unused, uint8_t* out, uint64_t peer);
typedef void (*ListUpdateFn)(uint8_t* list, float a, uint32_t b, float c);
typedef void (*NodeFn)(uint8_t* node);
typedef void (*TextFontFn)(uint8_t* node, uint32_t style, uint32_t a, uint32_t b, float scale, uint8_t flag);
typedef void (*TextReleaseFn)(uint64_t unused, uint64_t cache);
typedef uint8_t (*NodeSetLayerFn)(uint8_t* node, uint16_t layer);
typedef void (*NodeSetAlphaFn)(uint8_t* node, float alpha);
typedef void (*NodeSetColorFn)(uint8_t* node, const float* rgb);
typedef void (*NodeSetVisibleFn)(uint8_t* node, uint8_t visible);
typedef void (*CooldownFn)(uint8_t* history, uint64_t peer, uint32_t slot, uint8_t set_start, float seconds);
typedef void (*CooldownSystemsFn)(uint64_t peer, uint32_t slot, uint8_t set_start, float seconds);
typedef uint8_t (*EaglesSpentFn)(uint8_t* unused, uint32_t player);
typedef uint8_t (*EaglesUsedFn)(uint8_t* unused, uint8_t* list);

/* ---- state ------------------------------------------------------------------------------------------------------- */

typedef struct
{
    uint8_t* panel;
    uint8_t* widgets; /* extra_count widgets of WIDGET_SIZE */
    int built;        /* initialised and linked under the panel since its last build */
    int failed;       /* not linked: see BuildExtras */
    int focus;        /* the focused extra slot, or -1 */
    int hover;        /* the extra slot under the mouse, or -1 */
    int pick_row;     /* PlaceSlots: the row the picker shows, or -1 */
} Panel;

static const HfApi* hf;
static HfMod* self;
static uintptr_t game;
static uint32_t g_slots = 8, g_per_row = 4, g_extra = 4;

static RefreshFn g_refresh;
static BuildPanelFn g_build_panel;
static CommitFn g_commit;
static WidgetInitFn widget_init;
static WidgetSetTypeFn widget_set_type;
static NodeVec2Fn node_set_position, node_set_pivot, node_set_anchor;
static NodeAddChildFn node_add_child;
static NodeVec2Fn node_set_size;
static uint32_t g_rows; /* rows under the game's four (0: one row, the extra slots after the booster box) */
static SpecialModeFn special_mode;
static HoverFn g_hover;
static NavFn g_nav;
static SetFocusFn g_set_focus;
static SetSlotFn g_set_slot;
static OpenPickerFn g_open_picker;
static ClosePickerFn g_close_picker;
static PickerInputFn g_picker_input;
static StableToTypeFn stable_to_type;
static PlaySoundFn play_sound;
static InputIndexFn input_index;
static NavDirFn nav_dir;
static CursorOverFn cursor_over;
static ApplyStateFn apply_state;
static IsHoveredFn is_hovered;
static AugmentFn g_augment;
static ProfileLoadFn g_profile_load;
static LoadAssetsFn g_load_assets;
static ActivateFn g_activate;
static ResetFn g_reset;
static ListOpenFn g_list_open;
static CopyLoadoutFn g_copy_loadout;
static ListUpdateFn g_list_update;
static NodeFn text_init, node_layout, text_changed;
static TextFontFn text_font;
static TextReleaseFn text_release;
static NodeSetLayerFn node_set_layer;
static NodeSetAlphaFn node_set_alpha;
static NodeSetColorFn node_set_color;
static NodeSetVisibleFn node_set_visible;
static CooldownFn g_cooldown;
static CooldownSystemsFn g_cooldown_systems;
static EaglesSpentFn g_eagles_spent;
static EaglesUsedFn g_eagles_used;
static int g_eagles_ok;           /* the Eagles' code matched and is hooked */
static int g_activating_local;    /* inside the game's activation of a local player's stratagem */
static uint32_t g_key_prev = 'Q', g_key_next = 'E'; /* virtual-key codes */
static int g_wheel_on = 1;        /* page_wheel: the mouse wheel changes pages too */
static int g_page_hold = 1;       /* page_hold: no page change while a stratagem of the page is called in or in hand */
static int g_page_added = 1;      /* page_added: loadout stratagems other mods add to the list go in the pages too */
static volatile LONG64 g_list_seen; /* GetTickCount64 when the list's update last saw it open */

/* PickerInput's call in progress (its screen), and whether it set a slot (a pick, not the back button) */
static uint8_t* g_picking_screen;
static int g_picked;

static Panel g_panels[MAX_PANELS];
static uint32_t g_panel_count, g_evict_next;

/* The commit computes uses from the four types at COMMIT_TYPES; a buffer shaped like a panel lets it do that for the
 * extra slots too, into a scratch copy of the record. */
static uint8_t g_fake_panel[COMMIT_TYPES + GAME_SLOTS * WIDGET_SIZE];
static uint8_t g_scratch[LOADOUT_SIZE];
static uint8_t g_saved[MAX_EXTRA * LOADOUT_SLOT]; /* Commit: the record's extra entries from before the game's wipe */

/* Log lines per kind, so a session's log shows the first few of each without filling up. */
enum { LOG_BUILD, LOG_REFRESH, LOG_COMMIT, LOG_PICK, LOG_GUARD, LOG_PROFILE, LOG_PAGE, LOG_LABEL, LOG_COPY, LOG_EAGLE,
       LOG_ROWS, LOG_WHEEL, LOG_KINDS };
static uint32_t g_logged[LOG_KINDS];
static const uint32_t kLogLimit[LOG_KINDS] = { 16, 16, 40, 60, 40, 16, 200, 24, 6, 40, 200, 12 };
#define LOG_ONCE(kind, ...) do { if (g_logged[kind] < kLogLimit[kind]) { g_logged[kind]++; hf->log(self, __VA_ARGS__); } } while (0)

static uint32_t Get32(const uint8_t* at) { uint32_t v; memcpy(&v, at, 4); return v; }
static void Put32(uint8_t* at, uint32_t v) { memcpy(at, &v, 4); }
static uint64_t Get64(const uint8_t* at) { uint64_t v; memcpy(&v, at, 8); return v; }
static void Put64(uint8_t* at, uint64_t v) { memcpy(at, &v, 8); }

static uint64_t Vec2(float x, float y)
{
    uint32_t bx, by;
    memcpy(&bx, &x, 4);
    memcpy(&by, &y, 4);
    return (uint64_t)by << 32 | bx;
}

static uint8_t* Extra(const Panel* p, uint32_t k) { return p->widgets + (size_t)k * WIDGET_SIZE; }
static uint8_t* GameSlot(uint8_t* panel, uint32_t i) { return panel + PANEL_SLOTS + (size_t)i * WIDGET_SIZE; }

/* ---- the extra widgets ------------------------------------------------------------------------------------------- */

static Panel* FindPanel(const uint8_t* panel)
{
    for (uint32_t i = 0; i < g_panel_count; i++)
        if (g_panels[i].panel == panel) return &g_panels[i];
    return NULL;
}

/* A table entry for the panel. When the table is full the oldest entry is reused; its widgets stay allocated (a panel
 * that still exists may point at them), so a panel that comes back costs about 19 KB per 4 extra slots. */
static Panel* AddPanel(uint8_t* panel)
{
    Panel* p = FindPanel(panel);
    if (p != NULL) return p;
    uint8_t* widgets = VirtualAlloc(NULL, (size_t)g_extra * WIDGET_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (widgets == NULL)
    {
        LOG_ONCE(LOG_BUILD, "could not allocate the extra slot widgets (error %lu)", GetLastError());
        return NULL;
    }
    if (g_panel_count < MAX_PANELS) p = &g_panels[g_panel_count++];
    else
    {
        p = &g_panels[g_evict_next];
        g_evict_next = (g_evict_next + 1) % MAX_PANELS;
        LOG_ONCE(LOG_BUILD, "panel table full: panel %p's entry goes to %p", (void*)p->panel, (void*)panel);
    }
    p->panel = panel;
    p->widgets = widgets;
    p->built = 0;
    p->failed = 0;
    p->focus = p->hover = p->pick_row = -1;
    return p;
}

/* Builds the extra widgets the way LoadoutCard_BuildPanel builds the game's four, and links them under the panel. A
 * node takes a UI context when it is initialised (node + 248); if the extras did not get the game's four's, they are
 * not linked (built lazily, at another time than the screen's build, they might not). */
static void BuildExtras(Panel* p, const char* how)
{
    uint64_t want = Get64(p->panel + PANEL_SLOTS + NODE_CONTEXT);
    for (uint32_t k = 0; k < g_extra; k++)
    {
        uint8_t* w = Extra(p, k);
        widget_init(w, 0, 0);
        uint64_t got = Get64(w + NODE_CONTEXT);
        if (got != want)
        {
            LOG_ONCE(LOG_BUILD, "panel %p (%s): the extra slots got UI context %llx, the game's slots have %llx; not shown",
                     (void*)p->panel, how, (unsigned long long)got, (unsigned long long)want);
            p->failed = 1;
            return;
        }
    }
    for (uint32_t k = 0; k < g_extra; k++)
    {
        uint8_t* w = Extra(p, k); /* placed by PlaceSlots */
        node_set_pivot(w, 0);
        node_set_anchor(w, 0);
        node_add_child(p->panel, w, 0);
    }
    p->built = 1;
    p->failed = 0;
    p->focus = p->hover = p->pick_row = -1;
    LOG_ONCE(LOG_BUILD, "panel %p (%s): %u extra slots linked, UI context %llx", (void*)p->panel, how, g_extra,
             (unsigned long long)want);
}

/* Rows: the panel taller by the extra rows (the card's frame follows the panel's height when the card updates; the
 * equipment row is anchored to the panel's top, so it moves up with it), and the game's four and the booster box up
 * by the same. The game sets the height in BuildPanel and in every refresh that runs, so this follows both. */
static void FitPanel(uint8_t* panel)
{
    if (g_rows == 0) return;
    float want = PANEL_HEIGHT + (panel[PANEL_EXPANDED] ? PANEL_HEIGHT_EXPANDED : 0.0f) + (float)g_rows * SLOT_PITCH;
    float width, height;
    memcpy(&width, panel + NODE_SIZE_X, 4);
    memcpy(&height, panel + NODE_SIZE_Y, 4);
    if (height != want) node_set_size(panel, Vec2(width, want));
}

/* Where the slots go. One row: the extra ones after the booster box, which sits where a fifth slot would. Rows: the
 * game's four (and the booster box) move up by the extra rows and the extra rows take their place and below; the UI's
 * y grows upwards, from the panel's bottom.
 * pick_row >= 0: the stratagem picker is open on a slot of that row. The card then shows one row, at the game's row's
 * usual place (the rest of the panel is behind the card's header): that row goes there, the others past the card's
 * right edge, where the card cuts them off. */
#define HIDE_X 4000.0f
static void PlaceSlots(Panel* p, int pick_row)
{
    p->pick_row = pick_row;
    if (g_rows == 0)
    {
        for (uint32_t k = 0; k < g_extra; k++)
            node_set_position(Extra(p, k), Vec2(SLOT_ORIGIN + (float)(GAME_SLOTS + k + 1) * SLOT_PITCH, SLOT_ORIGIN));
        return;
    }
    for (uint32_t slot = 0; slot < g_slots; slot++)
    {
        int row = (int)(slot / g_per_row);
        float x = SLOT_ORIGIN + (float)(slot % g_per_row) * SLOT_PITCH;
        float y = SLOT_ORIGIN + (float)(g_rows - (uint32_t)row) * SLOT_PITCH;
        if (pick_row >= 0)
        {
            if (row == pick_row) y = SLOT_ORIGIN;
            else x += HIDE_X;
        }
        node_set_position(slot < GAME_SLOTS ? GameSlot(p->panel, slot) : Extra(p, slot - GAME_SLOTS), Vec2(x, y));
    }
    float booster_x = BOOSTER_X, booster_y = SLOT_ORIGIN + (float)g_rows * SLOT_PITCH;
    if (pick_row >= 0)
    {
        if (pick_row == 0) booster_y = SLOT_ORIGIN;
        else booster_x += HIDE_X;
    }
    node_set_position(p->panel + PANEL_BOOSTER, Vec2(booster_x, booster_y));
}

/* The layout for the loadout screen's picker state: one row while it picks a stratagem, all of them otherwise. */
static void SyncPickLayout(uint8_t* screen)
{
    Panel* p = FindPanel(screen + SCREEN_PANEL0);
    if (p == NULL || !p->built || g_rows == 0) return;
    int want = -1;
    if (Get32(screen + SCREEN_PICKER_MODE) == PICKER_STRATAGEMS && Get32(screen + SCREEN_CARD0_MODE) == 0)
    {
        uint32_t target = Get32(screen + SCREEN_PICKER_SLOT);
        if (target < g_slots) want = (int)(target / g_per_row);
    }
    if (want != p->pick_row) PlaceSlots(p, want);
}

static Panel* ReadyPanel(uint8_t* panel, const char* how)
{
    Panel* p = AddPanel(panel);
    if (p != NULL && !p->built && !p->failed)
    {
        BuildExtras(p, how);
        if (p->built)
        {
            PlaceSlots(p, -1);
            FitPanel(panel);
        }
    }
    return p != NULL && p->built ? p : NULL;
}

/* Would the refresh show this type? (its own filter: a known type with the flag, not 77 or 90 in the special mode) */
static int Shown(uint32_t type)
{
    if (type == 0 || type >= STRATAGEM_TYPES) return 0;
    const uint8_t* info = *(uint8_t* const*)(game + RVA_STRATAGEM_INFO + 8u * type);
    if (info == NULL || (info[INFO_FLAGS] & 2) == 0) return 0;
    if ((type == 77 || type == 90) && special_mode()) return 0;
    return 1;
}

/* The record's shown entries after the first four, the way the refresh counts them: each one's index into the record. */
static uint32_t ExtraEntries(const uint8_t* loadout, uint32_t* index, uint32_t* count_out)
{
    uint32_t count = Get32(loadout + LOADOUT_COUNT), shown = 0, found = 0;
    if (count > LOADOUT_MAX) count = LOADOUT_MAX;
    for (uint32_t j = 0; j < count && found < g_extra; j++)
    {
        if (!Shown(Get32(loadout + LOADOUT_SLOTS + j * LOADOUT_SLOT))) continue;
        if (shown++ < GAME_SLOTS) continue; /* one of the game's four */
        index[found++] = j;
    }
    if (count_out != NULL) *count_out = count;
    return found;
}

static void FillExtras(Panel* p, const uint8_t* loadout)
{
    uint32_t index[MAX_EXTRA], count = 0, found = 0;
    if (loadout != NULL) found = ExtraEntries(loadout, index, &count);
    for (uint32_t k = 0; k < g_extra; k++)
    {
        uint32_t type = k < found ? Get32(loadout + LOADOUT_SLOTS + index[k] * LOADOUT_SLOT) : 0;
        if (Get32(Extra(p, k) + WIDGET_TYPE) != type) widget_set_type(Extra(p, k), type);
    }
    if (found != 0) LOG_ONCE(LOG_REFRESH, "refresh panel %p: record %p holds %u, %u in the extra slots", (void*)p->panel,
             (const void*)loadout, count, found);
}

/* ---- focus ------------------------------------------------------------------------------------------------------- */


static void SetState(uint8_t* widget, uint16_t bit, int on)
{
    uint16_t state, now;
    memcpy(&state, widget + WIDGET_STATE, 2);
    now = on ? (uint16_t)(state | bit) : (uint16_t)(state & ~bit);
    if (now == state) return;
    memcpy(widget + WIDGET_STATE, &now, 2);
    apply_state(widget);
}

static int Locked(const uint8_t* widget)
{
    uint16_t state;
    memcpy(&state, widget + WIDGET_STATE, 2);
    return (state & STATE_LOCKED) != 0;
}

/* Focus on extra slot k: the game's focus on its slot 4 (index 3, through the original setter), drawn unfocused, and
 * the extra slot drawn focused. */
static void FocusExtra(Panel* p, int k)
{
    if (p->focus >= 0) SetState(Extra(p, (uint32_t)p->focus), STATE_FOCUS, 0);
    g_set_focus(p->panel, GAME_SLOTS - 1);
    SetState(GameSlot(p->panel, GAME_SLOTS - 1), STATE_FOCUS, 0);
    p->focus = k;
    SetState(Extra(p, (uint32_t)k), STATE_FOCUS, 1);
}

/* Off the extra slot; the game's next SetFocus then starts from no focus (its slot 4 is already drawn unfocused). */
static void UnfocusExtra(Panel* p)
{
    if (p->focus < 0) return;
    SetState(Extra(p, (uint32_t)p->focus), STATE_FOCUS, 0);
    p->focus = -1;
    Put32(p->panel + PANEL_FOCUS, 0xFFFFFFFFu);
    Put32(p->panel + PANEL_PREV_FOCUS, 0xFFFFFFFFu);
}

static int Pressed(const uint8_t* input, uint64_t action)
{
    return input[32u * input_index(action) + INPUT_ENTRIES] != 0;
}

static const uint8_t* InputModes(void) { return *(const uint8_t* const*)(game + RVA_G_INPUT_MODE); }

/* ---- hooks ------------------------------------------------------------------------------------------------------- */

static uint32_t BuildPanel(uint8_t* panel)
{
    uint32_t result = g_build_panel(panel);
    Panel* p = FindPanel(panel);
    if (p != NULL) p->built = p->failed = 0; /* the build reset the panel's children: link the extras again */
    ReadyPanel(panel, "panel build");
    return result;
}

static void Refresh(uint8_t* panel, uint8_t* loadout, uint8_t expanded, uint8_t force)
{
    /* The refresh returns early when nothing changed; then the extras are up to date too, once they exist. */
    Panel* known = FindPanel(panel);
    int runs = force || known == NULL || !known->built || Get64(panel + PANEL_SHOWN) != (uint64_t)(uintptr_t)loadout ||
               panel[PANEL_EXPANDED] != expanded;
    /* Shown for the first time, the game puts the focus on its first slot itself (without SetFocus). */
    if (known != NULL && known->built && Get64(panel + PANEL_SHOWN) == 0 && known->focus >= 0)
    {
        SetState(Extra(known, (uint32_t)known->focus), STATE_FOCUS, 0);
        known->focus = -1;
    }
    g_refresh(panel, loadout, expanded, force);
    if (!runs) return;
    Panel* p = ReadyPanel(panel, "first refresh");
    if (p == NULL) return;
    FitPanel(panel); /* the refresh set the game's height again */
    FillExtras(p, loadout);
}

/* Appends entries for the given types, each with the uses the game's commit gives it (base + ship upgrades). */
static uint32_t AppendTypes(uint8_t* loadout, uint32_t count, const uint32_t* types, uint32_t n)
{
    for (uint32_t first = 0; first < n && count < g_slots; first += GAME_SLOTS)
    {
        int any = 0;
        for (uint32_t i = 0; i < GAME_SLOTS; i++)
        {
            uint32_t type = first + i < n ? types[first + i] : 0;
            Put32(g_fake_panel + COMMIT_TYPES + i * WIDGET_SIZE, type);
            any |= type != 0;
        }
        if (!any) continue;
        memcpy(g_scratch, loadout, LOADOUT_SIZE);
        g_commit(g_fake_panel, g_scratch);
        uint32_t made = Get32(g_scratch + LOADOUT_COUNT);
        for (uint32_t i = 0; i < made && i < GAME_SLOTS && count < g_slots; i++, count++)
            memcpy(loadout + LOADOUT_SLOTS + count * LOADOUT_SLOT, g_scratch + LOADOUT_SLOTS + i * LOADOUT_SLOT,
                   LOADOUT_SLOT);
    }
    return count;
}

static void* Commit(uint8_t* panel, uint8_t* loadout)
{
    /* The game's commit wipes the record's entries. Keep the extra ones first, in case this panel has no extra slots
     * to take them from (they would be lost from the profile at the next save otherwise). */
    uint32_t index[MAX_EXTRA], saved = 0, before = 0;
    if (loadout != NULL)
    {
        saved = ExtraEntries(loadout, index, &before);
        for (uint32_t k = 0; k < saved; k++)
            memcpy(g_saved + k * LOADOUT_SLOT, loadout + LOADOUT_SLOTS + index[k] * LOADOUT_SLOT, LOADOUT_SLOT);
    }

    void* result = g_commit(panel, loadout);
    if (loadout == NULL) return result;
    uint32_t count = Get32(loadout + LOADOUT_COUNT), game_count = count;
    if (count > LOADOUT_MAX) return result;

    Panel* p = FindPanel(panel);
    const char* from;
    if (p != NULL && p->built)
    {
        uint32_t types[MAX_EXTRA];
        for (uint32_t k = 0; k < g_extra; k++) types[k] = Get32(Extra(p, k) + WIDGET_TYPE);
        count = AppendTypes(loadout, count, types, g_extra);
        from = "the extra slots";
    }
    else
    {
        for (uint32_t k = 0; k < saved && count < g_slots; k++, count++)
            memcpy(loadout + LOADOUT_SLOTS + count * LOADOUT_SLOT, g_saved + k * LOADOUT_SLOT, LOADOUT_SLOT);
        from = "the record (no extra slots on this panel)";
    }
    /* The lobby broadcast commits into a copy of its own first and packs that copy into lists of 16 without a check:
     * the copy gets the first 16 (the card's own record, its second commit, keeps them all). */
    const NT_TIB* tib = (const NT_TIB*)NtCurrentTeb();
    int on_stack = (uintptr_t)loadout >= (uintptr_t)tib->StackLimit && (uintptr_t)loadout < (uintptr_t)tib->StackBase;
    if (count > GAME_LIST_MAX && (on_stack || (uintptr_t)__builtin_return_address(0) == game + RVA_BROADCAST_RETURN))
    {
        memset(loadout + LOADOUT_SLOTS + GAME_LIST_MAX * LOADOUT_SLOT, 0, (size_t)(count - GAME_LIST_MAX) * LOADOUT_SLOT);
        LOG_ONCE(LOG_GUARD, "lobby broadcast: its copy %p gets the first 16 of %u", (void*)loadout, count);
        count = GAME_LIST_MAX;
    }
    Put32(loadout + LOADOUT_COUNT, count);
    if (count > game_count)
    {
        char list[MAX_EXTRA * 24] = "";
        for (uint32_t j = game_count; j < count; j++)
        {
            char one[24];
            wsprintfA(one, " %u:%u", Get32(loadout + LOADOUT_SLOTS + j * LOADOUT_SLOT),
                      Get32(loadout + LOADOUT_SLOTS + j * LOADOUT_SLOT + 4));
            lstrcatA(list, one);
        }
        LOG_ONCE(LOG_COMMIT, "commit panel %p into %p: %u before, the game wrote %u, %u after, from %s; type:uses%s",
                 (void*)panel, (void*)loadout, before, game_count, count, from, list);
    }
    return result;
}

/* Any focus change the game makes takes the focus off the extra slot. */
static uint64_t HookSetFocus(uint8_t* panel, uint32_t index)
{
    Panel* p = FindPanel(panel);
    if (p != NULL && p->focus >= 0) UnfocusExtra(p);
    return g_set_focus(panel, index);
}

/* The mouse: after the game's own test of its widgets, the same test of the extra slots. With the virtual cursor the
 * game only draws hover (no index); so do the extra slots. */
static uint64_t Hover(uint8_t* panel, uint64_t unused, uint8_t cursor_off)
{
    uint64_t result = g_hover(panel, unused, cursor_off);
    Panel* p = FindPanel(panel);
    if (p == NULL || !p->built) return result;
    int mouse_path = !(InputModes()[MODE_VIRTUAL_CURSOR] || cursor_off);
    uint8_t* cursor = *(uint8_t**)(game + RVA_G_CURSOR) + CURSOR;
    p->hover = -1;
    for (uint32_t k = 0; k < g_extra; k++)
    {
        uint8_t* w = Extra(p, k);
        if (mouse_path)
        {
            if (is_hovered(w)) p->hover = (int)k;
        }
        else
            SetState(w, STATE_HOVER, !cursor_off && cursor_over(cursor, w, 0));
    }
    return result;
}

/* Rows: down from the game's four into the rows under them and back up, the same column; left and right along a row
 * of extra slots (off the card at its ends, as from the game's row). NAV_PASS: the game's own handling. */
#define NAV_PASS 0xFFu
static uint64_t RowNav(Panel* p, int32_t dir)
{
    uint8_t* panel = p->panel;
    if (p->focus < 0)
    {
        uint32_t focus = Get32(panel + PANEL_FOCUS);
        if (dir != NAV_DOWN || focus >= GAME_SLOTS) return NAV_PASS;
        uint32_t k = focus + g_per_row - GAME_SLOTS; /* the slot under it */
        if (k >= g_extra || Locked(Extra(p, k))) return NAV_PASS;
        play_sound(0, SOUND_MOVE);
        FocusExtra(p, (int)k);
        return NAV_MOVED;
    }
    uint32_t slot = GAME_SLOTS + (uint32_t)p->focus, col = slot % g_per_row;
    if (dir == NAV_DOWN || dir == NAV_TO_EQUIPMENT)
    {
        int up = dir == NAV_TO_EQUIPMENT;
        if (!up && slot + g_per_row >= g_slots) return 0; /* the bottom row */
        play_sound(0, SOUND_MOVE);
        if (up && slot < 2 * g_per_row) /* the row under the game's four: up into them */
        {
            UnfocusExtra(p);
            g_set_focus(panel, col < GAME_SLOTS ? col : GAME_SLOTS - 1);
        }
        else
            FocusExtra(p, (int)(up ? slot - g_per_row : slot + g_per_row) - (int)GAME_SLOTS);
        return NAV_MOVED;
    }
    if (dir == NAV_LEFT || dir == NAV_RIGHT)
    {
        int right = dir == NAV_RIGHT;
        int edge = right ? (col + 1 == g_per_row || slot + 1 >= g_slots) : col == 0;
        play_sound(0, SOUND_MOVE);
        if (!edge)
        {
            FocusExtra(p, p->focus + (right ? 1 : -1));
            return NAV_MOVED;
        }
        UnfocusExtra(p);
        g_set_focus(panel, 0xFFFFFFFFu);
        return right ? NAV_LEAVE_RIGHT : NAV_LEAVE_LEFT;
    }
    return NAV_PASS;
}

/* Clicks on an extra slot, and the controller's way into and along them. One row: the game's four, the booster box,
 * then the extra slots, then off the card (as from the booster box). Rows: RowNav. */
static uint64_t Nav(uint8_t* panel, uint8_t* input, uint64_t unused, uint8_t nav_on)
{
    Panel* p = FindPanel(panel);
    if (p == NULL || !p->built) return g_nav(panel, input, unused, nav_on);
    if (panel[PANEL_EXPANDED] && p->hover >= 0 && InputModes()[MODE_MOUSE] && Pressed(input, ACTION_SELECT))
    {
        play_sound(0, SOUND_SELECT);
        FocusExtra(p, p->hover);
        LOG_ONCE(LOG_PICK, "click on extra slot %d", p->focus + (int)GAME_SLOTS + 1);
        return NAV_OPEN_CLICK;
    }
    if (nav_on)
    {
        int32_t dir = nav_dir(input);
        if (g_rows != 0)
        {
            uint64_t moved = RowNav(p, dir);
            if (moved != NAV_PASS) return moved;
        }
        else if (p->focus >= 0)
        {
            int k = p->focus;
            if (dir == NAV_RIGHT)
            {
                play_sound(0, SOUND_MOVE);
                if ((uint32_t)k + 1 < g_extra)
                {
                    FocusExtra(p, k + 1);
                    return NAV_MOVED;
                }
                UnfocusExtra(p);
                g_set_focus(panel, 0xFFFFFFFFu);
                return NAV_LEAVE_RIGHT;
            }
            if (dir == NAV_LEFT)
            {
                play_sound(0, SOUND_MOVE);
                if (k > 0) FocusExtra(p, k - 1);
                else
                {
                    UnfocusExtra(p);
                    g_set_focus(panel, FOCUS_BOOSTER);
                }
                return NAV_MOVED;
            }
            if (dir == NAV_TO_EQUIPMENT)
            {
                if (!panel[PANEL_EXPANDED]) return 0;
                play_sound(0, SOUND_MOVE);
                UnfocusExtra(p);
                g_set_focus(panel, FOCUS_EQUIPMENT);
                return NAV_MOVED;
            }
        }
        else if (dir == NAV_RIGHT && Get32(panel + PANEL_FOCUS) == FOCUS_BOOSTER && !Locked(Extra(p, 0)))
        {
            play_sound(0, SOUND_MOVE);
            FocusExtra(p, 0);
            return NAV_MOVED;
        }
    }
    /* The rest (the select button with an extra slot focused: the game sees its slot 4 focused, opens the picker in
     * stratagem mode, and OpenPicker below retargets it). */
    return g_nav(panel, input, unused, nav_on);
}

static double OpenPicker(uint8_t* screen, uint64_t unused)
{
    /* The focused extra slot from before the call: the open itself may touch the card's focus. */
    Panel* p = FindPanel(screen + SCREEN_PANEL0);
    int focus = p != NULL && p->built ? p->focus : -1;
    double result = g_open_picker(screen, unused);
    if (focus >= 0 && Get32(screen + SCREEN_PICKER_MODE) == PICKER_STRATAGEMS)
    {
        Put32(screen + SCREEN_PICKER_SLOT, GAME_SLOTS + (uint32_t)focus);
        if (p->focus != focus) FocusExtra(p, focus);
        LOG_ONCE(LOG_PICK, "picker opened on extra slot %d", focus + (int)GAME_SLOTS + 1);
    }
    SyncPickLayout(screen);
    return result;
}

static uint8_t PickerInput(uint8_t* screen, uint64_t input)
{
    uint8_t* outer = g_picking_screen;
    int outer_picked = g_picked;
    g_picking_screen = screen;
    g_picked = 0;
    uint8_t result = g_picker_input(screen, input);
    g_picking_screen = outer;
    g_picked = outer_picked;
    SyncPickLayout(screen); /* a pick may have moved the picker on to another row's slot */
    return result;
}

/* A pick into slot 5+: into the extra widget (the commit that follows puts it in the record). The game's network
 * message for one slot is not sent for these; the commit's caller sends the whole loadout. */
static void SetSlot(uint8_t* card, uint32_t slot, uint32_t stable)
{
    if (g_picking_screen != NULL) g_picked = 1;
    if (slot < GAME_SLOTS || slot >= g_slots)
    {
        g_set_slot(card, slot, stable);
        return;
    }
    Panel* p = FindPanel(card + CARD_PANEL);
    if (p == NULL || !p->built) return;
    uint32_t type = stable_to_type(stable);
    widget_set_type(Extra(p, slot - GAME_SLOTS), type);
    LOG_ONCE(LOG_PICK, "slot %u set to type %u (stable id %u)", slot + 1, type, stable);
}

/* After a pick the game moves on to an empty one of its four, or closes the picker when they are full: then move on
 * to an empty extra slot instead. Not the back button's close (no slot was set), nor another view of the card. */
static uint64_t ClosePicker(uint8_t* screen, uint64_t unused)
{
    if (screen == g_picking_screen && g_picked && Get32(screen + SCREEN_PICKER_MODE) == PICKER_STRATAGEMS &&
        Get32(screen + SCREEN_CARD0_MODE) == 0)
    {
        Panel* p = FindPanel(screen + SCREEN_PANEL0);
        if (p != NULL && p->built)
            for (uint32_t k = 0; k < g_extra; k++)
            {
                uint8_t* w = Extra(p, k);
                if (Get32(w + WIDGET_TYPE) != 0 || Locked(w)) continue;
                Put32(screen + SCREEN_PICKER_SLOT, GAME_SLOTS + k);
                FocusExtra(p, (int)k);
                LOG_ONCE(LOG_PICK, "picker moves on to extra slot %u", k + GAME_SLOTS + 1);
                SyncPickLayout(screen);
                return 0;
            }
    }
    uint64_t result = g_close_picker(screen, unused);
    SyncPickLayout(screen);
    return result;
}

/* ---- the game's limit of 16 -------------------------------------------------------------------------------------- */

/* The types of a record's entries first..last-1, for the log. */
static void TypeList(const uint8_t* record, uint32_t first, uint32_t last, char* out, size_t size)
{
    out[0] = 0;
    for (uint32_t j = first; j < last && (size_t)lstrlenA(out) + 12 < size; j++)
    {
        char one[12];
        wsprintfA(one, j > first ? " %u" : "%u", Get32(record + LOADOUT_SLOTS + j * LOADOUT_SLOT));
        lstrcatA(out, one);
    }
}

/* ---- pages ------------------------------------------------------------------------------------------------------- */

/* The in-mission list never holds more than 16: the game's own entries (mission stratagems, linked ones like Eagle
 * Rearm, whatever it adds during the mission) stay in it, and the picks take turns in it a page at a time (slots 1..k,
 * then k+1..2k, ...; k = 16 - the game's entries at ready-up). A pick's uses and cooldown live in its own entry, so a
 * pick out of the list keeps them here (its cooldown is an end time: it runs on). The page changes with the keys while
 * the list is open, or by itself when a stratagem in hand is on another page. */
#define MAX_ITEMS (MAX_SLOTS + GAME_LIST_MAX)
typedef struct
{
    int active;                   /* a loadout's picks went to the mission and were found again in its record */
    int paged;                    /* its list has been in pages (more than one page, or an entry taken in) */
    uint32_t picks;               /* the loadout's picks: items 0..picks-1 */
    uint32_t items;               /* the picks, then the entries taken in during the mission (page_added) */
    uint32_t per_page, pages, page;
    uint32_t in_record;           /* the picks the record went to the mission with (page 1 at ready-up) */
    int checked;                  /* those picks were seen in the local player's list (the record was this player's) */
    uint32_t fixed_types[GAME_LIST_MAX], fixed; /* the game's entries at ready-up: on every page */
    uint8_t fresh[MAX_ITEMS][LOADOUT_SLOT]; /* each item's entry at ready-up (or when it was taken in) */
    uint8_t state[MAX_ITEMS][LOADOUT_SLOT]; /* its entry now (for the items in the list: as of the last page change) */
    uint8_t gone[MAX_ITEMS];      /* left the list during the mission (used up): not put back */
    uint8_t shown[MAX_ITEMS];     /* put in the list by the last page change (or at ready-up) */
} Pager;
static Pager g_pager;
static int g_list_is_open;
static int g_refitting;           /* the page change under way is the update's own (room in the list), not the player's */

static uint32_t EntryType(const uint8_t* list, uint32_t i) { return Get32(list + LOADOUT_SLOTS + i * LOADOUT_SLOT); }

/* The local player's id, as PlayerHistory entries hold it at +0 (0: none). */
static uint64_t LocalPeer(void)
{
    const uint8_t* network = *(const uint8_t* const*)(game + RVA_G_NETWORK);
    return network != NULL ? Get64(network + NETWORK_LOCAL_PEER) : 0;
}

/* The local player's in-mission list (the record layout), or NULL. */
static uint8_t* LocalList(void)
{
    uint8_t* history = *(uint8_t* const*)(game + RVA_G_HISTORY);
    uint64_t peer = LocalPeer();
    if (history == NULL || peer == 0) return NULL;
    uint32_t n = Get32(history + HISTORY_COUNT);
    for (uint32_t i = 0; i < n && i < HISTORY_MAX; i++)
    {
        uint8_t* entry = history + (size_t)i * HISTORY_STRIDE;
        if (Get64(entry) == peer) return entry + HISTORY_LIST;
    }
    return NULL;
}

static uint32_t PageFirst(uint32_t page) { return page * g_pager.per_page; }
static uint32_t PageEnd(uint32_t page)
{
    uint32_t end = (page + 1) * g_pager.per_page;
    return end < g_pager.items ? end : g_pager.items;
}

static const uint8_t* Info(uint32_t type); /* with the Eagles */

/* A stratagem a player can put in a loadout: a stable id, and the flag the loadout's own refresh shows a type by. The
 * mission's own (Reinforce, Resupply, Eagle Rearm, objective stratagems) are not. */
#define INFO_STABLE 4u
static int Selectable(uint32_t type)
{
    const uint8_t* info = g_eagles_ok ? Info(type) : NULL;
    return info != NULL && Get32(info + INFO_STABLE) != 0 && (Get32(info + INFO_FLAGS) & 2) != 0;
}

/* The list against the pages. Its entries are the items in it, the game's own from ready-up, and whatever was added
 * since (by the mission, or by another mod such as P.S.S.). With page_added, an added loadout stratagem is taken in as
 * an item after the picks, so it is in the pages like them; anything else stays on every page. A page is as many items
 * as fit beside the entries that stay, less LIST_SPARE rows left free for the next addition, so the pages are counted
 * again from the list as it is now. Returns the items in the list. */
static uint32_t FitPages(const uint8_t* list)
{
    uint32_t n = Get32(list + LOADOUT_COUNT);
    if (n > GAME_LIST_MAX || g_pager.per_page == 0) return 0;
    uint8_t taken[GAME_LIST_MAX] = { 0 };
    uint32_t ours = 0;
    for (uint32_t k = 0; k < g_pager.items; k++)
    {
        if (!g_pager.shown[k] || g_pager.gone[k]) continue;
        uint32_t type = Get32(g_pager.state[k]), i = 0;
        while (i < n && (taken[i] || Get32(list + LOADOUT_SLOTS + i * LOADOUT_SLOT) != type)) i++;
        if (i == n) continue;
        taken[i] = 1;
        ours++;
    }
    uint32_t fixed_types[GAME_LIST_MAX];
    memcpy(fixed_types, g_pager.fixed_types, sizeof fixed_types);
    for (uint32_t i = 0; i < n; i++)
    {
        if (taken[i]) continue;
        const uint8_t* e = list + LOADOUT_SLOTS + i * LOADOUT_SLOT;
        uint32_t type = Get32(e), j = 0, k = 0;
        while (j < g_pager.fixed && fixed_types[j] != type) j++;
        if (j < g_pager.fixed) { fixed_types[j] = 0xFFFFFFFFu; continue; }
        g_pager.paged = 1; /* something was added: from here on the pages leave rows free for the next */
        if (!g_page_added || !Selectable(type)) continue;
        /* an item out of the list that was added again (P.S.S. after a change of players) is that item; else a new one */
        while (k < g_pager.items && (g_pager.shown[k] || g_pager.gone[k] || Get32(g_pager.state[k]) != type)) k++;
        if (k == g_pager.items)
        {
            if (g_pager.items >= MAX_ITEMS) continue;
            g_pager.items++;
            memcpy(g_pager.fresh[k], e, LOADOUT_SLOT);
            g_pager.gone[k] = 0;
            LOG_ONCE(LOG_PAGE, "pages: type %u in row %u was added to the list: item %u, in the pages after the picks", type,
                     i + 1, k + 1);
        }
        memcpy(g_pager.state[k], e, LOADOUT_SLOT);
        g_pager.shown[k] = 1;
        taken[i] = 1;
        ours++;
        g_pager.paged = 1;
    }
    uint32_t others = n - ours, room = GAME_LIST_MAX - others;
    /* a loadout that fits and has had nothing added keeps its one page, with whatever rows are free */
    uint32_t spare = g_pager.paged ? LIST_SPARE : 0;
    uint32_t per = room > spare ? room - spare : 1;
    uint32_t pages = (g_pager.items + per - 1) / per;
    if (pages == 0) pages = 1;
    if (per != g_pager.per_page || pages != g_pager.pages)
    {
        uint32_t first = PageFirst(g_pager.page);
        LOG_ONCE(LOG_PAGE, "pages: %u entries stay on every page, %u items: %u pages of %u now (%u of %u before)", others,
                 g_pager.items, pages, per, g_pager.pages, g_pager.per_page);
        g_pager.per_page = per;
        g_pager.pages = pages;
        g_pager.page = first / per;
        if (g_pager.page >= pages) g_pager.page = pages - 1;
    }
    if (pages >= 2) g_pager.paged = 1;
    return ours;
}

/* A list entry's fields past type (+0) and uses (+4): u64 times on the game's clock (microseconds). */
#define ENTRY_USES 4u
#define ENTRY_START 0x10u        /* its cooldown's start */
#define ENTRY_COOLDOWN_END 0x18u
#define ENTRY_CALL_END 0x20u     /* its call-in's end (set when it is used: now + the call-in time) */
#define CALL_HOLD_US 2000000ull  /* after a call-in ends its row flashes 1.5 s ("activating") */
#define TYPE_EAGLE_REARM 49u
#define INFO_LINKED 200u         /* u32: the type it brings along (an Eagle's is Eagle Rearm) */
#define INFO_TYPES 198u          /* the game's 150 and HellForge's room for 48 more */

static uint64_t g_view_until;    /* the HUD rows are logged until then, after a page change */

/* The game's clock, or 0 (without the Eagles' code, which checks where it is read). */
static uint64_t Now(void)
{
    if (!g_eagles_ok) return 0;
    const uint8_t* clock = *(const uint8_t* const*)(game + RVA_G_CLOCK);
    return clock != NULL ? Get64(clock + 0x18) : 0;
}

/* Being called in, or its row still flashing after: its row must keep showing it. */
static int CallingIn(const uint8_t* entry, uint64_t now)
{
    uint64_t end = Get64(entry + ENTRY_CALL_END);
    return now != 0 && end != 0 && now < end + CALL_HOLD_US;
}

/* The local player's list row named by the game's in-hand records (RVA_ARMED_QUERY), or -1. */
#define ARMED_COUNT 28u
#define ARMED_RECORDS 96u
#define ARMED_STRIDE 40u
#define ARMED_HOLD_MS 10000u     /* a record that holds a page change longer than this is not waited for */
static int ArmedRow(const uint8_t** record)
{
    *record = NULL;
    if (!g_eagles_ok) return -1;
    const uint8_t* system = *(const uint8_t* const*)(game + RVA_G_ARMED);
    if (system == NULL) return -1;
    uint32_t n = Get32(system + ARMED_COUNT);
    const uint8_t* records = *(const uint8_t* const*)(system + ARMED_RECORDS);
    uint64_t peer = LocalPeer();
    if (records == NULL || peer == 0 || n > 1024) return -1;
    for (uint32_t i = 0; i < n; i++)
    {
        const uint8_t* r = records + (size_t)i * ARMED_STRIDE;
        uint32_t row = Get32(r + 24);
        if (Get64(r + 16) == peer && r[1] == 0 && row < GAME_LIST_MAX) { *record = r; return (int)row; }
    }
    return -1;
}

/* Puts page `to` in the list in place of the current one. The current page's picks are found in the list by type (in
 * order) and kept here; the rest of the list (the game's) stays as it is, and the new page's picks go after it.
 * Unless forced, a change that would move or replace an entry being called in is held back (1: changed,
 * 0: not possible, -1: held back): the HUD shows a call-in by its row, so after a change that row would show the new
 * page's stratagem as the one coming in. */
static int SwitchPage(uint32_t to, const char* why, int force)
{
    uint8_t* list = LocalList();
    if (list == NULL) LOG_ONCE(LOG_PAGE, "page change (%s): no list of the local player (id %llu)", why,
                               (unsigned long long)LocalPeer());
    if (list == NULL) return 0;
    uint32_t n = Get32(list + LOADOUT_COUNT);
    if (n > GAME_LIST_MAX) return 0;
    FitPages(list);
    if (to >= g_pager.pages) return 0;

    uint8_t keep[GAME_LIST_MAX][LOADOUT_SLOT];
    uint32_t kept = 0, insert_at;
    uint8_t taken[GAME_LIST_MAX] = { 0 };
    uint8_t placed[MAX_ITEMS] = { 0 }; /* the items this change puts in the list */
    int found[MAX_ITEMS]; /* the items in the list now: where each is, or -1 (used up) */
    for (uint32_t k = 0; k < MAX_ITEMS; k++) found[k] = -1;
    for (uint32_t k = 0; k < g_pager.items; k++)
    {
        if (!g_pager.shown[k] || g_pager.gone[k]) continue;
        uint32_t type = Get32(g_pager.state[k]), i = 0;
        while (i < n && (taken[i] || EntryType(list, i) != type)) i++;
        if (i == n) continue; /* the game took it out (used up) */
        taken[i] = 1;
        found[k] = (int)i;
    }
    for (uint32_t i = 0; i < n; i++)
    {
        if (taken[i]) continue;
        memcpy(keep[kept++], list + LOADOUT_SLOTS + i * LOADOUT_SLOT, LOADOUT_SLOT);
    }
    /* The page goes after every entry that stays. The HUD draws granted entries (the mission's, another mod's) before
     * the picks, in the list's order: with the page in front of the game's trailing entries (an objective's stratagem,
     * Eagle Rearm), an added stratagem on the page came before them and they moved from page to page. */
    insert_at = kept;

    uint8_t out[GAME_LIST_MAX][LOADOUT_SLOT];
    uint32_t count = 0, put = 0, room = GAME_LIST_MAX - kept; /* the game's entries always stay */
    for (uint32_t i = 0; i <= kept; i++)
    {
        if (i == insert_at)
            for (uint32_t k = PageFirst(to); k < PageEnd(to) && put < room; k++)
                if (!g_pager.gone[k] && (!g_pager.shown[k] || found[k] >= 0))
                {
                    /* a pick in the list comes from the list (its state here is as of the last change) */
                    const uint8_t* from = g_pager.shown[k] ?
                        list + LOADOUT_SLOTS + (uint32_t)found[k] * LOADOUT_SLOT : g_pager.state[k];
                    memcpy(out[count++], from, LOADOUT_SLOT);
                    placed[k] = 1;
                    put++;
                }
        if (i < kept && count < GAME_LIST_MAX) memcpy(out[count++], keep[i], LOADOUT_SLOT);
    }

    /* the rows the game ties to a stratagem by their index: being called in, or named by an in-hand record (which is
     * not waited for longer than ARMED_HOLD_MS, in case it outlives the throw) */
    static ULONGLONG armed_since;
    static const uint8_t* stale;  /* a record not waited for any more, while it names the same row */
    static int stale_row = -1;
    const uint8_t* record = NULL;
    uint64_t now = Now();
    /* page_hold = off: the player takes the wrong name on that row (not for a change the player did not ask for) */
    if (!g_page_hold && !g_refitting) force = 1;
    int armed = force ? -1 : ArmedRow(&record);
    if (!force && record == NULL) stale = NULL;
    if (armed >= 0 && record == stale && armed == stale_row) armed = -1;
    else if (armed >= 0 && armed_since != 0 && GetTickCount64() - armed_since > ARMED_HOLD_MS)
    {
        LOG_ONCE(LOG_PAGE, "page change (%s): row %d is still named in hand after %u ms; not waited for", why, armed + 1,
                 ARMED_HOLD_MS);
        stale = record;
        stale_row = armed;
        armed = -1;
    }
    for (uint32_t i = 0; i < n && !force; i++)
    {
        const uint8_t* e = list + LOADOUT_SLOTS + i * LOADOUT_SLOT;
        int calling = CallingIn(e, now);
        if ((!calling && (int)i != armed) || (i < count && Get32(out[i]) == Get32(e))) continue;
        if (calling) armed_since = 0;
        else if (armed_since == 0) armed_since = GetTickCount64();
        /* (the refit asks again every update: not logged) */
        if (!g_refitting)
            LOG_ONCE(LOG_PAGE, "page change (%s) held back: type %u in row %u is %s (call-in ends in %d ms)", why,
                     Get32(e), i + 1, calling ? "being called in" : "in hand",
                     (int)(((int64_t)Get64(e + ENTRY_CALL_END) - (int64_t)now) / 1000));
        return -1;
    }
    armed_since = 0;

    /* only a pick that was in the list and is not there any more was used up; one that found no room was never in it */
    for (uint32_t k = 0; k < g_pager.items; k++)
    {
        if (!g_pager.shown[k] || g_pager.gone[k]) continue;
        if (found[k] < 0) { g_pager.gone[k] = 1; continue; }
        memcpy(g_pager.state[k], list + LOADOUT_SLOTS + (uint32_t)found[k] * LOADOUT_SLOT, LOADOUT_SLOT);
    }
    memcpy(g_pager.shown, placed, sizeof g_pager.shown);
    uint8_t* slots = list + LOADOUT_SLOTS;
    memcpy(slots, out, (size_t)count * LOADOUT_SLOT);
    if (count < n) memset(slots + count * LOADOUT_SLOT, 0, (size_t)(n - count) * LOADOUT_SLOT);
    Put32(list + LOADOUT_COUNT, count);
    LOG_ONCE(LOG_PAGE, "page %u of %u (%s): %u picks in, the game's %u kept, list %u -> %u", to + 1, g_pager.pages, why,
             put, kept, n, count);
    g_pager.page = to;
    if (now != 0) g_view_until = now + 8000000ull;
    return 1;
}

/* Before a loadout goes to the mission, the game's systems add their entries: the mission's stratagems in front of the
 * picks, linked ones (Eagle Rearm) and others after. Over 16 in all, the picks past the first page come out of the
 * record into the pages. The additions are what is not a pick, found by type in the picks' order. Without room for a
 * page of at least MIN_PAGE, the last picks are left out instead (the old guard). */
static void Augment(uint8_t* record)
{
    uint32_t picks = record != NULL ? Get32(record + LOADOUT_COUNT) : 0;
    if (record == NULL || picks > MAX_SLOTS)
    {
        g_augment(record);
        /* not a loadout of this mod's: its first 16 at most go on (the player history takes no more) */
        uint32_t now = record != NULL ? Get32(record + LOADOUT_COUNT) : 0;
        if (now > GAME_LIST_MAX && now <= LOADOUT_MAX)
        {
            memset(record + LOADOUT_SLOTS + GAME_LIST_MAX * LOADOUT_SLOT, 0, (size_t)(now - GAME_LIST_MAX) * LOADOUT_SLOT);
            Put32(record + LOADOUT_COUNT, GAME_LIST_MAX);
            LOG_ONCE(LOG_GUARD, "record %p: %u entries after the game's additions; it keeps its first 16", (void*)record,
                     now);
        }
        return;
    }
    uint32_t pick_types[MAX_SLOTS];
    for (uint32_t j = 0; j < picks; j++) pick_types[j] = EntryType(record, j);
    g_augment(record);
    uint32_t after = Get32(record + LOADOUT_COUNT);
    if (after > LOADOUT_MAX) return;

    int pick_of[LOADOUT_MAX]; /* the entry's pick index, or -1 for the game's */
    uint32_t matched = 0;
    for (uint32_t i = 0; i < after; i++)
    {
        pick_of[i] = -1;
        if (matched < picks && EntryType(record, i) == pick_types[matched]) pick_of[i] = (int)matched++;
    }
    uint32_t added = after - matched;
    if (after <= GAME_LIST_MAX)
    {
        /* Every caller applies a loadout to the mission (ready-up, the game's rebuilds). One that fits goes as it is,
         * in one page; the pages start during the mission if other mods add theirs to the list (FitPages). */
        g_pager.active = 0;
        if (picks == 0 || matched != picks || added > GAME_LIST_MAX) return;
        memset(&g_pager, 0, sizeof g_pager);
        g_pager.picks = g_pager.items = g_pager.in_record = picks;
        g_pager.per_page = GAME_LIST_MAX - added > LIST_SPARE ? GAME_LIST_MAX - added - LIST_SPARE : 1;
        if (g_pager.per_page < picks) g_pager.per_page = picks; /* they are all in the list until FitPages counts */
        g_pager.pages = 1;
        for (uint32_t i = 0; i < after; i++)
        {
            const uint8_t* e = record + LOADOUT_SLOTS + i * LOADOUT_SLOT;
            if (pick_of[i] < 0) { g_pager.fixed_types[g_pager.fixed++] = Get32(e); continue; }
            memcpy(g_pager.fresh[pick_of[i]], e, LOADOUT_SLOT);
            memcpy(g_pager.state[pick_of[i]], e, LOADOUT_SLOT);
            g_pager.shown[pick_of[i]] = 1;
        }
        g_pager.active = 1;
        LOG_ONCE(LOG_PAGE, "record %p: %u picks and %u the game added: %u in all, one page", (void*)record, picks, added,
                 after);
        return;
    }
    char added_list[LOADOUT_MAX * 12] = "";
    for (uint32_t i = 0; i < after; i++)
        if (pick_of[i] < 0 && lstrlenA(added_list) + 12 < (int)sizeof added_list)
        {
            char one[12];
            wsprintfA(one, added_list[0] ? " %u" : "%u", EntryType(record, i));
            lstrcatA(added_list, one);
        }

    uint8_t out[GAME_LIST_MAX][LOADOUT_SLOT];
    uint32_t count = 0;
    if (matched == picks && added + LIST_SPARE + MIN_PAGE <= GAME_LIST_MAX)
    {
        memset(&g_pager, 0, sizeof g_pager);
        g_pager.picks = g_pager.items = picks;
        g_pager.per_page = GAME_LIST_MAX - LIST_SPARE - added;
        g_pager.in_record = picks < g_pager.per_page ? picks : g_pager.per_page;
        g_pager.pages = (picks + g_pager.per_page - 1) / g_pager.per_page;
        g_pager.paged = 1;
        for (uint32_t i = 0; i < after; i++)
        {
            const uint8_t* e = record + LOADOUT_SLOTS + i * LOADOUT_SLOT;
            if (pick_of[i] < 0 && g_pager.fixed < GAME_LIST_MAX) g_pager.fixed_types[g_pager.fixed++] = Get32(e);
            if (pick_of[i] >= 0)
            {
                memcpy(g_pager.fresh[pick_of[i]], e, LOADOUT_SLOT);
                memcpy(g_pager.state[pick_of[i]], e, LOADOUT_SLOT);
                if ((uint32_t)pick_of[i] >= g_pager.per_page) continue;
                g_pager.shown[pick_of[i]] = 1;
            }
            memcpy(out[count++], e, LOADOUT_SLOT);
        }
        g_pager.active = 1;
        LOG_ONCE(LOG_PAGE, "record %p: %u picks and %u the game added (types %s): %u pages of %u picks (local player "
                 "id %llu)", (void*)record, picks, added, added_list, g_pager.pages, g_pager.per_page,
                 (unsigned long long)LocalPeer());
    }
    else
    {
        /* drop the last picks (past the game's four) until it fits, then the end */
        uint32_t over = after - GAME_LIST_MAX, dropped = 0;
        for (uint32_t i = after; i-- > 0 && dropped < over;)
            if (pick_of[i] >= (int)GAME_SLOTS) { pick_of[i] = -2; dropped++; }
        for (uint32_t i = 0; i < after && count < GAME_LIST_MAX; i++)
            if (pick_of[i] != -2) memcpy(out[count++], record + LOADOUT_SLOTS + i * LOADOUT_SLOT, LOADOUT_SLOT);
        LOG_ONCE(LOG_GUARD, "record %p: %u picks (%u found again) and %u the game added (types %s): no room for pages; "
                 "left out: %u extra pick(s), %u kept", (void*)record, picks, matched, added, added_list, dropped, count);
    }
    memcpy(record + LOADOUT_SLOTS, out, (size_t)count * LOADOUT_SLOT);
    memset(record + LOADOUT_SLOTS + count * LOADOUT_SLOT, 0, (size_t)(after - count) * LOADOUT_SLOT);
    Put32(record + LOADOUT_COUNT, count);
}

/* A reset of the local player's list (its count drops; the game fills it again from the record after) starts the
 * pages over too: fresh uses and cooldowns, page 1, which is what the record holds. */
static uint64_t Reset(uint64_t unused, uint32_t peer, uint8_t preset, uint32_t group)
{
    const uint8_t* list = g_pager.active ? LocalList() : NULL;
    uint32_t before = list != NULL ? Get32(list + LOADOUT_COUNT) : 0;
    uint64_t result = g_reset(unused, peer, preset, group);
    if (list != NULL && Get32(list + LOADOUT_COUNT) < before)
    {
        memcpy(g_pager.state, g_pager.fresh, sizeof g_pager.state);
        memset(g_pager.gone, 0, sizeof g_pager.gone);
        g_pager.page = 0;
        /* the entries taken in are not in the record: they go, and whoever added them adds them again or not */
        g_pager.items = g_pager.picks;
        if (g_pager.in_record != 0)
        {
            g_pager.per_page = g_pager.in_record;
            g_pager.pages = (g_pager.picks + g_pager.per_page - 1) / g_pager.per_page;
        }
        for (uint32_t k = 0; k < MAX_ITEMS; k++) g_pager.shown[k] = k < g_pager.in_record;
        LOG_ONCE(LOG_PAGE, "pages: the local list was reset (%u before); back to page 1", before);
    }
    return result;
}

/* After a mission the lobby takes each player's loadout back from their in-mission list: the local one gets all its
 * picks again (not the page shown plus the game's own), so the loadout screen and the profile keep every slot. */
static uint8_t* CopyLoadout(uint64_t unused, uint8_t* out, uint64_t peer)
{
    uint8_t* result = g_copy_loadout(unused, out, peer);
    if (!g_pager.active || !g_pager.paged || out == NULL || out[0] == 0 || peer != LocalPeer()) return result;
    uint8_t* record = out + 8;
    uint32_t n = Get32(record + LOADOUT_COUNT);
    if (n > LOADOUT_MAX) return result;
    for (uint32_t k = 0; k < g_pager.picks; k++)
        memcpy(record + LOADOUT_SLOTS + k * LOADOUT_SLOT, g_pager.fresh[k], LOADOUT_SLOT);
    if (n > g_pager.picks)
        memset(record + LOADOUT_SLOTS + g_pager.picks * LOADOUT_SLOT, 0, (size_t)(n - g_pager.picks) * LOADOUT_SLOT);
    Put32(record + LOADOUT_COUNT, g_pager.picks);
    /* the game copies it often during a mission too (lookups by type: every pick is there, with its fresh uses) */
    LOG_ONCE(LOG_COPY, "loadout copied from the mission: %u entries there, the %u picks put in its place", n,
             g_pager.picks);
    return result;
}

/* The mission's packages are requested for the list's types when it loads: with every page in the list for that
 * moment, all of them load (and the copy the release walks later holds them all). */
static void LoadAssets(uint8_t* entry, uint32_t stage, uint8_t ack, uint64_t by, uint32_t extra)
{
    uint8_t* list = entry != NULL ? entry + HISTORY_LIST : NULL;
    if (list == NULL || !g_pager.active || Get64(entry) != LocalPeer())
    {
        g_load_assets(entry, stage, ack, by, extra);
        return;
    }
    uint32_t n = Get32(list + LOADOUT_COUNT), count = n;
    if (n > GAME_LIST_MAX)
    {
        g_load_assets(entry, stage, ack, by, extra);
        return;
    }
    for (uint32_t k = 0; k < g_pager.items && count < HISTORY_MAX; k++)
    {
        if (g_pager.gone[k] || g_pager.shown[k]) continue;
        memcpy(list + LOADOUT_SLOTS + count++ * LOADOUT_SLOT, g_pager.state[k], LOADOUT_SLOT);
    }
    Put32(list + LOADOUT_COUNT, count);
    g_load_assets(entry, stage, ack, by, extra);
    memset(list + LOADOUT_SLOTS + n * LOADOUT_SLOT, 0, (size_t)(count - n) * LOADOUT_SLOT);
    Put32(list + LOADOUT_COUNT, n);
    LOG_ONCE(LOG_PAGE, "player history load (stage %u): %u in the list, %u from the other pages with it", stage, n,
             count - n);
}

/* A stratagem in hand names its slot in the list as it was when its code went in. After a page change that slot may
 * hold another: point it at its type's entry, first changing to the page that holds it if it is not in the list. */
static uint64_t CallActivate(uint64_t unused, uint64_t peer, uint32_t* armed, void* where, uint8_t flag)
{
    int was = g_activating_local;
    g_activating_local = peer == LocalPeer();
    uint64_t result = g_activate(unused, peer, armed, where, flag);
    g_activating_local = was;
    return result;
}

static uint64_t Activate(uint64_t unused, uint64_t peer, uint32_t* armed, void* where, uint8_t flag)
{
    uint8_t* list = g_pager.active && armed != NULL && peer == LocalPeer() ? LocalList() : NULL;
    if (list == NULL) return CallActivate(unused, peer, armed, where, flag);
    uint32_t type = armed[0], slot = armed[1], n = Get32(list + LOADOUT_COUNT);
    const uint8_t* record;
    int in_hand = ArmedRow(&record);
    LOG_ONCE(LOG_ROWS, "activation: type %u, slot %u (now type %u there), page %u, in-hand record's row %d", type,
             slot + 1, slot < n && slot < GAME_LIST_MAX ? EntryType(list, slot) : 0, g_pager.page + 1, in_hand + 1);
    if (n > GAME_LIST_MAX || (slot < n && EntryType(list, slot) == type)) return CallActivate(unused, peer, armed, where, flag);

    uint32_t i = 0;
    while (i < n && EntryType(list, i) != type) i++;
    if (i == n) FitPages(list);
    if (i == n)
        for (uint32_t k = 0; k < g_pager.items; k++)
            if (!g_pager.gone[k] && Get32(g_pager.state[k]) == type && !g_pager.shown[k])
            {
                if (SwitchPage(k / g_pager.per_page, "a stratagem in hand", 1) > 0)
                {
                    n = Get32(list + LOADOUT_COUNT);
                    for (i = 0; i < n && EntryType(list, i) != type; i++) {}
                }
                break;
            }
    if (i >= n)
    {
        LOG_ONCE(LOG_PAGE, "activation: type %u (slot %u) is in no page; left to the game", type, slot + 1);
        return CallActivate(unused, peer, armed, where, flag);
    }
    LOG_ONCE(LOG_PAGE, "activation: type %u was slot %u, now %u", type, slot + 1, i + 1);
    armed[1] = i;
    uint64_t result = CallActivate(unused, peer, armed, where, flag);
    armed[1] = slot;
    return result;
}

static void ListOpen(uint8_t* hud, uint8_t open)
{
    g_list_open(hud, open);
    g_list_is_open = hud != NULL && hud[HUD_LIST_OPEN] != 0;
}

/* ---- Eagles ------------------------------------------------------------------------------------------------------ */

/* The game keeps its Eagles together only within the list: a cooldown starting on one Eagle (or on Eagle Rearm) is
 * given to the other Eagles there, and the rearm refills their uses. Eagles on another page get the same here, and
 * they count when the game asks whether any Eagle still has uses left (it starts the rearm by itself when none has) or
 * whether any is below its full uses. */

/* A type's info row from the table the game uses (HellForge moves it for stratagem mods), or NULL. */
static const uint8_t* Info(uint32_t type)
{
    if (type == 0 || type >= INFO_TYPES) return NULL;
    const uint8_t* lea = (const uint8_t*)(game + RVA_COOLDOWN_TABLE);
    int32_t disp;
    memcpy(&disp, lea + 3, 4);
    const uint8_t* const* table = (const uint8_t* const*)(lea + 7 + disp);
    return table[type];
}

static int IsEagle(uint32_t type)
{
    const uint8_t* info = Info(type);
    return info != NULL && Get32(info + INFO_LINKED) == TYPE_EAGLE_REARM;
}

static int OnPage(uint32_t k) { return k < MAX_ITEMS && g_pager.shown[k]; }

/* After the game started a cooldown on the local list's entry `slot`. */
static void MirrorEagles(uint32_t slot)
{
    const uint8_t* list = g_eagles_ok ? LocalList() : NULL;
    if (list == NULL || slot >= Get32(list + LOADOUT_COUNT) || slot >= GAME_LIST_MAX) return;
    const uint8_t* from = list + LOADOUT_SLOTS + slot * LOADOUT_SLOT;
    uint32_t type = Get32(from);
    int rearm = type == TYPE_EAGLE_REARM;
    if (!rearm && !IsEagle(type)) return;
    uint64_t now = Now(), end = Get64(from + ENTRY_COOLDOWN_END);
    uint32_t followed = 0;
    for (uint32_t k = 0; k < g_pager.items; k++)
    {
        uint32_t t = Get32(g_pager.state[k]);
        if (g_pager.gone[k] || OnPage(k) || t == type || !IsEagle(t)) continue;
        Put64(g_pager.state[k] + ENTRY_COOLDOWN_END, end);
        Put64(g_pager.state[k] + ENTRY_START, now);
        if (rearm) Put32(g_pager.state[k] + ENTRY_USES, Get32(g_pager.fresh[k] + ENTRY_USES));
        followed++;
    }
    if (followed == 0) return;
    LOG_ONCE(LOG_EAGLE, "%s (type %u, row %u): %u Eagle(s) on other pages follow, cooldown ends in %d ms",
             rearm ? "Eagle Rearm" : "Eagle", type, slot + 1, followed, (int)(((int64_t)end - (int64_t)now) / 1000));
    if (!rearm) return;
    /* the game refills the list's Eagles with uses it computes; the other pages' get their uses at ready-up */
    uint32_t n = Get32(list + LOADOUT_COUNT);
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t t = EntryType(list, i);
        if (!IsEagle(t)) continue;
        for (uint32_t k = 0; k < g_pager.items; k++)
            if (Get32(g_pager.fresh[k]) == t)
                LOG_ONCE(LOG_EAGLE, "Eagle Rearm: type %u in the list has %u uses, %u at ready-up", t,
                         Get32(list + LOADOUT_SLOTS + i * LOADOUT_SLOT + ENTRY_USES),
                         Get32(g_pager.fresh[k] + ENTRY_USES));
    }
}

static void Cooldown(uint8_t* history, uint64_t peer, uint32_t slot, uint8_t set_start, float seconds)
{
    g_cooldown(history, peer, slot, set_start, seconds);
    if (!g_eagles_ok || !g_pager.active || peer != LocalPeer()) return;
    const uint8_t* list = LocalList();
    if (list != NULL && slot < Get32(list + LOADOUT_COUNT) && slot < GAME_LIST_MAX)
    {
        const uint8_t* e = list + LOADOUT_SLOTS + slot * LOADOUT_SLOT;
        LOG_ONCE(LOG_ROWS, "used: type %u in row %u (page %u), its call-in ends in %d ms", Get32(e), slot + 1,
                 g_pager.page + 1, (int)(((int64_t)Get64(e + ENTRY_CALL_END) - (int64_t)Now()) / 1000));
    }
    MirrorEagles(slot);
}

static void CooldownSystems(uint64_t peer, uint32_t slot, uint8_t set_start, float seconds)
{
    g_cooldown_systems(peer, slot, set_start, seconds);
    if (g_pager.active && peer == LocalPeer()) MirrorEagles(slot);
}

/* An Eagle on another page with uses left. */
static int EagleLeft(int below_full)
{
    for (uint32_t k = 0; k < g_pager.items && g_eagles_ok; k++)
    {
        if (g_pager.gone[k] || OnPage(k) || !IsEagle(Get32(g_pager.state[k]))) continue;
        uint32_t uses = Get32(g_pager.state[k] + ENTRY_USES);
        if (below_full ? uses < Get32(g_pager.fresh[k] + ENTRY_USES) : uses != 0) return 1;
    }
    return 0;
}

/* Asked by the local player's activation (its call that starts the rearm by itself), or by effects. Only the first is
 * answered for the pages: the player's u32 id is not mapped here (the game's own mapping reads out of bounds for an id
 * it doesn't know). */
static uint8_t EaglesSpent(uint8_t* unused, uint32_t player)
{
    uint8_t spent = g_eagles_spent(unused, player);
    if (spent && g_pager.active && g_activating_local && EagleLeft(0))
    {
        LOG_ONCE(LOG_EAGLE, "the list's Eagles are spent, but not those on other pages: no rearm by itself yet");
        return 0;
    }
    return spent;
}

static uint8_t EaglesUsed(uint8_t* unused, uint8_t* list)
{
    uint8_t used = g_eagles_used(unused, list);
    if (!used && g_pager.active && list != NULL && list == LocalList() && EagleLeft(1)) return 1;
    return used;
}

/* ---- the page label ---------------------------------------------------------------------------------------------- */

/* "page/pages" at the top right of the in-mission list, while it is open and the picks are in pages. A text node of
 * the game's own kind, made the way a row makes its name (same font, colour, alpha and layer) and given its text the
 * way the row update gives a name, as a child of the list's first row: anchored and pivoted at the top right, so it
 * sits on the row's top right corner, LABEL_Y above it. All of it happens in the list's own update, on its thread.
 * The UI's y grows upwards. */
#define LABEL_X 0.0f
#define LABEL_Y 26.0f
#define LABEL_ALPHA 0.7f          /* the row names' (0x1835FEB) */
#define LABEL_LISTS 8u

typedef struct { uint8_t* list; uint8_t* node; int logged; } Label;
static Label g_labels[LABEL_LISTS];
static uint32_t g_label_next;
static int g_label_ok;            /* the label's code matched */

static int Linked(const uint8_t* parent, const uint8_t* node)
{
    const uint8_t* child = *(uint8_t* const*)(parent + NODE_FIRST_CHILD);
    for (uint32_t steps = 0; child != NULL && steps < 256; steps++, child = *(uint8_t* const*)(child + NODE_NEXT))
        if (child == node) return 1;
    return 0;
}

static Label* FindLabel(const uint8_t* list)
{
    for (uint32_t i = 0; i < LABEL_LISTS; i++)
        if (g_labels[i].list == list) return &g_labels[i];
    return NULL;
}

/* The label of this list, made (again) when it is not in the first row's children: the first time, and after the list
 * is built again (a row's init empties its children). A list at a new address gets a new node, and an old node is
 * never freed or reused for another list: an old list still alive may still have it in its children. */
static uint8_t* LabelOf(uint8_t* list)
{
    uint8_t* row0 = list + HUD_ROW0;
    if (*(uint8_t* const*)(row0 + NODE_CONTEXT) == NULL) return NULL;
    Label* l = FindLabel(list);
    if (l == NULL)
    {
        uint8_t* node = (uint8_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, TEXT_NODE_SIZE);
        if (node == NULL) return NULL;
        l = &g_labels[g_label_next++ % LABEL_LISTS];
        l->list = list;
        l->node = node;
        l->logged = 0;
    }
    uint8_t* node = l->node;
    if (*(uint8_t* const*)(node + NODE_PARENT) == row0 && Linked(row0, node)) return node;

    static const float kColor[3] = { 1.0f, 0.984f, 0.847f }; /* the row names' (0x21E2D20, y z w) */
    memset(node, 0, TEXT_NODE_SIZE);
    text_init(node);
    memcpy(node + NODE_CONTEXT, row0 + NODE_CONTEXT, 8); /* the font comes from the context: the list's */
    text_font(node, TEXT_STYLE_ROW, 1, 1, 1.0f, 0);
    node_set_position(node, Vec2(LABEL_X, LABEL_Y));
    node_set_pivot(node, Vec2(1.0f, 1.0f));
    node_set_anchor(node, Vec2(1.0f, 1.0f));
    node_set_alpha(node, LABEL_ALPHA);
    node_set_color(node, kColor);
    node_set_layer(node, LAYER_ROW_TEXT);
    node_set_visible(node, 0);
    node_add_child(row0, node, 0);
    l->logged = 0;
    LOG_ONCE(LOG_LABEL, "page label made: list %p, first row %p, node %p, context %p", (void*)list, (void*)row0,
             (void*)node, *(void* const*)(row0 + NODE_CONTEXT));
    return node;
}

static void LabelText(uint8_t* node, const char* text)
{
    char* now = (char*)node + TEXT_BUFFER;
    if (strcmp(now, text) == 0) return;
    lstrcpynA(now, text, TEXT_MAX);
    uint64_t cache = Get64(node + TEXT_CACHE);
    if (cache != 0)
    {
        text_release(0, cache);
        memset(node + TEXT_CACHE, 0, 8);
    }
    text_changed(node);
    node_layout(node);
}

/* For the test of a call-in across a page change: for some seconds after a change, each HUD row's entry index, type,
 * state (3: being called in or flashing after), flash time and call-in time left, logged when its type or state
 * changes. */
#define ROW_STRIDE 14176u
#define ROW_INDEX 14152u          /* u32: the list entry it shows */
#define ROW_TYPE 14156u
#define ROW_STATE 14168u
#define ROW_FLASH 14084u          /* float seconds */
#define ROW_CALL_LEFT 14104u      /* float seconds */
#define HUD_ROWS 16u

static void LogRows(const uint8_t* list)
{
    static uint32_t last[HUD_ROWS][2];
    static uint64_t window;
    uint64_t now = Now();
    if (now == 0 || now >= g_view_until) return;
    int first = window != g_view_until; /* the first frame after a change: every row of the list */
    window = g_view_until;
    const uint8_t* local = LocalList();
    uint32_t count = local != NULL ? Get32(local + LOADOUT_COUNT) : HUD_ROWS;
    if (first)
    {
        const uint8_t* record;
        LOG_ONCE(LOG_ROWS, "HUD after the change: in-hand record's row %d, list +227100 %d, +227104 %d", ArmedRow(&record) + 1,
                 (int)Get32(list + 227100u), (int)Get32(list + 227104u));
    }
    for (uint32_t r = 0; r < HUD_ROWS && r < count; r++)
    {
        const uint8_t* row = list + HUD_ROW0 + r * ROW_STRIDE;
        uint32_t type = Get32(row + ROW_TYPE), state = Get32(row + ROW_STATE);
        float flash, left;
        memcpy(&flash, row + ROW_FLASH, 4);
        memcpy(&left, row + ROW_CALL_LEFT, 4);
        int changed = last[r][0] != type || last[r][1] != state;
        last[r][0] = type;
        last[r][1] = state;
        if (!first && !changed) continue;
        LOG_ONCE(LOG_ROWS, "HUD row %u: entry %u, type %u, state %u, flash %d ms, call-in left %d ms (page %u)", r + 1,
                 Get32(row + ROW_INDEX) + 1, type, state, (int)(flash * 1000.0f), (int)(left * 1000.0f),
                 g_pager.page + 1);
    }
}

static void ListUpdate(uint8_t* list, float a, uint32_t b, float c)
{
    g_list_update(list, a, b, c);
    if (list != NULL && list[HUD_LIST_OPEN] != 0) InterlockedExchange64(&g_list_seen, (LONG64)GetTickCount64());
    if (list != NULL && g_pager.active) LogRows(list);
    if (list == NULL || !g_label_ok) return;
    int show = g_pager.active && g_pager.pages >= 2 && list[HUD_LIST_OPEN] != 0;
    if (!show)
    {
        Label* l = FindLabel(list);
        if (l != NULL && Get64(l->node + NODE_PARENT) == (uint64_t)(uintptr_t)(list + HUD_ROW0))
            node_set_visible(l->node, 0);
        return;
    }
    uint8_t* node = LabelOf(list);
    if (node == NULL) return;
    char text[24];
    wsprintfA(text, "%u/%u", g_pager.page + 1, g_pager.pages);
    LabelText(node, text);
    node_set_visible(node, 1);
    Label* l = FindLabel(list);
    if (l != NULL && l->logged < 2)
    {
        /* the second frame too: the size comes from the layout */
        l->logged++;
        float size[2], row[2];
        memcpy(size, node + NODE_SIZE_X, 8);
        memcpy(row, list + HUD_ROW0 + NODE_SIZE_X, 8);
        LOG_ONCE(LOG_LABEL, "page label shows \"%s\": its size %d x %d, the first row's %d x %d", text, (int)size[0],
                 (int)size[1], (int)row[0], (int)row[1]);
    }
}

static int GameFocused(void)
{
    DWORD pid = 0;
    HWND window = GetForegroundWindow();
    if (window != NULL) GetWindowThreadProcessId(window, &pid);
    return pid == GetCurrentProcessId();
}

/* ---- the mouse wheel --------------------------------------------------------------------------------------------- */

/* GetAsyncKeyState has no wheel, and the mod has no way into the game's own input on its update, so a low-level mouse
 * hook on a thread of the mod's own sees the wheel. While the list is open with pages and the game has focus, each
 * notch (120) counts a step, up = previous page, and the wheel message goes no further (best effort: a game reading
 * raw input may still see it). The game update takes the steps. "Open" expires by itself (g_wheel_until, renewed by
 * every game update that sees the list open and, with the label's hook, drawn), so the wheel is never kept from
 * anything else if the game stops updating or the list goes away without closing. */
#define WHEEL_LIVE_MS 250u
static volatile LONG64 g_wheel_until; /* GetTickCount64 */
static volatile LONG g_wheel_steps;   /* + next, - previous */
static HHOOK g_mouse_hook;
static HANDLE g_wheel_thread;
static DWORD g_wheel_thread_id;

static LRESULT CALLBACK MouseHook(int code, WPARAM message, LPARAM data)
{
    static int rest; /* a high-resolution wheel's part of a notch */
    if (code == HC_ACTION && message == WM_MOUSEWHEEL)
    {
        if (GetTickCount64() >= (ULONGLONG)g_wheel_until || !GameFocused()) rest = 0;
        else
        {
            rest += (short)HIWORD(((const MSLLHOOKSTRUCT*)data)->mouseData);
            for (; rest >= WHEEL_DELTA; rest -= WHEEL_DELTA) InterlockedDecrement(&g_wheel_steps);
            for (; rest <= -WHEEL_DELTA; rest += WHEEL_DELTA) InterlockedIncrement(&g_wheel_steps);
            return 1;
        }
    }
    return CallNextHookEx(NULL, code, message, data);
}

static DWORD WINAPI WheelThread(void* unused)
{
    (void)unused;
    HMODULE module = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(void*)MouseHook, &module);
    MSG msg;
    PeekMessageW(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE); /* the thread's message queue, for WM_QUIT */
    g_mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, module, 0);
    if (g_mouse_hook == NULL)
    {
        LOG_ONCE(LOG_WHEEL, "mouse wheel: no mouse hook (error %lu); the page keys still work", GetLastError());
        return 0;
    }
    while (GetMessageW(&msg, NULL, 0, 0) > 0) DispatchMessageW(&msg);
    UnhookWindowsHookEx(g_mouse_hook);
    g_mouse_hook = NULL;
    return 0;
}

static void StartWheel(void)
{
    if (!g_wheel_on || g_wheel_thread != NULL) return;
    g_wheel_thread = CreateThread(NULL, 0, WheelThread, NULL, 0, &g_wheel_thread_id);
    if (g_wheel_thread == NULL) hf->log(self, "mouse wheel: no thread (error %lu); the page keys still work", GetLastError());
}

static void StopWheel(void)
{
    InterlockedExchange64(&g_wheel_until, 0);
    if (g_wheel_thread == NULL) return;
    PostThreadMessageW(g_wheel_thread_id, WM_QUIT, 0, 0);
    CloseHandle(g_wheel_thread); /* not waited for: the DLL stays loaded, and the hook goes when the thread ends */
    g_wheel_thread = NULL;
}

/* ---- every game update ------------------------------------------------------------------------------------------- */

/* The page keys and the wheel, while the list is open. */
static void Tick(HfMod* mod, float dt, void* user)
{
    (void)mod; (void)dt; (void)user;
    static int was_prev, was_next;
    int prev = (GetAsyncKeyState((int)g_key_prev) & 0x8000) != 0, next = (GetAsyncKeyState((int)g_key_next) & 0x8000) != 0;
    int press_prev = prev && !was_prev, press_next = next && !was_next;
    was_prev = prev;
    was_next = next;
    ULONGLONG ms = GetTickCount64();
    /* What other mods (or the mission) added to the list since the last update: counted, taken into the pages, and if
     * the list is then fuller than a page allows, the page is put in again so the rows left free are free again (the
     * next addition needs them). Held back like any page change while a stratagem is coming in. */
    if (g_pager.active && hf->game_state(self) == HF_STATE_MISSION)
    {
        const uint8_t* now = LocalList();
        uint32_t n = now != NULL ? Get32(now + LOADOUT_COUNT) : 0;
        if (now != NULL && !g_pager.checked && n >= g_pager.in_record && n <= GAME_LIST_MAX)
        {
            /* the record the pages were set up from must be this player's: its picks are in this player's list */
            uint8_t taken[GAME_LIST_MAX] = { 0 };
            uint32_t seen = 0;
            for (uint32_t k = 0; k < g_pager.in_record; k++)
            {
                uint32_t type = Get32(g_pager.state[k]), i = 0;
                while (i < n && (taken[i] || EntryType(now, i) != type)) i++;
                if (i < n) { taken[i] = 1; seen++; }
            }
            g_pager.checked = 1;
            if (seen < g_pager.in_record)
            {
                LOG_ONCE(LOG_PAGE, "pages: only %u of the record's %u picks are in the local list (another player's "
                         "record?): no pages this mission", seen, g_pager.in_record);
                g_pager.active = 0;
            }
        }
        if (now != NULL && g_pager.active && g_pager.checked && FitPages(now) > g_pager.per_page)
        {
            g_refitting = 1;
            SwitchPage(g_pager.page, "room in the list", 0);
            g_refitting = 0;
        }
    }
    int live = g_pager.active && g_pager.pages >= 2 && g_list_is_open &&
               (!g_label_ok || ms - (ULONGLONG)g_list_seen < WHEEL_LIVE_MS) && GameFocused();
    InterlockedExchange64(&g_wheel_until, live ? (LONG64)(ms + WHEEL_LIVE_MS) : 0);
    LONG wheel = InterlockedExchange(&g_wheel_steps, 0);
    if (!live) return;
    int step = press_next ? 1 : press_prev ? -1 : wheel > 0 ? 1 : wheel < 0 ? -1 : 0; /* a step per update */
    if (step == 0) return;
    if (!press_next && !press_prev && (wheel > 1 || wheel < -1))
        InterlockedExchangeAdd(&g_wheel_steps, wheel - step); /* the rest for the next updates */
    const char* why = press_next ? "next key" : press_prev ? "previous key" : step > 0 ? "wheel down" : "wheel up";
    uint32_t to = step > 0 ? (g_pager.page + 1) % g_pager.pages : (g_pager.page + g_pager.pages - 1) % g_pager.pages;
    if (SwitchPage(to, why, 0) > 0) play_sound(0, SOUND_MOVE);
    else InterlockedExchange(&g_wheel_steps, 0); /* held back: drop the rest of the wheel's steps */
}

/* ---- the profile ------------------------------------------------------------------------------------------------- */

/* At boot the game loads the loadout from the profile, but stops once the slots' cost comes to 4: the extra slots,
 * saved with the rest, came back empty after a restart. The profile's pairs are in slot order, so after the game's
 * load the picks are filled again from all of them, up to the slot setting, the way the game reads a pair (uses 0 is
 * an empty pair, an unknown stable id is skipped). Only when the game's own picks are the first of those: anything
 * else is a choice of the game's this mod does not know, and it is left as it is.
 *
 * The game calls the load from five places. Three hand a copy of the result straight to the player history
 * (0x13620A0: host start, join, the end of the profile's loading), whose lists take 16 and no more: for those, and
 * for a caller this mod does not know, the picks stop at 16. Only the two that keep the loadout get every slot. (A
 * hook on the player history did this before; it changed code that HD2 Transmog checks, which switched its Custom
 * Variants off.) */
static uint8_t* ProfileLoad(uint8_t* record)
{
    uintptr_t from = (uintptr_t)__builtin_return_address(0) - (uintptr_t)game;
    uint8_t* result = g_profile_load(record);
    const uint8_t* profile = *(const uint8_t* const*)(game + RVA_G_PROFILE);
    if (record == NULL || profile == NULL || profile[PROFILE_HAS_LOADOUT] == 0) return result;
    uint32_t loaded = Get32(record + LOADOUT_COUNT);
    if (loaded > LOADOUT_MAX) return result;

    uint32_t limit = g_slots;
    if (limit > GAME_LIST_MAX)
    {
        if (from == RVA_PROFILE_KEEP_A || from == RVA_PROFILE_KEEP_B)
            LOG_ONCE(LOG_PROFILE, "profile load for 0x%llX: every slot", (unsigned long long)from);
        else
        {
            limit = GAME_LIST_MAX;
            LOG_ONCE(LOG_PROFILE, "profile load for 0x%llX: the first 16 picks at most (the player history's limit)",
                     (unsigned long long)from);
        }
    }

    uint32_t types[MAX_SLOTS], uses[MAX_SLOTS], n = 0;
    for (uint32_t i = 0; i < PROFILE_PAIR_COUNT && n < limit; i++)
    {
        const uint8_t* pair = profile + PROFILE_PAIRS + i * 8u;
        uint32_t stable = Get32(pair), use = Get32(pair + 4);
        if (use == 0) continue;
        uint32_t type = stable_to_type(stable);
        if (type == 0) continue;
        types[n] = type;
        uses[n++] = use;
    }
    if (n <= loaded) return result;

    char game_list[LOADOUT_MAX * 12];
    TypeList(record, 0, loaded, game_list, sizeof game_list);
    for (uint32_t j = 0; j < loaded; j++)
        if (Get32(record + LOADOUT_SLOTS + j * LOADOUT_SLOT) != types[j])
        {
            LOG_ONCE(LOG_PROFILE, "profile load: the game loaded types %s, not the profile's first %u in order; left as "
                     "the game loaded it", game_list, loaded);
            return result;
        }
    for (uint32_t j = loaded; j < n; j++)
    {
        uint8_t* entry = record + LOADOUT_SLOTS + j * LOADOUT_SLOT;
        memset(entry, 0, LOADOUT_SLOT);
        Put32(entry, types[j]);
        Put32(entry + 4, uses[j]);
    }
    Put32(record + LOADOUT_COUNT, n);
    char added[LOADOUT_MAX * 12];
    TypeList(record, loaded, n, added, sizeof added);
    LOG_ONCE(LOG_PROFILE, "profile load: the game loaded %u (types %s); added the profile's next %u (types %s)", loaded,
             game_list, n - loaded, added);
    return result;
}

/* ---- settings ---------------------------------------------------------------------------------------------------- */

static uint32_t Clamp(uint32_t v, uint32_t lo, uint32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The settings file is <installation>\mods\more-stratagem-slots.ini, beside the mod and not in its folder: the folder's
 * files are what co-op compares (an edited setting would make it another mod), a zip install has no folder a player
 * can edit, and an update replaces the folder. Written with the defaults below when missing; they match the variables'
 * own initial values. */
#define SETTINGS_NAME "more-stratagem-slots.ini"

static const char kDefaultSettings[] =
    "# More Stratagem Slots settings. Read once when the game starts.\r\n"
    "\r\n"
    "# Stratagem slots in the loadout, 4 to 20.\r\n"
    "# In a mission the game's stratagem list (the one you see holding the stratagem key) holds 16 in all, its own\r\n"
    "# included (Reinforce, SOS Beacon, Resupply, mission stratagems, Eagle Rearm). When your picks don't fit with those,\r\n"
    "# they go in pages: page 1 is slots 1 to k, page 2 the next k, and so on (k = 14 minus the game's own, usually 10 or\r\n"
    "# so; two rows stay free for what the mission grants). Only the page shown can be called in. Each stratagem keeps its\r\n"
    "# uses and cooldown while it is on another page. Eagles on every page share their cooldown and Eagle Rearm, as they\r\n"
    "# do in the list. While a stratagem is being called in, the page doesn't change (its row would show the next page's\r\n"
    "# stratagem as the one coming in); try again after.\r\n"
    "slots = 8\r\n"
    "\r\n"
    "# Slots per row on the loadout card, at least 4. The card grows upwards by a row for each row of extra slots: slots\r\n"
    "# 1-4 stay the top row, the rest go in rows under them. With slots <= per_row they are all one row instead, the extra\r\n"
    "# ones after the booster box (past the card's right edge, where the game cuts them off).\r\n"
    "per_row = 4\r\n"
    "\r\n"
    "# Page keys, while the stratagem list is open: one letter or digit, or a Windows virtual-key code (two digits or\r\n"
    "# more).\r\n"
    "page_prev = Q\r\n"
    "page_next = E\r\n"
    "\r\n"
    "# The mouse wheel changes pages too while the list is open: up = previous page, down = next. While it does, the mod\r\n"
    "# tries to keep the wheel from the game (on or off).\r\n"
    "page_wheel = on\r\n"
    "\r\n"
    "# While a stratagem of the page shown is being called in (or is in hand), the page doesn't change (on). With off it\r\n"
    "# changes at once: the stratagem still comes in, but until it lands its row shows the new page's stratagem as the\r\n"
    "# one coming in.\r\n"
    "page_hold = on\r\n"
    "\r\n"
    "# Stratagems that other mods add to your list during a mission (P.S.S. extras, for example) go in the pages after\r\n"
    "# your own (on). With off they stay on every page, which leaves less room for yours on each page. The mission's own\r\n"
    "# (Reinforce, Resupply, SOS Beacon, objective stratagems) are on every page either way.\r\n"
    "page_added = on\r\n";

/* The file's path: the mod's folder is mods\<folder>, or deeper for a zip (mods\.unpacked\<zip>). */
static int SettingsPath(char* path, size_t size, wchar_t* wide, int wide_count)
{
    const char* folder = hf->mod_folder(self);
    if (folder == NULL || strlen(folder) + sizeof SETTINGS_NAME + 2 > size) return 0;
    strcpy(path, folder);
    char* mods = NULL;
    for (char* p = path; *p; p++)
    {
        if (*p == '/') *p = '\\';
        if (*p == '\\' && _strnicmp(p + 1, "mods", 4) == 0 && (p[5] == '\\' || p[5] == '/')) mods = p;
    }
    if (mods != NULL) mods[5] = 0; /* the last "\mods\" in the path: keep "...\mods" */
    else
    {
        char* slash = strrchr(path, '\\');
        if (slash == NULL) return 0;
        *slash = 0; /* the folder's parent */
    }
    strcat(path, "\\" SETTINGS_NAME);
    return MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, wide_count) != 0;
}

static void ReadSettings(void)
{
    char path[1024];
    wchar_t wide[1024];
    if (!SettingsPath(path, sizeof path, wide, 1024))
    {
        hf->log(self, "no path for " SETTINGS_NAME "; using %u slots, %u per row", g_slots, g_per_row);
        return;
    }
    HANDLE file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        int wrote = 0;
        if (error == ERROR_FILE_NOT_FOUND)
        {
            file = CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (file != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                wrote = WriteFile(file, kDefaultSettings, sizeof kDefaultSettings - 1, &written, NULL) &&
                        written == sizeof kDefaultSettings - 1;
                CloseHandle(file);
            }
        }
        if (wrote) hf->log(self, "wrote %s with the defaults: %u slots, %u per row", path, g_slots, g_per_row);
        else
            hf->log(self, "%s could not be %s (error %u); using the defaults: %u slots, %u per row", path,
                    error == ERROR_FILE_NOT_FOUND ? "written" : "opened",
                    (unsigned)(error == ERROR_FILE_NOT_FOUND ? GetLastError() : error), g_slots, g_per_row);
        return;
    }
    char text[4096];
    DWORD size = 0;
    BOOL ok = ReadFile(file, text, sizeof text - 1, &size, NULL);
    CloseHandle(file);
    if (!ok) return;
    text[size] = 0;

    for (char* line = text; line != NULL && *line;)
    {
        char* end = strchr(line, '\n');
        if (end != NULL) *end = 0;
        while (*line == ' ' || *line == '\t') line++;
        char* eq = strchr(line, '=');
        if (*line != '#' && *line != ';' && eq != NULL)
        {
            char* key_end = eq;
            while (key_end > line && (key_end[-1] == ' ' || key_end[-1] == '\t')) key_end--;
            size_t key_len = (size_t)(key_end - line);
            /* page keys: one letter or digit (its key), or a virtual-key code of two digits or more */
            int is_prev = key_len == 9 && memcmp(line, "page_prev", 9) == 0;
            int is_next = key_len == 9 && memcmp(line, "page_next", 9) == 0;
            if (is_prev || is_next)
            {
                const char* v = eq + 1;
                while (*v == ' ' || *v == '\t') v++;
                uint32_t code = 0;
                int one = ((*v >= 'A' && *v <= 'Z') || (*v >= 'a' && *v <= 'z') || (*v >= '0' && *v <= '9')) &&
                          (v[1] == 0 || v[1] == ' ' || v[1] == '\t' || v[1] == '\r' || v[1] == '#');
                if (one) code = (uint32_t)(*v >= 'a' && *v <= 'z' ? *v - 'a' + 'A' : *v);
                else
                    for (; *v >= '0' && *v <= '9' && code < 256; v++) code = code * 10 + (uint32_t)(*v - '0');
                if (code > 0 && code < 256) *(is_prev ? &g_key_prev : &g_key_next) = code;
                line = end != NULL ? end + 1 : NULL;
                continue;
            }
            int is_wheel = key_len == 10 && memcmp(line, "page_wheel", 10) == 0;
            int is_hold = key_len == 9 && memcmp(line, "page_hold", 9) == 0;
            int is_added = key_len == 10 && memcmp(line, "page_added", 10) == 0;
            if (is_wheel || is_hold || is_added)
            {
                const char* v = eq + 1;
                while (*v == ' ' || *v == '\t') v++;
                *(is_wheel ? &g_wheel_on : is_hold ? &g_page_hold : &g_page_added) = !(_strnicmp(v, "off", 3) == 0 || _strnicmp(v, "no", 2) == 0 ||
                                                             _strnicmp(v, "false", 5) == 0 || *v == '0');
                line = end != NULL ? end + 1 : NULL;
                continue;
            }
            uint32_t value = 0;
            int digits = 0;
            for (const char* v = eq + 1; *v; v++)
            {
                if (*v >= '0' && *v <= '9') { value = value * 10 + (uint32_t)(*v - '0'); digits = 1; if (value > 1000) break; }
                else if (digits || (*v != ' ' && *v != '\t')) break;
            }
            if (digits && key_len == 5 && memcmp(line, "slots", 5) == 0) g_slots = value;
            else if (digits && key_len == 7 && memcmp(line, "per_row", 7) == 0) g_per_row = value;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    hf->log(self, "read %s: slots = %u, per_row = %u (before limits), page keys 0x%02X / 0x%02X, page_wheel %s, "
            "page_hold %s, page_added %s", path, g_slots, g_per_row, g_key_prev, g_key_next, g_wheel_on ? "on" : "off",
            g_page_hold ? "on" : "off", g_page_added ? "on" : "off");
}

/* ---- start ------------------------------------------------------------------------------------------------------- */

static void GameLoaded(HfMod* mod, HfEvent event, void* user)
{
    (void)event; (void)user;
    game = hf->module_base("game.dll", NULL);
    if (game == 0) return;
    for (uint32_t i = 0; i < CODE_COUNT; i++)
        if (!hf->code_matches(game + kCode[i].rva, kCode[i].pattern))
        {
            hf->log(mod, "the game's %s (0x%X) is not the code this mod knows (another game build?); nothing changed",
                    kCode[i].name, kCode[i].rva);
            return;
        }
    widget_init = (WidgetInitFn)(game + RVA_WIDGET_INIT);
    widget_set_type = (WidgetSetTypeFn)(game + RVA_WIDGET_SET_TYPE);
    node_set_position = (NodeVec2Fn)(game + RVA_NODE_SET_POSITION);
    node_set_pivot = (NodeVec2Fn)(game + RVA_NODE_SET_PIVOT);
    node_set_anchor = (NodeVec2Fn)(game + RVA_NODE_SET_ANCHOR);
    node_add_child = (NodeAddChildFn)(game + RVA_NODE_ADD_CHILD);
    node_set_size = (NodeVec2Fn)(game + RVA_NODE_SET_SIZE);
    special_mode = (SpecialModeFn)(game + RVA_SPECIAL_MODE);
    stable_to_type = (StableToTypeFn)(game + RVA_STABLE_TO_TYPE);
    play_sound = (PlaySoundFn)(game + RVA_PLAY_SOUND);
    input_index = (InputIndexFn)(game + RVA_INPUT_INDEX);
    nav_dir = (NavDirFn)(game + RVA_NAV_DIR);
    cursor_over = (CursorOverFn)(game + RVA_CURSOR_OVER);
    apply_state = (ApplyStateFn)(game + RVA_APPLY_STATE);
    is_hovered = (IsHoveredFn)(game + RVA_IS_HOVERED);

    /* The guards on the game's limit of 16 first, then the commit, so the record keeps its extra slots from the first
     * refresh on. */
    static const struct { uint32_t rva; void* detour; void** original; } hooks[] = {
        { RVA_AUGMENT, (void*)Augment, (void**)&g_augment },
        { RVA_RESET, (void*)Reset, (void**)&g_reset },
        { RVA_LOAD_ASSETS, (void*)LoadAssets, (void**)&g_load_assets },
        { RVA_ACTIVATE, (void*)Activate, (void**)&g_activate },
        { RVA_LIST_OPEN, (void*)ListOpen, (void**)&g_list_open },
        { RVA_COPY_LOADOUT, (void*)CopyLoadout, (void**)&g_copy_loadout },
        { RVA_PROFILE_LOAD, (void*)ProfileLoad, (void**)&g_profile_load },
        { RVA_COMMIT, (void*)Commit, (void**)&g_commit },
        { RVA_BUILD_PANEL, (void*)BuildPanel, (void**)&g_build_panel },
        { RVA_REFRESH, (void*)Refresh, (void**)&g_refresh },
        { RVA_SET_FOCUS, (void*)HookSetFocus, (void**)&g_set_focus },
        { RVA_SET_SLOT, (void*)SetSlot, (void**)&g_set_slot },
        { RVA_PICKER_INPUT, (void*)PickerInput, (void**)&g_picker_input },
        { RVA_CLOSE_PICKER, (void*)ClosePicker, (void**)&g_close_picker },
        { RVA_OPEN_PICKER, (void*)OpenPicker, (void**)&g_open_picker },
        { RVA_HOVER, (void*)Hover, (void**)&g_hover },
        { RVA_NAV, (void*)Nav, (void**)&g_nav },
    };
    for (uint32_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        if (hf->hook(mod, game + hooks[i].rva, hooks[i].detour, hooks[i].original) != 0) return; /* the log says why */
    g_label_ok = 1;
    for (uint32_t i = 0; i < LABEL_CODE_COUNT && g_label_ok; i++)
        if (!hf->code_matches(game + kLabelCode[i].rva, kLabelCode[i].pattern))
        {
            hf->log(mod, "the game's %s (0x%X) is not the code this mod knows: no page label (the pages still work)",
                    kLabelCode[i].name, kLabelCode[i].rva);
            g_label_ok = 0;
        }
    if (g_label_ok)
    {
        text_init = (NodeFn)(game + RVA_TEXT_INIT);
        text_font = (TextFontFn)(game + RVA_TEXT_FONT);
        node_layout = (NodeFn)(game + RVA_NODE_LAYOUT);
        text_changed = (NodeFn)(game + RVA_TEXT_CHANGED);
        text_release = (TextReleaseFn)(game + RVA_TEXT_RELEASE);
        node_set_layer = (NodeSetLayerFn)(game + RVA_NODE_SET_LAYER);
        node_set_alpha = (NodeSetAlphaFn)(game + RVA_NODE_SET_ALPHA);
        node_set_color = (NodeSetColorFn)(game + RVA_NODE_SET_COLOR);
        node_set_visible = (NodeSetVisibleFn)(game + RVA_NODE_SET_VISIBLE);
        if (hf->hook(mod, game + RVA_LIST_UPDATE, (void*)ListUpdate, (void**)&g_list_update) != 0) g_label_ok = 0;
    }
    int eagles = 1;
    for (uint32_t i = 0; i < EAGLE_CODE_COUNT && eagles; i++)
        if (!hf->code_matches(game + kEagleCode[i].rva, kEagleCode[i].pattern))
        {
            hf->log(mod, "the game's %s (0x%X) is not the code this mod knows: Eagle Rearm reaches only the page shown, "
                    "and pages change during a call-in", kEagleCode[i].name, kEagleCode[i].rva);
            eagles = 0;
        }
    if (eagles)
    {
        static const struct { uint32_t rva; void* detour; void** original; } eagle_hooks[] = {
            { RVA_COOLDOWN, (void*)Cooldown, (void**)&g_cooldown },
            { RVA_COOLDOWN_SYSTEMS, (void*)CooldownSystems, (void**)&g_cooldown_systems },
            { RVA_EAGLES_SPENT, (void*)EaglesSpent, (void**)&g_eagles_spent },
            { RVA_EAGLES_USED, (void*)EaglesUsed, (void**)&g_eagles_used },
        };
        for (uint32_t i = 0; i < sizeof eagle_hooks / sizeof eagle_hooks[0] && eagles; i++)
            if (hf->hook(mod, game + eagle_hooks[i].rva, eagle_hooks[i].detour, eagle_hooks[i].original) != 0)
                eagles = 0; /* the log says why; the hooks made stay, and do nothing without g_eagles_ok */
        g_eagles_ok = eagles;
    }
    if (hf_on_game_thread(mod, Tick, NULL) != 0)
        hf->log(mod, "no game-thread update (HellForge too old?): the page keys do nothing; pages still change for a "
                "stratagem in hand");
    else StartWheel();
    hf->log(mod, "ready: %u stratagem slots, %u per row, page label %s, Eagles across pages %s, mouse wheel %s", g_slots,
            g_per_row, g_label_ok ? "on" : "off", g_eagles_ok ? "on" : "off", g_wheel_thread != NULL ? "on" : "off");
}

/* Taken out of the running game (or replaced): stop the wheel's thread, which the API doesn't know about. */
HF_MOD_EXPORT int hellforge_mod_unload(const HfApi* api, HfMod* mod, uint32_t reason)
{
    (void)api; (void)mod; (void)reason;
    StopWheel();
    return 0;
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    if (api->version < 13) return 1; /* code_matches */
    hf = api;
    self = mod;
    ReadSettings();
    g_slots = Clamp(g_slots, GAME_SLOTS, MAX_SLOTS);
    g_per_row = Clamp(g_per_row, GAME_SLOTS, MAX_SLOTS); /* the game's four stay one row */
    g_extra = g_slots - GAME_SLOTS;
    g_rows = g_slots <= g_per_row ? 0 : (g_slots - 1) / g_per_row;
    if (g_extra == 0)
    {
        hf->log(mod, "slots = 4: nothing to do");
        return 0;
    }
    return api->on(mod, HF_EVENT_GAME_LOADED, GameLoaded, NULL) == 0 ? 0 : 1; /* on: 0 on success */
}
