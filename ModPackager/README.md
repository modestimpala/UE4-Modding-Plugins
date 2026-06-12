# Mod Packager

A UE4.27 **editor plugin** for building individual mod `.pak` files straight from the editor.

Right-click a mod folder in the Content Browser (e.g. `Mods/MyMod`) → **Package Mod**.
It cooks **only that folder**, builds `MyMod.pak`, and deploys it - no per-mod maps,
no `PrimaryAssetLabel` chunk IDs, no full-project recook.

## Pipeline (what "Package Mod" does)

1. **Save** dirty content packages (so the cook sees current edits).
2. **Clean** this mod's previously-cooked output (`Saved/Cooked/<Platform>/<Project>/Content/<modRel>`)
   so the pak only contains current assets. *(toggle: `bCleanModCookDirFirst`)*
3. **Scoped cook** - only this folder + its dependencies:
   ```
   UE4Editor-Cmd.exe "<Project>.uproject" -run=Cook -TargetPlatform=WindowsNoEditor ^
       -CookDir="<abs mod folder>" -unversioned -iterate -stdout -unattended -nopause
   ```
4. **Pak** - enumerates the cooked files, writes an UnrealPak response file with mount paths
   `../../../<Project>/Content/<modRel>/...`, then:
   ```
   UnrealPak.exe "<ModName>.pak" -create="<filelist>.txt" -compress
   ```
5. **Deploy** - prompts for a destination folder each run (default remembered in settings),
   copies and renames to `<ModFolderName>.pak`.

Progress shows as an editor toast; click the hyperlink to open the deploy folder (or the log on failure).
Per-mod logs are written to `<YourProject>/Saved/ModPackager/<ModName>.log`.

## Settings

**Project Settings → Plugins → Mod Packager** (saved to `Config/DefaultGame.ini`):

| Setting | Default | Notes |
|---|---|---|
| `DefaultDeployDirectory` | *(empty)* | Default target, e.g. your r2modman `...\shimloader\pak` or the game's `...\VotV\Content\Paks\LogicMods`. |
| `bPromptForDeployDirEachRun` | `true` | Folder picker every run (defaults to the above). |
| `bCompressPak` | `true` | UnrealPak `-compress`. |
| `bIterativeCook` | `true` | Skip unchanged assets on repeat builds. |
| `bCleanModCookDirFirst` | `true` | Guarantees the pak matches current folder contents. |
| `TargetPlatform` | `WindowsNoEditor` | |
| `ModsRootFolder` | `Mods` | "Package Mod" only appears for folders under `/Game/<this>`. |
| `ExtraCookerArgs` | *(empty)* | Advanced cook commandlet args. |

## Installation

1. Copy this folder into your project's plugins directory:
   ```
   <YourProject>/Plugins/ModPackager/
   ```
2. Regenerate project files (right-click the `.uproject` → *Generate Visual Studio project files*),
   open the solution, and **Build** (Development Editor, Win64). The module
   `UE4Editor-ModPackager.dll` is produced in `Plugins/ModPackager/Binaries/Win64/`.
3. Launch the editor and enable **Mod Packager** under **Edit → Plugins** if it isn't already.
4. Place your mod content under `Content/<ModsRootFolder>/<ModName>/` (default root: `Mods`),
   then right-click that folder → **Package Mod**.

### Deploying the compiled plugin to another project

If you build the plugin in one project and want to use it in another (e.g. a separate runtime
project), copy the `ModPackager.uplugin` file and the `Binaries/Win64/UE4Editor-ModPackager.*`
files into the target project's `Plugins/ModPackager/` folder, then restart its editor.

## Notes / gotchas

- The cook runs as a **separate** `UE4Editor-Cmd` process; you can keep the editor open.
- Base-game/shared assets your mod references are **not** packed (they live in the base game);
  the pak only contains files under the mod folder. This is the same behaviour as the chunk paks.
- If "Package Mod" doesn't appear, the menu section hook may differ in your build - switch the
  hook in `ModPackagerModule.cpp` to a ToolMenus extension of `ContentBrowser.FolderContextMenu`.

## License

MIT — see [LICENSE](LICENSE).
