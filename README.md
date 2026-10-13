# StatsPage

Completion stats for **CONTROL Resonant**. The mod adds four tabs to the Archives screen of the
in-game menu, behind Collectibles, Enemies and Tutorials: **Completion**, **Story**, **Zones** and
**Dylan**. They show one completion bar and a count for each kind of thing the game lets you do or
find, taken from your latest save.

- **Completion**: the bar for everything, and its parts as percentages.
- **Story**: main story missions, side stories, and a row for each side story that comes in parts
  across the zones, with what you found in every zone.
- **Zones**: zone levels, world encounters, Hiss Vending Machines, World Feats, SHÜM scores (your
  place on each machine) and FBC Tasks, each with a count per zone.
- **Dylan**: Artifacts, clothes, abilities, talents and weapon upgrades, with the names of what is
  still missing and where it comes from.
- The pages hold back what would spoil the story: names of story things you have not done yet,
  story totals, zones you have not visited, names of items you have not unlocked. Each of these is
  a setting.
- The numbers are read again each time you open the Archives. The mod changes no game code and no
  save.
- With Mod Settings Menu installed, the settings are under Options > MODS, with an Enable switch.

## Install

Grab `StatsPage.zip` from the [releases](../../releases) and extract it into the game folder
(next to `CONTROLResonant.exe`). Everything lands in `crmods\StatsCompletionTracker\`. It needs
crloader, the `winmm.dll` mod loader, which you already have if you run other DLL mods. Full
details, and how the bar counts, are in [README-StatsCompletionTracker.txt](README-StatsCompletionTracker.txt).

Made for game version 1.4.1. The mod shows no messages: if a later update moves the code it
needs, it leaves the game's menus as they are and says so in
`crmods\StatsCompletionTracker\statscompletiontracker.log`.
It works next to other mods that change the same menu files, Mod Settings Menu among them.

## Build

Visual Studio 2022 (MSVC, x64) and Python 3. From a terminal:

```bat
build.bat
```

It runs `tools\gen_ui.py`, which packs `ui\statspage_archive.js` into `ui.inc`, and compiles
`statspage.cpp` into `out\statscompletiontracker.dll`. No third-party code is used. `package.py`
makes the release zip from the DLL and the package files; by hand, copy the DLL,
`statscompletiontracker_config.ini`, `statscompletiontracker.menu.json` and
`README-StatsCompletionTracker.txt` into `crmods\StatsCompletionTracker\` in the game folder.

`tables.inc` holds what the mod counts: every mission, encounter, item and upgrade with the save
entry that marks it as done. It is made by `tools\gen_tables.py` from data taken out of the game's
packs (game 1.4.1). That script needs extraction tools that are not part of this repository, so
`tables.inc` and `tools\tables.json` are checked in as they came out.

Tests: `tests\build_tests.bat` builds `tallytest.exe` (the save reading and counting outside the
game), `fakegame.exe` (a stand-in game whose code holds the start of the game's UI file handler,
so the DLL finds its place and works as in the game) and a stand-in `steam_api64.dll`.
`python tests\run_tests.py <save folder> [second save folder]` runs them, compares the page's
numbers with the Python reference in `tools\evalsave.py`, and runs the Archives script against a
stand-in page with node. The tests need two of the game's UI files (`globals_island.bundle.js` and
`OptionsComponent.template.html`, out of the game's packs, in a folder named by `STATSPAGE_UI`)
and save folders; neither is in the repository, and several checks expect one finished save and
one from the middle of the story.

## How it works

The game's menus are HTML pages. The game hands their files to its UI library through one
function, the second entry of a vtable. The mod puts its own function into that entry, in front
of what is there: the game's function, or another mod's that did the same. It does two things
there. It appends `ui\statspage_archive.js` to the game's globals script on its way to the UI,
and it answers one address of its own, `coui://base/__statspage__.json`, with the numbers. The
mod finds the vtable entry by byte pattern at start. No game code is hooked or patched, and no
file on disk is changed.

The script adds the four tab buttons and the page to the Archives screen while the game runs. It
needs no particular piece of the game's script text, so it does not get in the way of other mods
that edit the same file. The game's own tab keys walk through all seven tabs; while one of the
mod's tabs is open, the game's list underneath is switched off so the D-pad does not move it
unseen.

The numbers come from the save with the most recent date. The saves are read through the store
library the game has loaded (Steam's remote storage), so no path is guessed; a folder can be
named in the ini instead. `save.h` reads the save's global chunk: world facts, collected
narrative objects, rewards handed out and the unlock graphs. `tally.h` counts them against the
tables and writes the page's answer as JSON.

The bar: every kind of thing is one equal part, whatever its size, and the seven side stories in
parts are one part together. That makes 16 parts, and what can be earned fills 99% of the bar.
The last 1% belongs to one outfit piece that game 1.4.1 never hands out. Outfit pieces from an
edition, a pre-order or a promotion are counted on the Dylan tab but are not part of the bar.

## Credits

Loaded by crloader. Works with Mod Settings Menu for its settings.
