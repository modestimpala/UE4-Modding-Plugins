# Struct Fixup

A UE4.27 **editor plugin** that makes User Defined Struct (UDS) edits safe. It fixes the classic
"modify a struct → instances, Blueprints, and DataTables break" problem by reinstancing struct
data properly whenever a struct changes.

## The problem

When a UDS layout changes, the engine reconforms Blueprint-generated class instances, but other
holders of that struct data are not always handled cleanly:

- struct members on **native-class** objects and components,
- **DataTables** whose editor isn't currently open,
- dependent **Blueprints** that don't get force-recompiled.

This leads to lost data, stale defaults, and broken references after seemingly trivial struct edits.

## What it does

The module implements `INotifyOnStructChanged` and reacts around every struct change:

**PreChange** (while the struct still has its old layout):
- Calls `CleanBeforeStructChange` on every loaded DataTable using the struct, serialising row data safely.
- Backs up struct instance data on all non-Blueprint objects (native classes, components, etc.) using
  tagged-property serialization that keeps `UObject` references alive.

**PostChange** (after the new layout is applied):
- Restores backed-up instance data field-by-field (surviving fields map by property GUID; removed or
  retyped fields are skipped), re-initialising with new-layout defaults first.
- Force-recompiles dependent Blueprints.
- Rebuilds DataTables and refreshes open editors.

Blueprint-generated class instances are intentionally **skipped** - the engine already handles those
through `MarkBlueprintAsStructurallyModified`.

## Usage

- **Automatic:** once loaded, the plugin listens for UDS changes and fixes them as they happen.
- **Manual sweep:** *Tools → Struct Fixup → Fix All Struct References*, or the console command:
  ```
  StructFixup.FixAll
  ```
  This recompiles dirty structs, force-recompiles dependent Blueprints, and runs a full
  clean/restore/rebuild cycle on every DataTable for all loaded structs. A toast notification
  reports how many Blueprints and DataTables were processed.

## Installation

1. Copy this folder into `<YourProject>/Plugins/StructFixup/`.
2. Regenerate project files and build (Development Editor, Win64).
3. Launch the editor; the listener, *Tools* menu entry, and console command register automatically.

## Notes

- Editor-only module, loaded in the Default phase.
- Logs under the `LogStructFixup` category - useful for confirming what was backed up and restored.
- Marked beta. As always, keep your content under source control before bulk struct edits.

## License

MIT — see [LICENSE](LICENSE).
