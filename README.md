<p align="center">
  <img src="docs/images/wayward-souls-logo.png" alt="Wayward Souls" width="578">
</p>

## Requirements

- A homebrew-enabled PS Vita with [VitaShell](https://github.com/TheOfficialFloW/VitaShell).
- [kubridge](https://github.com/bythos14/kubridge/releases) installed and enabled. Reboot after installing it.
- `libshacccg.suprx` in `ur0:data/` or `ur0:data/external/`. See [ShaRKF00D](https://github.com/OsirizX/ShaRKF00D) for extraction instructions. This file does **not** go in your taiHEN plugin configuration.
- **Android 1.12 ARMv7 APK — [download the APK ZIP here](https://archive.org/download/wayward-souls-data-1.12/wayward-souls-data-1.12.zip)** ([Internet Archive page](https://archive.org/details/wayward-souls-data-1.12)). Extract `wayward-souls-1.12.apk` from the ZIP. **No OBB or separate extracted data download is needed.**
- At least **200 MB free on `ux0:` before copying the APK**, with additional room for saves and logs.

## Installation

1. Download **wayward-souls-vita-0.16-rc1.zip** from [Releases](https://github.com/Cynostial/wayward-souls-vita/releases) and extract it on your computer. The automatic “Source code” downloads are for developers.
2. Copy `wayward.vpk` to the Vita and install it in VitaShell. Accept the extended-permissions prompt.
3. Download the **[Android 1.12 APK ZIP](https://archive.org/download/wayward-souls-data-1.12/wayward-souls-data-1.12.zip)** and extract it on your computer. Create `ux0:data/wayward/` if it does not exist, then copy **`wayward-souls-1.12.apk`** directly inside it:

   ```text
   ux0:data/wayward/
   └── wayward-souls-1.12.apk
   ```

   **Any filename works** as long as it ends in `.apk`; uppercase `.APK` also works. Do not put it in a subfolder. An XAPK, APKS or ZIP container must be unpacked first—renaming it does not convert it into an APK.

4. Exit VitaShell USB mode and launch **Wayward Souls**. Press **START** to prepare the game.
5. When **Ready to play** appears, press **START** again. Later launches skip preparation.

**Using USB on Windows?** If the Vita appears as `D:`, the folder is `D:\data\wayward\`. Do not create an extra `ux0` folder.

Preparation took about **33 seconds** in the clean device test. Keep the Vita powered while it runs. Press **Circle** to pause, wait for the setup screen, then close the app if needed. Reopen and press **START** to resume; completed files are checked and reused.

**Keep `ux0:data/wayward/source.apk`.** The game needs this file on every launch. If you used another filename, setup creates `source.apk` and also keeps your original. After successful preparation, you may delete that extra original copy, but **never delete `source.apk`**.

## Supported APK

**[Download the supported Android 1.12 APK (ZIP)](https://archive.org/download/wayward-souls-data-1.12/wayward-souls-data-1.12.zip)** · [Internet Archive page](https://archive.org/details/wayward-souls-data-1.12)

The archive contains only `wayward-souls-1.12.apk`. Extract the ZIP and copy that APK to `ux0:data/wayward/`; the port prepares the game data itself.

**The Vita port version and Android game version are different.** Port release **0.16-rc1** still requires Android **1.12**. Android **1.32** and other APK versions are not supported.

| Property | Required build |
| --- | --- |
| Android version | **1.12** |
| Version code | **1404952329** |
| Architecture | **ARMv7 / 32-bit** |
| Package | `com.noodlecake.waywardsouls` |
| File size | **46,616,908 bytes** |

The installer checks the **complete APK hash**, so another package with the same version label may still be rejected. Renaming a different APK will not make it compatible.

<details>
<summary>Supported APK SHA-256</summary>

```text
fc2c5b5150a40f781075963155cc3497e6ff25abbd7f71e2263e209319109f0b
```

</details>

Use the APK from your own supported Android installation or backup. The [Google Play listing](https://play.google.com/store/apps/details?id=com.noodlecake.waywardsouls) identifies the game, but compatibility with files currently delivered by the store has not been verified. Android game executables and playable game data are not included in this repository or release. PC and iOS files are not compatible substitutes.

## Controls

| Button | Action |
| --- | --- |
| D-pad / left stick | Move; navigate menus; aim targeted items |
| Cross (X) | Normal attack; confirm selection |
| Hold R, then release | Power attack |
| Square | Offensive ability |
| Triangle | Defensive ability; item details / equipment comparison in menus |
| L | Quick-item selection |
| START / SELECT | Open inventory / pause during gameplay |
| Circle | Interact during gameplay; back in menus; cancel targeting |
| START at the title | Begin |

Abilities vary by hero. Both touch panels are unused.

To use an item, open the **inventory**, highlight it, press **X** to select it, then press **X again** to use it. For targeted items, aim with the stick or D-pad and press **X** to cast.

For quick access, press **L**, choose an item, then press **X** to use it. **Circle** interacts with nearby objects independently of attacks. After an eligible death, **New Run** starts again with the same hero; the game's progression and unlock rules still apply.

Settings includes a **View** button for the Vita controls reference and an **FPS: ON/OFF** toggle. The counter appears in the bottom-left corner, starts disabled on a fresh installation, and remembers your preference.

## Saves and updates

Progress and preferences are stored in `ux0:data/wayward/Documents/` and `ux0:data/wayward/Library/`. Back up both folders to keep your progress. Pause before closing the application so the port can write its checkpoint. The game's original death and progression rules still apply.

To update, close the game and install the new VPK over the existing application. **Keep `ux0:data/wayward/` intact**, including `source.apk`, `Documents` and `Library`. Ordinary VPK updates do not require another import unless their release notes say otherwise. There is no automatic updater.

## Known limitations

- Startup is still slow: approximately **54 seconds to the title** in the recorded test, separate from first-time APK preparation.
- Busy scenes can have frame drops and occasional stalls. Full-game performance across every hero and area has not been verified.
- Google Play / Game Center services and score sharing are unavailable. The port runs offline.
- Save loading after a full restart and suspend/resume need broader device testing.

## Troubleshooting

| Problem | What to check |
| --- | --- |
| No APK found | Put the file directly in `ux0:data/wayward/`. Check for hidden extensions such as `.apk.zip`. |
| Multiple APKs found | Keep only one input APK. The retained `source.apk` is handled separately. |
| Unsupported APK | Check the version, file size and SHA-256 above. |
| Preparation was interrupted | Keep the APK, reopen the game, and press START to resume. |
| Not enough storage | Free space, then press START again. |
| App exits or fails to start | Check kubridge, the shader compiler path, and permission to run unsafe homebrew. Reboot after installing kubridge. |

Report problems through [GitHub Issues](https://github.com/Cynostial/wayward-souls-vita/issues). Include the port version, Vita model/firmware, hero, area, reproduction steps and any error code. For performance reports, enable the FPS counter and describe the action that causes the drop.

After closing the game, copy `ux0:data/wayward/run.log` **before launching again**, because the next launch replaces it. Include `setup.log` for import problems or `startup.log` for loading problems.

## Building

Install [VitaSDK](https://vitasdk.org/) and the libraries listed in `dependencies.json`. Their recorded fingerprints identify the versions used for this release. Python 3 is required.

```sh
python build.py --sdk /path/to/vitasdk --check
python build.py --sdk /path/to/vitasdk --jobs 4
```

The build produces `build/wayward.vpk`. No Android files are required to compile the launcher; the supported APK is still required to play. Modified builds can use `--allow-library-mismatch` to opt into different installed library versions, whose compatibility is unverified.

## Credits

- **Rocketcat Games** — Wayward Souls.
- **Noodlecake Studios** — the Android release.
- **VitaSDK, VitaGL, soLoader, kubridge, vitaShaRK, and their contributors** — the Vita tools and compatibility libraries.
- **Xiph.Org, the Android Open Source Project, Matthew Conte, and the other library authors** — audio, runtime helpers and memory allocation.
- **Typemade** — the Josefin Sans font used by setup.

Third-party notices are retained in `vendor/` and `licenses/`. The logo and shell artwork come from the original game. Game content, artwork and trademarks belong to their respective owners. This is an unofficial fan port, not affiliated with or endorsed by Rocketcat Games, Noodlecake Studios or Sony.
