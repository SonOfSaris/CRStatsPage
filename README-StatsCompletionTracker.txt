StatsPage 1.0 - completion stats in the Archives of CONTROL Resonant
====================================================================

Adds four tabs to the Archives screen of the in-game menu, behind Collectibles, Enemies and
Tutorials. They show one completion bar and a count for each kind of thing the game lets you do
or find, taken from your latest save.

REQUIRES
  crloader (the winmm.dll mod loader) in the game folder. If you run other DLL mods, you have it.
  Optional: Mod Settings Menu, for the mod's settings in game under Options > MODS.

INSTALL
  1. Extract this archive into the game folder (next to CONTROLResonant.exe). Everything goes
     into one folder:
       crmods\StatsCompletionTracker\
     If you have a crmods\StatsPage folder or statspage files from an earlier download,
     delete them.
  2. Start the game, open the menu and go to Archives.
  Made for game version 1.4.1. The mod changes no game code and no save. If a later update
  changes the menus, the mod leaves them as they are and says so in its log.

THE TABS
  Completion   the bar for everything and its parts as percentages
  Story        main story missions, side stories, and a row each for the seven side stories
               that come in parts across the zones, with what you found in every zone
  Zones        zone levels, world encounters, Hiss Vending Machines, World Feats, SHUM scores
               (your place on each machine), FBC Tasks
  Dylan        Artifacts, clothes, abilities, talents, weapon upgrades; below them a count
               of the promotional outfits you own

  Collectibles and enemy research count toward the bar too. They have no tab and no row here,
  because the game's own Collectibles and Enemies tabs show them.

  The tab keys go through all seven tabs, and you can click a tab. In a tab, move up and down
  the list on the left, or point at a row. The middle shows counts per zone, or the names of
  what is still missing and where it comes from. Side Stories also lists the stories you
  have completed. Its lists are in alphabetical order.

HOW IT COUNTS
  - The numbers come from the save with the most recent date. They are read again each time
    you open the Archives, so they are as fresh as your last save.
  - The bar shows what that save holds. New Game Plus resets missions, tasks, encounters
    and more, and the bar goes down with them, so you can track them again in the new
    playthrough. Some things need more than one playthrough: 100% is not reachable in
    the first one.
  - What you can earn is 99% of the bar. The last 1% belongs to one outfit piece the game
    does not hand out as it is (game 1.4.1): its reward never arrives. If a game update
    or a mod gives it to you, the bar reads 100%.
  - Outfit pieces from an edition, a pre-order or a promotion are not part of the bar.
    The Dylan tab shows how many of them you own.
  - Two Artifacts count as ones you can earn although game 1.4.1 gives no way to them:
    the one of the dog side story, which the game hands out since version 1.4.2, and
    The Weekend Crossword, which players report as a zone level reward. One Artifact
    that has a recipe but no blueprint anywhere is not counted at all.
  - The pages hold a few things back so they spoil nothing. Each has a setting:
      names of story things you have not done yet stay hidden until the main story is done
      (a side story in parts shows its name once you have found a part of it, and
      its ending once that is done);
      Main Story Missions and Side Stories show ?? as their total until all are done;
      zones you have not visited yet show as ??? (so does one SHUM machine the story only
      leads to at its end, until the main story is done or you have a score on it);
      Artifacts and clothes you have not unlocked show as ???, next to where they come from;
      a missing ability shows as ??? until its Resonant is defeated.

UNINSTALL
  Delete the crmods\StatsCompletionTracker folder. Nothing else was changed.

SETTINGS
  crmods\StatsCompletionTracker\statscompletiontracker_config.ini
  With Mod Settings Menu: Options > MODS > StatsPage; a choice made in the menu wins over the ini.
  There you choose what the pages hold back (see above) and can switch the mod off.
  If the pages say they found no save (a copy of the game that does not use Steam), name your
  save folder under [Saves] Folder in the ini.

LOG
  crmods\StatsCompletionTracker\statscompletiontracker.log, rewritten every start. It says
  whether the tabs were added, where the saves are read from and which save the pages show.
