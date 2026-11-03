Scavenger Sense - Arrow Quarry
==============================
Version 1.0.0

Land an arrow from your bow on someone and Scavenger Sense tracks them, as if you had aimed at them and pressed its
tracking key. When they die, it stops. Nothing to press and nothing to set.

- Any arrow you shoot from a bow, bound bows included. Crossbows, melee, spells and other people's arrows do not count.
- Everyone you hit stays tracked at once (a small installer-style file turns on Scavenger Sense's "Track several at once";
  your own ScavengerSense.ini wins if you have set that key).
- A second arrow on the same target leaves them tracked. Your own key still marks and releases anyone by hand.
- Any tracked target that dies is released at once, whoever marked it and whoever killed it.
- It works in any stance. Scavenger Sense's key only works during a crouched sweep by default; arrows do not wait for that.

REQUIREMENTS
Scavenger Sense 0.10.1 beta (Nexus 188380), and what that needs. This patch is made for that exact build. After a Scavenger
Sense update it does nothing until it is updated too, and says so in its log. It never touches a build it does not know.

WHAT IS IN THE PACKAGE
  SKSE\Plugins\ScavengerSenseArrowQuarry.dll / .pdb                 the patch
  SKSE\Plugins\ScavengerSenseArrowQuarry.ini                        the log level, nothing else
  SKSE\Plugins\ScavengerSense_setup\80-arrow-quarry.ini             several marks at once

LOG
Documents\My Games\Skyrim Special Edition\SKSE\ScavengerSenseArrowQuarry.log

CREDIT
Scavenger Sense is by KShakes. This patch contains none of its code or files.

Licence: GPL-3.0-or-later. Source: https://github.com/ApocryphaRealm/ScavengerSenseArrowQuarry
