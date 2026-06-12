# Fix Stale Struct Pins

A UE4.27 **editor plugin** that repairs Blueprint struct nodes after a User Defined Struct (UDS)
is modified and reconformed.

## The problem

When you change a UDS, the engine reconforms `Break Struct`, `Make Struct`, and
`Set members in Struct` nodes. In some cases a node ends up with **duplicate pin pairs** for the
same field:

- an **old, orphaned** pin (stale GUID) that still holds your wire connections and default values, and
- a **new, correct** pin (reconformed GUID) that is empty.

The result is broken or silently disconnected logic that's tedious to fix by hand across many graphs.

## What it does

For every `Break/Make/SetFields` node in a Blueprint, this plugin groups same-named pins (per
direction), and when it finds a stale/new pair it:

1. Transfers all link connections from the stale pin to the reconformed pin.
2. Copies over default value, default object, and default text if the new pin is empty.
3. Recompiles the Blueprint and marks the package dirty.

Exec pins, the main struct pin, and expanded sub-pins are skipped.

## Usage

There are two entry points:

- **Menu:** *Tools → Fix Stale Struct Pins...* (reminds you of the console command).
- **Console command:**
  ```
  FixStaleStructPins /Game/Path/Blueprint.Blueprint
  ```
  Open the **Output Log** first so you can see what was transferred, then run the command with the
  object path of the Blueprint to fix.

After it runs, check the log for `Transferring N link(s)` lines and a `Fixed N stale pin
connection(s)` summary - then **save** the Blueprint.

## Installation

1. Copy this folder into `<YourProject>/Plugins/FixStaleStructPins/`.
2. Regenerate project files and build (Development Editor, Win64).
3. Launch the editor; the *Tools* menu entry and console command register automatically.

## Notes

- Editor-only module, loaded in the Default phase.
- It only acts on pairs where exactly one pin is connected and its twin is empty; ambiguous cases
  (both connected / both empty) are left untouched.
- Marked beta - review results in the log and save manually.

## License

MIT — see [LICENSE](LICENSE).
