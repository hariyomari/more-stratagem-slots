/* More Stratagem Slots: functions other HellForge code mods can call (version 1).
 *
 * The mod's DLL is stratagem_slots.dll. Look the functions up by name, and check the version first:
 *
 *     HMODULE mss = GetModuleHandleW(L"stratagem_slots.dll");
 *     MssVersionFn version = mss ? (MssVersionFn)(void*)GetProcAddress(mss, "mss_version") : NULL;
 *     if (version != NULL && version() >= 1) { ... GetProcAddress(mss, "mss_slots") ... }
 *
 * Look them up once the game has loaded (HF_EVENT_GAME_LOADED, or a game-thread callback), not in hellforge_mod_init:
 * HellForge loads each mod's DLL just before its init, in the order of the mods' ids, so it may not be loaded yet. When
 * the DLL is there but the mod did not start (its log says why), mss_version returns 0 and the others answer 0.
 *
 * No function is ever removed or changed: a new version only adds functions. HellForge never unloads the DLL, so a
 * function found once stays valid for the session. Call them from the game thread (a hf_on_game_thread callback, or a
 * hook the game calls on it), where this mod does its own work.
 *
 * Stratagems are the game's types (the u32 a loadout slot widget holds at +0x128C), as in the loadout record.
 *
 * Setting the extra slots: this mod hooks the game's setter for a loadout card's slot (LobbyCard_SetSlotStable,
 * game.dll 0x189D050 on build 25480438: card, slot, stable id), which the game's picker calls, and puts slots 4 to
 * mss_slots() - 1 in its extra slots (past that, the game ignores them as it does 4 and up). A locked slot stays empty,
 * and stable id 0 empties the slot. The game's commit after a pick (0x189D120: card, -1) then writes them to the
 * loadout. Put a stratagem in one slot only, as the game's picker does. */
#ifndef MORE_STRATAGEM_SLOTS_H
#define MORE_STRATAGEM_SLOTS_H
#include <stdint.h>

#define MSS_VERSION 1u

/* mss_get_slots: a slot that is locked (past the slots the player has unlocked). */
#define MSS_SLOT_LOCKED 0xFFFFFFFFu

/* mss_picks: an entry's flags. */
#define MSS_PICK_SHOWN 1u    /* in the stratagem list now (its page is shown, or it stays in the list in hand) */
#define MSS_PICK_USED_UP 2u  /* the game took it out of the list during the mission (no uses left) */
#define MSS_PICK_ADDED 4u    /* not one of the loadout's picks: another mod added it to the list during the mission */

typedef struct MssPick
{
    uint32_t type;  /* the stratagem */
    uint32_t page;  /* the page it is on, from 0 */
    uint32_t flags; /* MSS_PICK_* */
} MssPick;

/* MSS_VERSION of this DLL (1 or more), or 0 when the mod did not start. */
typedef uint32_t (*MssVersionFn)(void);
/* The stratagem slots the player set (slots in the settings file, 4 to 20). 4 means the loadout screen is the game's
 * own, with no extra slots. */
typedef uint32_t (*MssSlotsFn)(void);
/* A loadout card's slots, in order (index 0 is slot 1): each slot's stratagem, 0 when it is empty, or MSS_SLOT_LOCKED.
 * card is the loadout card the game's setter takes. Writes min(capacity, mss_slots()) types and returns mss_slots(), or
 * returns 0 and writes nothing when this mod has no extra slots on that card (it is not the loadout screen's, or the
 * screen's slots are not built yet). Always 0 with slots = 4: there are no extra slots, and the card is the game's. */
typedef uint32_t (*MssGetSlotsFn)(const void* card, uint32_t* types, uint32_t capacity);
/* The local player's pages in this mission: the page shown, from 0, and how many there are. mss_page_count() is 0 when
 * there are no pages (not in a mission, the mission is over, or this mission's picks were not the local player's), and
 * 1 when everything fits in the stratagem list at once. */
typedef uint32_t (*MssPageFn)(void);
typedef uint32_t (*MssPageCountFn)(void);
/* Everything the pages hold in this mission, in order: the loadout's picks, then what other mods added to the list
 * (with page_added on). Writes min(capacity, count) entries and returns the count, 0 when there are no pages. The
 * game's own entries (Reinforce, Resupply, mission stratagems, Eagle Rearm) are on every page and are not in it. */
typedef uint32_t (*MssPicksFn)(MssPick* picks, uint32_t capacity);
/* Asks for page `page` (from 0), as the page keys do; it changes at this mod's next game-thread update, unless
 * page_lock holds it back. 1 asked, 0 not possible (no such page, or no pages). */
typedef int (*MssSetPageFn)(uint32_t page);

#endif
