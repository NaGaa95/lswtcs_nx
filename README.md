## LEGO Star Wars: The Complete Saga — Nintendo Switch port

A wrapper/port of the Android release of LEGO Star Wars: The Complete Saga
(v2.0.2.02, build 20202). It loads the original game binary `libTTapp.so`,
patches it and runs it inside a minimal Android-like environment natively on
the Switch — the same so-loader approach as `gtactw_nx`, and modeled closely on
the PS Vita port ([gm666q/lswtcs-vita](https://github.com/gm666q/lswtcs-vita)).

No game code or assets are included in this repository.

### How to install

You need the **v2.0.2.02 (20202)** Android release.
extract from your own legally-owned copy:

* From `lib/arm64-v8a/libTTapp.so` — the 64-bit game binary.
* The four data packs, found inside the `assets/` of the install APKs as zip
  archives (`assetpack1`, `assetpack2`, `assetpack3`). Inside those zips the
  files live at
  `files/assetpacks/asset_<X>/20202/20202/assets/<X>.dat`:
  * `Audio.dat`, `Levels.dat`, `Others.dat`, `Textures.dat`

To install on the SD card:

1. Create a folder `lswtcs` inside `/switch/` on your SD card.
2. Copy `libTTapp.so` into `/switch/lswtcs/`.
3. Copy `Audio.dat`, `Levels.dat`, `Others.dat`, `Textures.dat` into
   `/switch/lswtcs/`.
4. Copy `lswtcs_nx.nro` into `/switch/lswtcs/`.

Resulting layout:

```
/switch/lswtcs/
  lswtcs_nx.nro
  libTTapp.so
  Audio.dat
  Levels.dat
  Others.dat
  Textures.dat
```

Saves are written by the game under `/switch/lswtcs/SavedGames/`.

### Notes

This will not run in applet/album mode — it needs the full memory of a game
override. Launch it by holding **R** while opening an installed title, or use a
forwarder.

The port reads `/switch/lswtcs/config.txt`, created on first run:

* `screen_width` / `screen_height` — render resolution; `-1` auto-picks
  1280x720 handheld / 1920x1080 docked.
* `language` — `-1` follows the console language; otherwise an index
  (`0` en, `1` fr, `2` de, `3` it, `4` es, `5` ja, `6` nl, `7` pt, `8` ru,
  `9` ko, `10` zh, `11` da).
* `show_fps` — `1` draws a small FPS counter in the top left corner

### How to build

You need devkitA64 + libnx and these portlibs (`pacman -S`):
`switch-sdl2`, `switch-mesa`, `switch-libdrm_nouveau`.

```
source $DEVKITPRO/switchvars.sh
make
```

The bundled OpenSL ES is the AOSP "Wilhelm" implementation
(from gm666q/opensles), built with `-DUSE_SDL -DUSE_OUTPUTMIXEXT -DLSWTCS`:
buffer-queue audio players are mixed in software and pulled by an SDL2 audio
callback.

### Credits

* TheOfficialFloW for the original Android so-loader (gtasa_vita).
* gm666q for the PS Vita port this one is modeled on, and the OpenSL ES build.
* fgsfds for max_nx / the Switch so-loader groundwork reused here.

### Legal

This project has no affiliation with TT Games, Warner Bros., Disney, or
Lucasfilm. "LEGO Star Wars: The Complete Saga" and related marks belong to
their respective owners. No assets or program code from the original game or
its Android port are included here. We do not condone piracy; users must own a
legal copy of the game.

Unless noted otherwise, the source in this repository is under the MIT License
(see LICENSE). The vendored `lib/opensles` is Apache-2.0 (AOSP).
