# gltfu - GLTF Utilities

A memory-efficient C++ CLI tool for GLTF operations including merge, deduplication, joining, and compression.

## Features

- **Info**: Display detailed information about GLTF files
- **Merge**: Combine multiple GLTF files and scenes into one
- **Deduplicate**: Remove duplicate accessors, meshes, materials, and textures to reduce file size
- **Flatten**: Flatten scene graph hierarchy while preserving skeletons and animations
- **Join**: Combine compatible primitives to reduce draw calls
- **Weld**: Merge identical vertices to reduce geometry size
- **Simplify**: Reduce mesh complexity using meshoptimizer
- **Prune**: Remove unused resources not referenced by any scene
- **Progress Reporting**: JSON streaming progress updates for all operations
- **Memory Efficient**: Optimized for handling large GLTF files with minimal memory overhead
- **Fast**: Native C++ performance with O(n) deduplication using xxHash

## Building

### Prerequisites

- CMake 3.15 or higher
- C++17 compatible compiler
- curl (for downloading dependencies)

### Download Dependencies

All dependencies are header-only libraries with specific release versions:

```bash
./download_deps.sh
```

This downloads:
- tinygltf v2.9.3
- CLI11 v2.6.1
- nlohmann/json v3.11.3
- meshoptimizer v0.21
- xxHash v0.8.2

### Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Usage

### Merge GLTF files

```bash
./gltfu merge -o output.gltf input1.gltf input2.gltf input3.gltf

# Output as binary GLB (auto-detected from extension)
./gltfu merge -o output.glb input1.gltf input2.gltf input3.gltf

# Or explicitly specify binary format
./gltfu merge -o output.gltf input1.gltf input2.gltf --binary
```

**Note:** All commands automatically detect binary GLB format from the `.glb` file extension. GLB files are self-contained with all buffers embedded in the binary chunk - no separate `.bin` files are created.

### Display file information

View comprehensive statistics about a GLTF file:

```bash
# Basic info
./gltfu info input.gltf

# Detailed info with verbose flag
./gltfu info input.gltf --verbose
```

The info command displays:
- File size and format (GLTF/GLB)
- Asset metadata (version, generator, copyright)
- Scene structure (scenes, nodes, cameras, lights)
- Mesh statistics (meshes, primitives, triangles, vertices)
- Material counts (materials, textures, images)
- Animation and skin counts
- Memory usage breakdown (buffer bytes, image bytes, total)

With `--verbose`, also shows:
- Accessor, buffer view, buffer counts
- Sampler counts
- Copyright information

### Merge specific scenes

```bash
./gltfu merge -o output.gltf --scenes 0,1,2 input.gltf
```

### Deduplicate GLTF file

Remove duplicate resources to reduce file size:

```bash
./gltfu dedupe input.gltf -o output.gltf
```

### Deduplicate with options

```bash
# Deduplicate only specific resource types
./gltfu dedupe input.gltf -o output.gltf --accessors --meshes --no-materials

# Keep resources with unique names
./gltf dedupe input.gltf -o output.gltf --keep-unique-names

# Verbose output with statistics
./gltfu dedupe input.gltf -o output.gltf --verbose

# Output as binary GLB format
./gltfu dedupe input.gltf -o output.glb --binary
```

### Flatten scene graph

Flatten the scene hierarchy while preserving skeletons and animations:

```bash
./gltfu flatten input.gltf -o output.gltf
```

### Join primitives

Combine compatible primitives to reduce draw calls:

```bash
# Basic join
./gltfu join input.gltf -o output.gltf

# Keep meshes separate (only join within same mesh)
./gltfu join input.gltf -o output.gltf --keep-meshes

# Keep named meshes separate
./gltfu join input.gltf -o output.gltf --keep-named
```

### Weld vertices

Merge identical vertices to reduce geometry size:

```bash
# Basic weld
./gltfu weld input.gltf -o output.gltf

# Overwrite existing indices (optimize pre-indexed meshes)
./gltfu weld input.gltf -o output.gltf --overwrite
```

### Prune unused resources

Remove all resources not referenced by any scene:

```bash
# Basic prune
./gltfu prune input.gltf -o output.gltf

# Keep empty leaf nodes
./gltfu prune input.gltf -o output.gltf --keep-leaves

# Keep unused vertex attributes
./gltfu prune input.gltf -o output.gltf --keep-attributes
```

### Simplify meshes

Reduce mesh complexity using meshoptimizer:

```bash
# Basic simplify to 50% triangles
./gltfu simplify input.gltf -o output.gltf

# Aggressive simplification to 25%
./gltfu simplify input.gltf -o output.gltf --ratio 0.25

# High quality simplification with tight error bound
./gltfu simplify input.gltf -o output.gltf --ratio 0.75 --error 0.001

# Lock borders for terrain chunks
./gltfu simplify input.gltf -o output.gltf --lock-border
```

### Optimization Pipeline

For best results, combine operations:

```bash
# Full optimization: Deduplicate → Flatten → Join → Weld → Simplify → Prune
./gltfu dedupe input.gltf -o temp1.gltf
./gltfu flatten temp1.gltf -o temp2.gltf
./gltfu join temp2.gltf -o temp3.gltf
./gltfu weld temp3.gltf -o temp4.gltf
./gltfu simplify temp4.gltf -o temp5.gltf --ratio 0.5
./gltfu prune temp5.gltf -o output.gltf
```

### Progress Reporting

All commands support JSON streaming progress updates for integration with automation tools:

```bash
# Human-readable output (default)
./gltfu dedupe input.gltf -o output.gltf

# JSON progress stream - one JSON object per line
./gltfu --json-progress dedupe input.gltf -o output.gltf
```

JSON progress format:
```json
{"type":"progress","operation":"dedupe-accessors","message":"Computing content hashes","progress":0.1}
{"type":"progress","operation":"dedupe-accessors","message":"Grouped into 1234 metadata buckets","progress":0.4}
{"type":"progress","operation":"dedupe-accessors","message":"Found 567 duplicates","progress":0.8}
{"type":"success","operation":"dedupe","message":"Successfully deduplicated to: output.gltf"}
```

Progress types:
- `progress`: Operation in progress with optional progress value (0.0-1.0)
- `error`: Operation failed with error message
- `success`: Operation completed successfully

This makes it easy to parse and display progress in UIs or automation pipelines.

## Documentation

- [DEDUPLICATION.md](DEDUPLICATION.md) - Detailed deduplication documentation
- [FLATTEN.md](FLATTEN.md) - Scene graph flattening documentation
- [JOIN.md](JOIN.md) - Primitive joining documentation
- [WELD.md](WELD.md) - Vertex welding documentation
- [SIMPLIFY.md](SIMPLIFY.md) - Mesh simplification documentation
- [PRUNE.md](PRUNE.md) - Unused resource removal documentation

## Dependencies

- [tinygltf](https://github.com/syoyo/tinygltf) - GLTF parsing and writing
- [CLI11](https://github.com/CLIUtils/CLI11) - Command-line parsing
- [meshoptimizer](https://github.com/zeux/meshoptimizer) - Mesh simplification algorithms
- [xxHash](https://github.com/Cyan4973/xxHash) - Extremely fast hashing for deduplication
