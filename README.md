# Zombies Ate My Neighbors - Native PC Port

A native Windows version of the 1993 SNES game *Zombies Ate My Neighbors*. The
game's logic is being rewritten in C, checked against the original, while the
SNES graphics and sound chips are emulated. It plays like the cartridge, with
some additions for a modern PC:

- Fullscreen at any resolution, with optional widescreen (16:9, 16:10, 21:9)
- Smooth motion on high refresh rate displays (120 Hz, 144 Hz, 240 Hz)
- Controller support for one or two players, with twin-stick aiming on the right stick
- Weapon and item switching in both directions on the shoulders and triggers
- Quick save and quick load
- The top scores are saved between sessions
- Skip the intro, or start on any level
- Optional cheats: invincibility, infinite ammo, infinite lives and more

> **You need your own copy of the game.** No game data is included. Supply a
> legally obtained ROM named `Zombies Ate My Neighbors.sfc`.

## Installation

There are no prebuilt downloads yet, so the port is built from source. This
takes a few minutes.

1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/) (the free
   Community edition is fine) with the **Desktop development with C++**
   workload, and install [Git](https://git-scm.com/).
2. Get the source:
   ```
   git clone https://moshferatu.dev/moshferatu/zombies-ate-my-neighbors.git
   cd zombies-ate-my-neighbors
   ```
3. Copy your ROM into this folder as `Zombies Ate My Neighbors.sfc`. The
   launcher's icon and logo are made from it during the build.
4. Build:
   ```
   powershell -ExecutionPolicy Bypass -File tools\build.ps1
   ```
   The first build also downloads and builds SDL2.

Everything you need ends up in the `build` folder.

## Playing

Start **`build\zamn_launcher.exe`**. Choose your ROM on the Game tab, change
anything else you like, and press **Play**. The launcher saves your choices to
`zamn.ini` and hides while the game runs.

The launcher works with a mouse, the keyboard or a controller. Its tabs are:

| Tab | What it sets |
|---|---|
| Game | ROM, skipping the intro, starting level, pickup reach, blood colour, top scores |
| Video | Fullscreen, widescreen, scaling filter, window size, smoothing, radar |
| Audio | Sound on or off, volume, restoring dropped sound effects |
| Controller | Twin-stick aiming, deadzone, which stick moves and which aims, button bindings |
| Keyboard | Key bindings for player 1 and player 2 |
| Hotkeys | Keyboard and controller shortcuts for quick save, fullscreen and the like |
| Cheats | Invincibility, invincible neighbours, infinite ammo, infinite lives, all weapons, always run |

To rebind a control, select it and press the new key or button.

### Controls

A controller is the best way to play. Most controllers work, and they can be
plugged in at any time. Two controllers means two players. Buttons are named by
position, as on a SNES pad:

| Controller | Action |
|---|---|
| Left stick or D-pad | Move |
| Right stick | Aim and fire (twin-stick) |
| Left face button (Y) | Fire, held down |
| R1 / L1 | Next / previous item |
| R2 / L2 | Next / previous weapon |
| Top face button (X) | Use the selected item |
| Touchpad click or L3 | Radar |
| Start | Pause |
| Start + Select together | Quit |

On the keyboard, the arrow keys move and **A** fires. The other defaults are:

| Key | Action |
|---|---|
| Z / X | Cycle weapons / items |
| S | Use the selected item |
| Q / W | Radar |
| Enter / Right Shift | Start / Select |
| Esc | Quit |
| F5 / F9 | Quick save / quick load |
| F4 | Cycle widescreen |
| F6 | Toggle smoothing |
| F11 or Alt+Enter | Toggle fullscreen |

A second keyboard player can be set up on the launcher's Keyboard tab.

### Files kept beside the ROM

- `Zombies Ate My Neighbors.hiscore` holds the top scores.
- `Zombies Ate My Neighbors.sfc.quicksave` holds the quick save. There is one
  save slot, and F5 overwrites it.

### Running without the launcher

`build\zamn.exe` can be started directly. It reads the same `zamn.ini`, and
command-line options override it for that run, for example
`build\zamn.exe --skip-intro --level 5 --windowed`. `zamn.ini` is plain text,
with a comment on every setting.

## For developers

The port's design, progress and verification tools are described in
[`PLAN.md`](PLAN.md), [`PROGRESS.md`](PROGRESS.md) and the [`docs`](docs)
folder. The longer technical README this one replaced is in the Git history.

## Credits

- SNES core: [LakeSnes](https://github.com/elzo-d/LakeSnes) (MIT)
- ZAMN data format reverse engineering: [Necrofy](https://github.com/Piranhaplant/Necrofy)
- Architecture model: [zelda3](https://github.com/snesrev/zelda3)
- PNG writing and font rendering: [stb](https://github.com/nothings/stb) (public domain or MIT)
