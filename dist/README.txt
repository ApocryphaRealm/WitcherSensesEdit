Witcher Senses Edit for Scavenger Sense
=======================================
Version 1.0.0

Scavenger Sense set up as a hunter's sense: tap a button to see what is around you, crouch to read footprints, and shoot
something to follow it.

THE SENSE
- Tap the left trigger or press G to send one sense pulse. It fades by itself after Scavenger Sense's usual few seconds.
- Only a quick tap counts, under a quarter of a second with no other button touched, so the left trigger still works as a hold modifier for your other mods.
- Opening a menu ends it early. Tapping again during a pulse starts a fresh one.
- What it lights is chosen when you tap: up to 127 objects in range at that moment. Things that load in later are not lit.
- On a horse the tap and G do nothing.
- Everything lights up with a strong glow and no name pop-ups: loot, small items, people and corpses. Containers are yellow, enemies red.
- The screen keeps its colour while you sense, so the highlight colours show. Scavenger Sense's HUD extras and its chime are off.

THE TRAIL
- Crouch and the footprints around you show, with no button.
- Land an arrow from your bow on someone and Scavenger Sense tracks them. When they die, it stops. Nothing to press.
- Any arrow you shoot from a bow, bound bows included. Crossbows, melee, spells and other people's arrows do not count.
- Everyone you hit stays tracked at once. A second arrow on the same target leaves them tracked.
- Any tracked target that dies is released at once, whoever marked it and whoever killed it.
- It works in any stance, and at any distance an arrow reaches. Tracked targets' trails stay visible when you stand up.
- Loading a save or starting a new game clears every tracked target and trail, with no message. Going through a door does not.
- Tracking has no key of its own here, so it takes no controller button.

SETTINGS
All of this is one small settings file. Your own ScavengerSense.ini overrides it key by key, so anything you save from
Scavenger Sense's menu wins.

REQUIREMENTS
Scavenger Sense 0.10.1 beta (Nexus 188380), and what that needs. This patch is made for that exact build. After a Scavenger
Sense update it does nothing until it is updated too, and says so in its log. It never touches a build it does not know.

WHAT IS IN THE PACKAGE
  SKSE\Plugins\WitcherSensesEdit.dll / .pdb                 the patch
  SKSE\Plugins\WitcherSensesEdit.ini                        the tap's length and the log level
  SKSE\Plugins\ScavengerSense_setup\80-witcher-senses-edit.ini     the settings and controls above

LOG
Documents\My Games\Skyrim Special Edition\SKSE\WitcherSensesEdit.log

CREDIT
Scavenger Sense is by KShakes. This patch contains none of its code or files.

Licence: GPL-3.0-or-later. Source: https://github.com/ApocryphaRealm/WitcherSensesEdit
