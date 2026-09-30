# Quarry Debug UI

A mod for **The Quarry** (PC, Steam) that turns on the developer debug menus
that ship, hidden, in the retail game.

The game's menus already contain debug buttons and screens. Each one asks the
game whether to show debug UI, and in the retail build the answer is always
no. This mod changes that answer to yes. It adds no menus of its own.

## What appears

- **A main debug menu**, opened from the main menu (the prompt reads "DEBUG",
  on the Z key with a keyboard). It has eight entries:
  - Blockouts: a developer chapter select (below).
  - Character Viewer.
  - Reset All Settings.
  - Delete All Save Data.
  - Build Type, which the game describes as affecting UI and flow.
  - Show Collectables, which makes all collectables visible on the
    collectables screens.
  - Rewind Debug Unlock, which makes the rewind feature available.
  - UI Button Swap.
- **Blockouts: start any chapter.** A list of 64 chapter starts in story order,
  from the Prologue to the Epilogue, including starts partway through acts.
  The same screen has an Automation Path option and a Force Subtitle Names
  checkbox. Starting a chapter from here goes through a pre-start screen with
  subtitle options, All Settings, Movie Mode Setup and Continue. Where the
  chapter has story variables, a screen for setting them follows.
- **Movie Mode: Debug Settings.** A button in the Movie Mode menu. According
  to the game's files, it sets each of the 9 decision types (QTEs, button
  mashing, conversations, interrupts, shooting, Don't Breathe, exploration,
  directional choices and "look at this" moments) separately for each of the
  10 characters, to Player, Good, Bad or Random. Director's Chair bundles
  these into four traits per character.
- **Settings: a Debug button.**
- **Smaller things:** the build version on the main menu, a Blockouts option in
  the Couch Co-op and Wolf Pack host menus, and debug-only buttons in some
  pop-ups.

## Before you use it

- **Back up your saves first**, in `%LOCALAPPDATA%\TheQuarry\Saved\SaveGames`.
  Delete All Save Data and Reset All Settings do what they say.
- **Leave Build Type on DEFAULT** unless you are experimenting. The other
  values have not been tested.
- **These are developer tools.** Not every screen has been tried, and some
  may depend on code the retail build does not include.
- **With a screen reader:** The Quarry Access (QuarryAccess) reads most of these
  screens, but not yet the Blockouts chapter list or the story-variables
  screen. In the chapter list, focus starts on the first chapter, the arrow
  keys move it (without the usual click sound) and Enter starts the chapter.

## Requirements

- The Quarry, Steam version for Windows (tested September 2026).
- Windows 10 or 11.

## Installing

1. Download `XAPOFX1_5.dll` from the latest release.
2. Open the game folder, then `SMG026\Binaries\Win64`: the folder that
   contains `TheQuarry-Win64-Shipping.exe`. In Steam, right-click The Quarry,
   then Manage, then Browse local files.
3. Copy the file there.
4. Start the game as usual.

It works alongside UE4SS, The Quarry Access and Essential Audio Enhancements for
The Quarry, which each use a different file.

## Checking that it works

Each time the game starts, the mod writes `QuarryDebugUI.log` next to the DLL.
It says either `Debug UI enabled` or why not.

## Uninstalling

Delete `XAPOFX1_5.dll` and `QuarryDebugUI.log` from `SMG026\Binaries\Win64`.
The mod changes no game files; it works in memory while the game runs.

## Known issues

- A game update may change the code the mod patches. The mod checks the exact
  bytes first. If they differ, it leaves the game alone and says so in the log.

## How it works

For modders and the curious.

- **Loading.** The game imports one function, `CreateFX`, from
  `XAPOFX1_5.dll`, a DirectX audio-effects library. A DLL of that name in the
  game folder loads before any game code runs. The mod passes `CreateFX`
  through to the real copy in Windows.
- **The switch.** The menus call the native Blueprint function
  `ShouldShowDebugUI`. In the retail exe its body is compiled out: it stores a
  constant false (`mov byte ptr [rsi], 0` at `0x1415ec258`), so no setting,
  config file or command-line option can change it. The mod checks the 37
  bytes around that instruction, which occur once in the exe, and only if they
  match exactly changes the 0 to 1.
- **Side effects.** The linker merged two editor-only functions into the same
  code (`JumpToBookmarkInLevelEditor` and `RunAssetExportTask`), so they
  return true as well. None of the game's 117,092 assets refers to either.
- **The eight callers**, read from the Blueprint bytecode: MainMenuWidget,
  MovieMode, SettingsHomeWidget, PopupScreenBaseWidget, WolfPackHostWidget,
  CouchCo-opQuickStart, CouchCo-opNoCharactersAssigned and
  GameCompleteNestedContentBase.

## Building

With MinGW-w64 GCC (for example WinLibs) on `PATH`, or its path in the
`QDU_GCC` environment variable:

    .\build.ps1           # builds build\XAPOFX1_5.dll and runs the tests
    .\build.ps1 -Deploy   # also copies it into the game folder

The tests check that the patch changes exactly one byte and restores the page
protection, that running it twice is harmless, that a one-byte difference at
any position of the check is refused with nothing written, that unmapped
memory is refused, that `CreateFX` reaches the real DLL, and that the check
matches the installed game.

## License

GPL-3.0-or-later. See `LICENSE`.

This is an unofficial, fan-made mod. It is not affiliated with, endorsed by or
supported by Supermassive Games, 2K or Microsoft. The Quarry is a trademark of
its respective owners. The mod does not include any of the game's files or
assets, or any Microsoft code; use it at your own risk.
