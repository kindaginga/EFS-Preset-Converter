# EFS Preset Converter

An F4SE plugin for Fallout 4. Extended Facial Sculpting REDUX gives the male extended sculpts new IDs; this plugin converts the old male sculpt IDs in LooksMenu presets as they load, so presets made with the original Extended Facial Sculpting or Scrithel's edition keep their look under REDUX.

Released on Nexus Mods by [kindaginga](https://www.nexusmods.com/profile/kindaginga/mods).

## Parts

| Folder | What it is |
|---|---|
| `src/` | The plugin: the preset hook and the conversion of sculpt IDs |
| `tests/` | A command-line tool that runs the conversion on a preset file without the game |
| `cmake/` | The version header and the version resource the build stamps into the DLL |
| `external/CommonLibF4RD/` | The vendored game library (MIT), with the patches listed in its `VENDORED.txt` |

## Player requirements

- F4SE for the game version
- LooksMenu
- Address Library for F4SE Plugins (All In One) or Runtime Database, either one

## Building

Windows, Visual Studio 2026, CMake and vcpkg with `VCPKG_ROOT` set.

```
cmake --preset vs2026
cmake --build --preset vs2026-release
```

The release DLL is `build/vs2026/Release/EFSPresetConverter.dll`.

## License

All rights reserved. See `LICENSE`. Third-party components keep their own licenses.
