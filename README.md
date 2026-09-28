# Zombies Ate My Neighbors PC Port

An unofficial PC port of the 1993 SNES game *[Zombies Ate My Neighbors](https://en.wikipedia.org/wiki/Zombies_Ate_My_Neighbors)*, currently a work in progress.

It plays like the original game, with
some additional features:

* Widescreen support (16:9, 16:10, and 21:9)
* Smoother motion on high refresh rate displays (120 Hz, 144 Hz, 240 Hz)
* USB controller support for one or two players, with twin-stick aiming and firing on the right stick
* Item and weapon switching in both directions on the shoulders and triggers
* Increased hitbox sizes for neighbors and items
* Quick save and quick load
* Top scores are saved between sessions
* Skip the intro, or start the game on any level
* Sound effects that get cut off in the original game during busy gameplay now play
* Optional cheats: invincibility, give all weapons / items, infinite ammo / uses, and more

## ROM (Required)

No game data is included.

You will need to obtain a copy of the original ROM:

| | |
| --- | --- |
| Region | USA |
| Size | 1,048,576 bytes |
| MD5 | `23c2af7897d9384c4791189b68c142eb` |
| SHA-1 | `1be79496d7a38b293d6a5d9fba0e16f3cb37d5ff` |
| SHA-256 | `b27e2e957fa760f4f483e2af30e03062034a6c0066984f2e284cc2cb430b2059` |

## Generative AI Usage

This port is exclusively developed using Claude Code and Codex.

I usually select the latest or largest model for each, but set to medium reasoning in order to save on token usage.

## Port Progress

This port is very much a work in progress.

The game's logic is being rewritten in C and validated against original ROM execution.

Some of the game is still being emulated, and only a very small portion of what is running natively has been translated into "readable" C.

My more immediate goal was to have an enjoyable, playable version of the game that I could run on my desktop with some additional modern features.

Longer term, I would love to see this port completed by becoming fully translated, but I'm not committing to that up front. I plan to work on it occasionally, but only for as long as I find it enjoyable. It already meets 

## Playing

Download the latest release. (COMING SOON!)

After extracting, run **`zamn_launcher.exe`**.

Choose your ROM file under game settings, change any other setting you like, then press **Play**. 

The launcher saves your settings to `zamn.ini`.

Be warned that playing in widescreen makes the neighbors more vulnerable as they are exposed sooner and can remain visible for longer on the screen.

### Files Created Beside the ROM

- `*.hiscore` holds the top scores.
- `*.quicksave` holds the quick save. There is one save slot, and F5 overwrites it.

## Credits

- SNES core: [LakeSnes](https://github.com/elzo-d/LakeSnes) (MIT)
- ZAMN data format reverse engineering: [Necrofy](https://github.com/Piranhaplant/Necrofy)
- Architecture model: [zelda3](https://github.com/snesrev/zelda3)
- PNG writing and font rendering: [stb](https://github.com/nothings/stb) (public domain or MIT)
