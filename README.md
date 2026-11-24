# More Stratagem Slots

<small>_Disclaimer: This was made using Claude Code. Opus 5.5 was used for the research and implementation, and Fable 5.2 was used for the planning._</small>

A mod for [HellForge](https://github.com/SkyeShade/HellForge) allowing you to bring up to a maximum of 20 stratagems. It reuses the existing stratagem widget on the loadout screen for seamless selection.

**Requires HellForge 0.4.0-beta or later. Built and tested on HellForge 0.4.0-beta and on the 0.5 nightly of 2026-11-23, for game build 25480438. It has only been tested solo, so co-op is untested. If you try it, every player needs the mod.**

## Known incompatibilities

- **[DiverKit](https://ayakamods.com/mods/diverkit.4165/)** (loadout presets): it does not load while this mod is on, so its preset buttons never appear. DiverKit checks parts of the game's loadout screen code before it starts, and this mod changes three of them. Use one or the other.

## Preview

<p>
  <img src="assets/demo/loadout-fill-folded-12.png" width="264">
  <img src="assets/demo/loadout-fill-unfolded-12.png" width="264">
  <img src="assets/demo/loadout-fold-demo-20.gif" width="250">
</p>

<p>
  <img src="assets/demo/stratagem-paged-static-20.png" width="300">
  <img src="assets/demo/stratagem-paged-demo-20.gif" width="441">
</p>

## Installation

1. Download the latest release from the [Releases](https://github.com/hariyomari/more-stratagem-slots/releases/latest) page.
2. Open the HellForge launcher, go to the Mods page and press Add mods...
3. Pick the zip. You don't need to unpack it.
4. Make sure the mod is switched on, then start the game.

You can also drop the zip directly into your HellForge `mods` folder. That is the `mods` folder inside the folder HellForge is installed in, `Documents\HellForge\mods` by default.

## Usage

By default, you can press Q/E or MWHEEL UP/DOWN to scroll through the list of pages. If you have multiple pages, you should see `the current page number/the max page number` in the top right of the stratagem list.

The controls can be configured in the settings, which get auto-generated on the first launch after installing the mod.

On the loadout screen the slots are in rows of four. With more than 8 slots the card starts collapsed: it shows the first row, and a bar under it with the number of slots it hides (`SHOW [+12]` for 16 slots). Click the bar to show the other rows, and click it again (`HIDE [-12]`) to hide them. On a controller, move down to the bar and press the select button. The card is collapsed again each time you come to the loadout screen, and it opens by itself when the stratagem picker moves on to a slot in a hidden row.

## Settings

The settings are in `more-stratagem-slots.ini`, directly in your HellForge `mods` folder:

```
<your HellForge folder>\mods\more-stratagem-slots.ini
```

With the default install location that is `%USERPROFILE%\Documents\HellForge\mods\more-stratagem-slots.ini`. It sits beside the mod's zip, not inside the mod's own folder (the one with `hellforge-mod.json` and `src`).

1. Open the folder HellForge is installed in, then its `mods` folder:

   <img src="assets/settings-tutorial/step1.png" width="600">

2. The settings file is here, beside the mod's zip:

   <img src="assets/settings-tutorial/step2.png" width="600">

The file is created with the defaults the first time you start the game with the mod switched on. Open it with a text editor, change a value and save. Changes apply the next time the game starts.

| Setting             | Default | Description                                                                                                                                                                                             |
| ------------------- | ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `slots`             | `8`     | The number of stratagem slots in the loadout, from 4 to 20.                                                                                                                                             |
| `collapse`          | `on`    | Collapses a loadout of many rows on the loadout screen (see [Usage](#usage)). With `off` the card always shows every row and has no bar.                                                                |
| `collapse_rows`     | `3`     | How many rows of four a loadout needs to be collapsed, from 2 to 5. With `3`, a loadout of more than 8 slots is.                                                                                        |
| `collapse_animate`  | `on`    | Opens and closes the collapsed rows over a moment. With `off` they show and hide at once.                                                                                                               |
| `page_prev`         | `Q`     | The key for the previous page, while the stratagem list is open. A letter, a digit, or a Windows virtual-key code.                                                                                      |
| `page_next`         | `E`     | The key for the next page.                                                                                                                                                                              |
| `page_wheel`        | `on`    | Allows the mouse wheel to switch pages while the stratagem list is open: wheel up is the next page and wheel down the previous one.                                                                     |
| `page_wheel_invert` | `off`   | Turns the mouse wheel around: wheel up is the previous page and wheel down the next one. Only takes effect with `page_wheel` on.                                                                        |
| `page_lock`         | `off`   | Stops you from switching pages while a stratagem is in your hand or being called in. With `off` you can switch at any time, and a stratagem in your hand stays in the list until it has been called in. |
| `page_added`        | `on`    | Puts stratagems that other mods add to your list during a mission (P.S.S. extras, for example) in the pages after your own. With `off` they stay on every page, which leaves less room for yours.       |
| `page_rearm`        | `on`    | Shows Eagle Rearm on every page once an Eagle has spent a use, so you can rearm from any page. With `off` it only shows on the pages that hold an Eagle. Either way it rearms the Eagles on every page. |

A settings file made by an older version doesn't have the newer settings, and the mod uses their defaults. To change one, add its line yourself (`page_rearm = off`, for example). Or delete the file while the game is closed, and the mod writes a new one with every setting the next time the game starts. The mod can't add to an existing file itself, because HellForge keeps the files in `mods` read-only while the game runs.

The game's stratagem list holds 16 at most, including its own (Reinforce, Resupply, SOS Beacon and so on). When your stratagems don't fit, they are split into pages. Only the page shown can be called in, and each stratagem keeps its uses and cooldown while on another page.

### Upgrading from an older version

`per_row` from before 0.3.0 is no longer read: the rows are always of four. You can leave its line in the file or delete it. To change when the card collapses, add a `collapse_rows` line; to switch the collapsing off, add `collapse = off`; to switch only its animation off, add `collapse_animate = off`.

`page_hold` from before 0.2.0 is no longer read, so you can leave its line in the file or delete it. `page_lock = on` does what it did.

From 0.2.0 wheel up is the next page and wheel down the previous one, the other way around from before. `page_wheel_invert = on` gives the old direction.

## Troubleshooting

### The settings file isn't there

The file is created the first time the game starts with the mod switched on. If it is missing:

1. Check on the launcher's Mods page that the mod is listed and switched on, and that your HellForge is up to date (the mod needs 0.4.0-beta or later, and is tested on 0.4.0-beta and on the 0.5 nightly of 2026-11-23). If the mod didn't start, it can't create the file.
2. Or create it yourself: copy [`more-stratagem-slots.template.ini`](more-stratagem-slots/more-stratagem-slots.template.ini) (it is also inside the zip) into the `mods` folder and rename it to `more-stratagem-slots.ini`. The template holds the defaults.

Don't edit the template where it is, or put the settings file inside the mod's folder: the mod only reads `mods\more-stratagem-slots.ini`.

### The game is stuck at "build mods"

The first time the game starts with a mod that is built from source, HellForge downloads its compiler. This can take a few minutes, and only happens once.

## Building

HellForge builds the mod from its source when the game starts, so you don't need to build anything to play. To make the release zip yourself, run `scripts\package.bat` (or `scripts/package.sh`) with your HellForge folder:

```powershell
.\scripts\package.bat $env:USERPROFILE\Documents\HellForge        # default HellForge install location
```

The zip is written to `dist`.

## License

[MIT](LICENSE)
