NX Redux, a fork of NextUI (by LoveRetro, itself based on MinUI) with screen sync fixes and many many more features!

Source:
https://github.com/mohammadsyuhada/nx-redux

Based on NextUI:
https://github.com/LoveRetro/NextUI

----------------------------------------
Installing

PREFACE

NX Redux has two essential parts: an installer/updater zip archive named "MinUI.zip" and a bootstrap folder named "trimui".

NX Redux supports the Trimui devices: Brick, Brick Pro, Smart Pro, and Smart Pro S. SD cards are built per device and are not swappable between models — use the Device Sync tool to move your data between devices.

The SD card should be a reputable brand and freshly formatted as exFAT (preferred).

INSTALLING

Copy all the folders from this zip file to the root of your SD card, then copy the "trimui" folder and "MinUI.zip" (without unzipping) to the root of the same card. At minimum, preload the "Bios" and "Roms" folders.

Please refer to the upstream NextUI docs at https://nextui.loveretro.games/usage/#getting-started for additional detail.

----------------------------------------
Updating

Copy "MinUI.zip" (without unzipping) to the root of the SD card containing your Roms.

Emulator and Tool paks are part of NX Redux itself now: they live in /.system/paks/ and update automatically with every update (MinUI.zip). There is nothing to copy by hand when updating.

/Emus and /Tools are for your OWN paks (e.g. a community PSP.pak). Do NOT place a pak here with the same name as a shipped one: same-named paks are treated as NX Redux leftovers and are currently removed on every update. (Placing a same-named pak here to shadow the shipped one will become supported in a future release, once this transition cleanup is retired.)

----------------------------------------
Shortcuts

  Mute: FN switch (volume and rumble)
  "TrimUI" button: Quick menu
  Select button: Game switcher

----------------------------------------
Quicksave & auto-resume

NX Redux will create a quicksave when powering off in-game. The next time you power on the device it will automatically resume from where you left off. A quicksave is created when powering off manually or automatically after a short sleep.

----------------------------------------
Roms

Included in this zip is a "Roms" folder containing folders for each console NX Redux currently supports. You can rename these folders but you must keep the uppercase tag name in parentheses in order to retain the mapping to the correct emulator (eg. "Nintendo Entertainment System (FC)" could be renamed to "Nintendo (FC)", "NES (FC)", or "Famicom (FC)").

When one or more folder share the same display name (eg. "Game Boy Advance (GBA)" and "Game Boy Advance (MGBA)") they will be combined into a single menu item containing the roms from both folders (continuing the previous example, "Game Boy Advance"). This allows opening specific roms with an alternate pak.

----------------------------------------
Bios

Some emulators require or perform much better with official bios. NX Redux is strictly BYOB. Place the bios for each system in a folder that matches the tag in the corresponding "Roms" folder name (eg. bios for "Sony PlayStation (PS)" roms goes in "/Bios/PS/"), or refer to the upstream NextUI docs at https://nextui.loveretro.games/usage/#required-bios for the correct file names and locations.

Bios file names are case-sensitive:

   FC: disksys.rom
   GB: gb_bios.bin
  GBA: gba_bios.bin
  GBC: gbc_bios.bin
   MD: bios_CD_E.bin
       bios_CD_J.bin
       bios_CD_U.bin
   PS: psxonpsp660.bin

----------------------------------------
Cheats

Cheats use RetroArch .cht file format. Many cheat files are here <https://github.com/libretro/libretro-database/tree/master/cht>

Cheat file name needs to match ROM name, and go underneath the "Cheats" directory. For example, `/Cheats/GB/Super Mario Land (World).zip.cht`. When a cheat file is detected, it will show up in the "cheats" menu item ingame. Not all cheats work with all cores, may want to clean up files to just the cheats you want.

----------------------------------------

Disc-based games

To streamline launching multi-file disc-based games with NX Redux place your bin/cue (and/or iso/wav files) in a folder with the same name as the cue file. NX Redux will automatically launch the cue file instead of navigating into the folder when selected, eg.

  Harmful Park (English v1.0)/
    Harmful Park (English v1.0).bin
    Harmful Park (English v1.0).cue

For multi-disc games, put all the files for all the discs in a single folder. Then create an m3u file in that folder (just a text file containing the relative path to each disc's cue file on a separate line) with the same name as the folder. Instead of showing the entire messy contents of the folder, NX Redux will launch the appropriate cue file, eg. For a "Policenauts" folder structured like this:

  Policenauts (English v1.0)/
    Policenauts (English v1.0).m3u
    Policenauts (Japan) (Disc 1).bin
    Policenauts (Japan) (Disc 1).cue
    Policenauts (Japan) (Disc 2).bin
    Policenauts (Japan) (Disc 2).cue

The m3u file would contain just:

  Policenauts (Japan) (Disc 1).cue
  Policenauts (Japan) (Disc 2).cue

When a multi-disc game is detected the in-game menu's Continue item will also show the current disc. Press left or right to switch between discs.

NX Redux also supports chd files and official pbp files (multi-disc pbp files larger than 2GB are not supported). Regardless of the multi-disc file format used, every disc of the same game share the same memory card and save state slots.

----------------------------------------
Collections

A collection is just a text file containing an ordered list of full paths to rom, cue, or m3u files. These text files live in the "Collections" folder at the root of your SD card, eg. "/Collections/Metroid series.txt" might look like this:

  /Roms/GBA/Metroid Zero Mission.gba
  /Roms/GB/Metroid II.gb
  /Roms/SNES (SFC)/Super Metroid.sfc
  /Roms/GBA/Metroid Fusion.gba

----------------------------------------

Display names

Certain (unsupported arcade) cores require roms to use arcane file names. You can override the display name used throughout NX Redux by creating a map.txt in the same folder as the files you want to rename. One line per file, `rom.ext` followed by a single tab followed by `Display Name`. You can hide a file by adding a `.` at the beginning of the display name. eg.

  neogeo.zip	.Neo Geo Bios
  mslug.zip	Metal Slug
  sf2.zip	Street Fighter II

----------------------------------------
Simple mode

Not simple enough for you (or maybe your kids)? NX Redux has a simple mode that hides the Tools folder and replaces Options in the in-game menu with Reset. Perfect for handing off to littles (and olds too I guess). Just create an empty file named "enable-simple-mode" (no extension) in "/.userdata/shared/".

----------------------------------------
Advanced

NX Redux can automatically run a user-authored shell script on boot. Just place a file named "auto.sh" in "/.userdata/<DEVICE>/". If you're on Windows, make sure your text editor uses Unix line-endings (eg. `\n`), these devices usually choke on Windows line-endings (eg. `\r\n`).
