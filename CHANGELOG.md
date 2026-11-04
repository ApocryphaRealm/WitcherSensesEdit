# Witcher Senses Edit for Scavenger Sense - changelog

## 1.0.0 - 2026-11-03 - untested

### Added
- Holding activate on a horse no longer fires or ends the sense, so hold to dismount works; a sense already on stays on. Debug log: a line for each arrow hit on something that is not an actor, naming what it is.
- First version. An arrow from your bow that lands on someone makes Scavenger Sense 0.10.1 beta track them, through its own tracking function, and any tracked target that dies is released at once. A second arrow leaves the target tracked; crossbows, melee, spells and other people's arrows do not count. A setup file turns on Track several at once, and your own ScavengerSense.ini wins. Any other Scavenger Sense build leaves the patch off, with a line in its log.

### Changed
- Hold activate to turn the sense on and hold again to turn it off, staying on for up to 120 seconds, Scavenger Sense's limit. Footprints show whenever you crouch, with no button.
- Renamed from Scavenger Sense - Arrow Quarry. Now also carries a settings file: hold activate to sense, containers yellow, no name pop-ups, no people highlighted, HUD extras and chime off, no hand keys for tracking.
### Fixed
- An arrow that lands on someone now tracks them. The game reports a hit on an actor with the bow but no projectile, and the check required a projectile, so only arrows hitting non-actors passed, and those are not actors to mark.