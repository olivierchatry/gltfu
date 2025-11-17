# gltfu — glTF Utilities

A command-line toolkit for inspecting and optimizing glTF/GLB assets with a memory-friendly workflow.

## Highlights

- Merge several glTF scenes into one output file.
- Remove duplicate meshes, materials, textures, and buffers.
- Flatten node hierarchies and join compatible primitives to cut draw calls.
- Weld vertices, simplify geometry, and optionally apply Draco compression.
- Run the full pipeline with a single `optim` command or inspect models with `info`.

## Build From Source

- C++17 compiler (GCC 7+, Clang 5+, MSVC 2019+, Apple Clang 10+).
- CMake 3.15+.
- Draco compression is enabled by default; disable with `-DGLTFU_ENABLE_DRACO=OFF` if you do not need it.

```bash
git clone https://github.com/olivierchatry/gltfu.git
cd gltfu
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/gltfu --help
```

## Usage

`gltfu <command> [options]` — run `gltfu <command> --help` for the full list. Most commands accept `--embed-images`, `--embed-buffers`, and `--no-pretty-print`; GLB output is auto-detected but can be forced with the command’s `--binary` (or `-b,--binary`) flag. Global flag: `--json-progress` for machine-readable progress messages.

### Commands

- **merge** `gltfu merge <inputs...> -o <output>` — combine files; supports `--keep-scenes`, `--default-scene-only`, `--scenes <indices>` (currently prints a warning), and the standard embed/binary switches.
- **dedupe** `gltfu dedupe <input> -o <output>` — collapse duplicate resources; toggles `--accessors`, `--meshes`, `--materials`, `--textures`, plus `--keep-unique-names`, `-v,--verbose`, and output flags.
- **info** `gltfu info <input>` — print model statistics; add `-v,--verbose` for extended data.
- **flatten** `gltfu flatten <input> -o <output>` — collapse node hierarchy; optional `--no-cleanup` and output flags.
- **join** `gltfu join <input> -o <output>` — merge compatible primitives; use `--keep-meshes` to stay within a mesh, `--keep-named` to skip named meshes/nodes, and `-v,--verbose` for a per-mesh summary.
- **weld** `gltfu weld <input> -o <output>` — deduplicate vertices; `--overwrite` replaces index buffers in place.
- **prune** `gltfu prune <input> -o <output>` — drop unused resources; control with `--keep-leaves`, `--keep-attributes`, `--keep-extras`, and `-v,--verbose` for a removal summary.
- **simplify** `gltfu simplify <input> -o <output>` — meshoptimizer-based decimation with `-r,--ratio`, `-e,--error`, `-l,--lock-border`, `-v,--verbose`, and the usual output flags.
- **optim** `gltfu optim <inputs...> -o <output>` — run merge → dedupe → flatten → join → weld → prune, with optional `--simplify`, `--simplify-ratio`, `--simplify-error`, `--simplify-lock-border`, and (when built with Draco) `--compress` plus the `--compress-*-bits` knobs. Skip stages via `--skip-dedupe`, `--skip-flatten`, `--skip-join`, `--skip-weld`, or `--skip-prune`. Add `-v,--verbose` for per-stage stats.

### Examples

```bash
# Inspect a file
gltfu info model.glb

# Join compatible primitives while preserving named meshes
gltfu join scene.gltf -o scene_joined.glb --keep-named

# Full optimization with simplification and Draco compression
gltfu optim large_scene.gltf -o large_scene_optimized.glb \
  --simplify --simplify-ratio 0.5 --compress -v
```

## License

See `LICENSE` for details.

## Credits

- [TinyGLTF](https://github.com/syoyo/tinygltf) — glTF parsing.
- [Draco](https://github.com/google/draco) — optional mesh compression.
- [meshoptimizer](https://github.com/zeux/meshoptimizer) — mesh simplification and welding.
- [CLI11](https://github.com/CLIUtils/CLI11) — command-line parsing.
