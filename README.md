# Moddy's UE4 Modding Plugins

A collection of small, single-purpose **Unreal Engine 4.27 editor plugins** built for
modding workflows (originally for *Voices of the Void*). Each plugin is self-contained and
can be dropped into a project's `Plugins/` folder independently.

## Plugins

| Plugin | Category | What it does |
|---|---|---|
| [FixStaleStructPins](FixStaleStructPins/) | Editor | Repairs Blueprint Break/Make/SetFields nodes that grow duplicate pin pairs after a User Defined Struct is reconformed, transferring connections and defaults from the stale pin to the new one. |
| [StructFixup](StructFixup/) | Editor | Comprehensive User Defined Struct reinstancing. Hooks struct change events to migrate instance data, force-recompile dependent Blueprints, and rebuild DataTables so "modify struct → objects break" stops happening. |
| [ModPackager](ModPackager/) | Modding | Right-click a content folder → **Package Mod**: cooks only that folder and produces a deployable `<ModName>.pak`. No per-mod maps, chunk IDs, or full-project recook. |

## Requirements

- Unreal Engine **4.27** (editor builds; these are editor-only modules)
- A C++ project (the plugins compile a small editor module each)

## Installation

1. Copy the plugin folder you want into your project's `Plugins/` directory:
   ```
   <YourProject>/Plugins/<PluginName>/
   ```
2. Regenerate project files (right-click the `.uproject` → *Generate Visual Studio project files*).
3. Build the project (Development Editor, Win64).
4. Launch the editor and enable the plugin under **Edit → Plugins** if needed.

See each plugin's own README for usage details.

## Repository layout

```
Various-Small/
├─ FixStaleStructPins/   # Stale struct pin repair
├─ ModPackager/          # Per-folder mod cooking + paking
└─ StructFixup/          # User Defined Struct reinstancing
```

## License

MIT — see [LICENSE](LICENSE). Each plugin folder also includes its own copy of the license.
