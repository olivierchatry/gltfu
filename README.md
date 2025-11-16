# gltfu - glTF Utilities

A comprehensive command-line toolkit for optimizing and manipulating glTF/GLB 3D model files.

## Features

- **Merge** multiple glTF files into one
- **Deduplicate** identical resources (meshes, materials, textures, accessors)
- **Flatten** scene graph hierarchies
- **Join** compatible primitives to reduce draw calls
- **Weld** identical vertices
- **Compress** meshes with Draco compression
- **Simplify** meshes to reduce triangle count
- **Prune** unused resources
- **Optimize** complete pipeline combining all operations
- **Info** display detailed file statistics

## Building from Source

### Prerequisites

- **C++17 compiler** (GCC 7+, Clang 5+, MSVC 2017+, or Apple Clang 10+)
- **CMake 3.15+**
- **Git** (for cloning submodules)

### Platform-Specific Setup

#### macOS

```bash
# Install prerequisites using Homebrew
brew install cmake

# Xcode Command Line Tools (if not already installed)
xcode-select --install
```

#### Linux (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install build-essential cmake git
```

#### Linux (Fedora/RHEL)

```bash
sudo dnf install gcc-c++ cmake git
```

#### Windows

- Install [Visual Studio 2019 or later](https://visualstudio.microsoft.com/) with C++ desktop development
- Install [CMake](https://cmake.org/download/) (or via `winget install Kitware.CMake`)
- Install [Git](https://git-scm.com/download/win)

### Build Steps

1. **Clone the repository with submodules:**

```bash
git clone --recursive https://github.com/olivierchatry/gltfu.git
cd gltfu
```

If you already cloned without `--recursive`, initialize submodules:

```bash
git submodule update --init --recursive
```

2. **Create build directory:**

```bash
mkdir build
cd build
```

3. **Configure with CMake:**

```bash
# Default configuration (with Draco compression enabled)
cmake ..

# Or specify build type explicitly
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build without Draco support
cmake -DGLTFU_ENABLE_DRACO=OFF ..
```

4. **Build the project:**

```bash
# Linux/macOS
cmake --build . --config Release

# Or use make directly
make -j$(nproc)  # Linux
make -j$(sysctl -n hw.ncpu)  # macOS

# Windows (Visual Studio)
cmake --build . --config Release
```

5. **Install (optional):**

```bash
# Install to system (may require sudo on Linux/macOS)
sudo cmake --install .

# Or install to custom prefix
cmake --install . --prefix /custom/install/path
```

The binary will be located at `build/gltfu` (or `build/Release/gltfu.exe` on Windows).

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `GLTFU_ENABLE_DRACO` | `ON` | Enable Draco mesh compression support |
| `CMAKE_BUILD_TYPE` | `Debug` | Build type: `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` |
| `CMAKE_INSTALL_PREFIX` | System default | Installation directory prefix |

**Examples:**

```bash
# Release build with Draco
cmake -DCMAKE_BUILD_TYPE=Release ..

# Debug build without Draco
cmake -DCMAKE_BUILD_TYPE=Debug -DGLTFU_ENABLE_DRACO=OFF ..

# Small executable optimized for size
cmake -DCMAKE_BUILD_TYPE=MinSizeRel ..
```

### Troubleshooting

**Problem: Submodules not found**
```bash
git submodule update --init --recursive
```

**Problem: CMake version too old**
```bash
# macOS
brew upgrade cmake

# Linux (download latest from cmake.org if apt version is old)
wget https://github.com/Kitware/CMake/releases/download/v3.28.1/cmake-3.28.1-linux-x86_64.sh
sudo sh cmake-3.28.1-linux-x86_64.sh --prefix=/usr/local --skip-license
```

**Problem: C++17 not supported**
- Update your compiler to a recent version (GCC 7+, Clang 5+, or MSVC 2017+)

**Problem: Build fails on Windows**
- Ensure you're using Visual Studio 2019 or later with C++ components
- Try building from "Developer Command Prompt for VS"

### Running Tests

```bash
# From build directory
ctest --output-on-failure

# Or run specific test
./gltfu info ../test/models/sample.gltf
```

### Development Build

For development with faster iteration:

```bash
mkdir build-debug
cd build-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .

# Enable verbose build output
cmake --build . --verbose
```

## Commands

### Global Options

- `--json-progress` - Output progress information in JSON format (for integration with other tools)

### merge

Combine multiple glTF files into a single file.

```bash
gltfu merge <input1.gltf> [input2.gltf ...] -o <output.gltf> [options]
```

**Options:**
- `-o, --output <file>` - Output file path (required)
- `--keep-scenes` - Keep scenes separate instead of merging them
- `--default-scenes-only` - Only include default scenes from each file
- `--scenes <indices>` - Select specific scenes by index (comma-separated, not yet implemented)
- `--embed-images` - Embed images as data URIs in the output
- `--embed-buffers` - Embed buffers as data URIs in the output
- `--no-pretty-print` - Disable JSON formatting (minify output)
- `-b, --binary` - Write output as GLB format (auto-detected from `.glb` extension)

**Example:**
```bash
gltfu merge model1.gltf model2.gltf model3.gltf -o combined.gltf
gltfu merge *.gltf -o scene.glb --embed-images
```

---

### dedupe

Remove duplicate data to reduce file size by identifying and merging identical resources.

```bash
gltfu dedupe <input.gltf> -o <output.gltf> [options]
```

**Options:**
- `-o, --output <file>` - Output file path (required)
- `--accessors` - Remove duplicate accessors (default: enabled)
- `--meshes` - Remove duplicate meshes (default: enabled)
- `--materials` - Remove duplicate materials (default: enabled)
- `--textures` - Remove duplicate textures and images (default: enabled)
- `--keep-unique-names` - Preserve resources with unique names even if data is identical
- `-v, --verbose` - Print detailed deduplication statistics
- `--embed-images` - Embed images in output file
- `--embed-buffers` - Embed buffers in output file
- `--no-pretty-print` - Disable JSON formatting
- `-b, --binary` - Write output as GLB format

**Example:**
```bash
gltfu dedupe model.gltf -o model_deduped.gltf -v
gltfu dedupe input.gltf -o output.glb --keep-unique-names
```

---

### info

Display detailed information about a glTF file including statistics on nodes, meshes, materials, textures, and more.

```bash
gltfu info <input.gltf> [options]
```

**Options:**
- `-v, --verbose` - Show more detailed information including extension data

**Example:**
```bash
gltfu info model.gltf
gltfu info scene.glb -v
```

---

### flatten

Flatten the scene graph hierarchy by collapsing node transformations, making all nodes direct children of the root.

```bash
gltfu flatten <input.gltf> -o <output.gltf> [options]
```

**Options:**
- `-o, --output <file>` - Output file path (required)
- `--no-cleanup` - Skip removal of empty leaf nodes after flattening
- `--embed-images` - Embed images in output file
- `--embed-buffers` - Embed buffers in output file
- `--no-pretty-print` - Disable JSON formatting
- `--binary` - Write output as GLB format

**Example:**
```bash
gltfu flatten complex_hierarchy.gltf -o flattened.gltf
```

---

### join

Join compatible mesh primitives to reduce draw calls. Primitives with the same material and vertex attributes can be merged.

```bash
gltfu join <input.gltf> -o <output.gltf> [options]
```

**Options:**
- `-o, --output <file>` - Output file path (required)
- `--keep-meshes` - Only join primitives within the same mesh (don't merge across meshes)
- `--keep-named` - Preserve named meshes and nodes (don't join their primitives)
- `--embed-images` - Embed images in output file
- `--embed-buffers` - Embed buffers in output file
- `--no-pretty-print` - Disable JSON formatting
- `--binary` - Write output as GLB format

**Example:**
```bash
gltfu join model.gltf -o joined.gltf
gltfu join scene.gltf -o output.glb --keep-named
```

---

### weld

Merge identical vertices to reduce geometry size and improve GPU cache efficiency.

```bash
gltfu weld <input.gltf> -o <output.gltf> [options]
```

**Options:**
- `-o, --output <file>` - Output file path (required)
- `--overwrite` - Replace existing indices with optimized version
- `--embed-images` - Embed images in output file
- `--embed-buffers` - Embed buffers in output file
- `--no-pretty-print` - Disable JSON formatting
- `--binary` - Write output as GLB format

**Example:**
```bash
gltfu weld model.gltf -o welded.gltf --overwrite
```

---

### prune

Remove unused resources that are not referenced by any scene (orphaned nodes, meshes, materials, textures, etc.).

```bash
gltfu prune <input.gltf> -o <output.gltf> [options]
```

**Options:**
- `-o, --output <file>` - Output file path (required)
- `--keep-leaves` - Preserve empty leaf nodes
- `--keep-attributes` - Keep unused vertex attributes
- `--keep-extras` - Prevent pruning resources with custom `extras` data
- `--embed-images` - Embed images in output file
- `--embed-buffers` - Embed buffers in output file
- `--no-pretty-print` - Disable JSON formatting
- `--binary` - Write output as GLB format

**Example:**
```bash
gltfu prune bloated.gltf -o cleaned.gltf
gltfu prune model.gltf -o output.glb --keep-extras
```

---

### simplify

Reduce mesh complexity by decimating triangles while preserving visual quality using meshoptimizer.

```bash
gltfu simplify <input.gltf> -o <output.gltf> [options]
```

**Options:**
- `-o, --output <file>` - Output file path (required)
- `-r, --ratio <value>` - Target ratio of triangles to keep (0.0-1.0, default: 0.5)
- `-e, --error <value>` - Maximum error threshold (default: 0.01)
- `-l, --lock-border` - Lock border vertices to prevent mesh from shrinking at edges
- `--embed-images` - Embed images in output file
- `--embed-buffers` - Embed buffers in output file
- `--no-pretty-print` - Disable JSON formatting
- `-b, --binary` - Write output as GLB format

**Example:**
```bash
gltfu simplify high_poly.gltf -o low_poly.gltf --ratio 0.25
gltfu simplify model.gltf -o simplified.glb -r 0.5 -e 0.005 --lock-border
```

---

### compress

Apply Draco mesh compression using the KHR_draco_mesh_compression extension. This can dramatically reduce file size (typically 60-90% reduction).

```bash
gltfu compress <input.gltf> -o <output.gltf> [options]
```

**Options:**
- `-o, --output <file>` - Output file path (required)
- `--position-bits <bits>` - Quantization bits for positions (10-16, default: 14)
- `--normal-bits <bits>` - Quantization bits for normals (8-12, default: 10)
- `--texcoord-bits <bits>` - Quantization bits for texture coordinates (10-14, default: 12)
- `--color-bits <bits>` - Quantization bits for colors (6-10, default: 8)
- `--generic-bits <bits>` - Quantization bits for generic attributes (6-16, default: 8)
- `-v, --verbose` - Show detailed compression statistics
- `--embed-images` - Embed images in output file
- `--embed-buffers` - Embed buffers in output file
- `--no-pretty-print` - Disable JSON formatting
- `-b, --binary` - Write output as GLB format

**Quantization Explanation:**

Higher bit values preserve more precision but result in larger files:
- **Position bits**: Controls precision of vertex positions. 14 bits is a good balance.
- **Normal bits**: Controls precision of surface normals. 10 bits is usually sufficient.
- **Texcoord bits**: Controls precision of UV coordinates. 12 bits preserves good texture mapping.
- **Color bits**: Controls precision of vertex colors. 8 bits (256 values per channel) is typically adequate.

**Example:**
```bash
gltfu compress model.gltf -o compressed.glb
gltfu compress high_quality.gltf -o output.glb --position-bits 16 --normal-bits 12 -v
gltfu compress web_model.gltf -o tiny.glb --position-bits 12 --normal-bits 8
```

---

### optim

Run a complete optimization pipeline that combines multiple operations in the optimal order:

1. **Merge** (if multiple input files)
2. **Deduplicate** resources
3. **Flatten** scene graph
4. **Join** compatible primitives
5. **Weld** identical vertices
6. **Simplify** meshes (optional)
7. **Compress** with Draco (optional)
8. **Prune** unused resources
9. **Compute bounds** for accessors

```bash
gltfu optim <input.gltf> [input2.gltf ...] -o <output.gltf> [options]
```

**Options:**
- `-o, --output <file>` - Output file path (required)

**Simplification Options:**
- `--simplify` - Enable mesh simplification pass
- `--simplify-ratio <value>` - Target triangle ratio (0.0-1.0, default: 0.75)
- `--simplify-error <value>` - Error threshold (default: 0.01)
- `--simplify-lock-border` - Lock border vertices during simplification

**Compression Options (requires Draco):**
- `--compress` - Enable Draco mesh compression
- `--compress-position-bits <bits>` - Position quantization (10-16, default: 14)
- `--compress-normal-bits <bits>` - Normal quantization (8-12, default: 10)
- `--compress-texcoord-bits <bits>` - Texcoord quantization (10-14, default: 12)
- `--compress-color-bits <bits>` - Color quantization (6-10, default: 8)

**Pipeline Control:**
- `--skip-dedupe` - Skip deduplication pass
- `--skip-flatten` - Skip flattening pass
- `--skip-join` - Skip primitive joining pass
- `--skip-weld` - Skip vertex welding pass
- `--skip-prune` - Skip pruning pass

**Output Options:**
- `-v, --verbose` - Show detailed statistics for each optimization step
- `--embed-images` - Embed images in output file
- `--embed-buffers` - Embed buffers in output file
- `--no-pretty-print` - Disable JSON formatting
- `-b, --binary` - Write output as GLB format (auto-detected)

**Example:**
```bash
# Full optimization with compression
gltfu optim large_model.gltf -o optimized.glb --compress --simplify --ratio 0.5 -v

# Merge and optimize multiple files
gltfu optim model1.gltf model2.gltf model3.gltf -o combined_optimized.glb --compress

# Aggressive optimization for web delivery
gltfu optim input.gltf -o web_ready.glb \
  --compress \
  --compress-position-bits 12 \
  --compress-normal-bits 8 \
  --simplify \
  --simplify-ratio 0.3 \
  -v

# Conservative optimization (skip aggressive operations)
gltfu optim model.gltf -o output.glb --skip-weld --skip-simplify
```

---

## Output Formats

### GLB vs GLTF

- **GLB** (`.glb`) - Binary format with embedded resources, smaller file size, single file
- **GLTF** (`.gltf`) - JSON format with external resources or embedded data URIs, human-readable

The output format is automatically detected from the file extension. Use `--binary` to force GLB output.

### Embedding Resources

- `--embed-images` - Convert external image files to data URIs embedded in JSON
- `--embed-buffers` - Convert external buffer files to data URIs embedded in JSON

When writing GLB format, buffers are always embedded in the binary chunk (ignoring `--embed-buffers`).

---

## Common Workflows

### Web Optimization Pipeline

For models intended for web delivery (Three.js, Babylon.js, etc.):

```bash
gltfu optim input.gltf -o web_model.glb \
  --compress \
  --compress-position-bits 14 \
  --compress-normal-bits 10 \
  --simplify \
  --simplify-ratio 0.5 \
  -v
```

This typically achieves 70-90% file size reduction.

### Maximum Compression

For the smallest possible file size (with some quality loss):

```bash
gltfu optim input.gltf -o tiny.glb \
  --compress \
  --compress-position-bits 10 \
  --compress-normal-bits 8 \
  --compress-texcoord-bits 10 \
  --simplify \
  --simplify-ratio 0.25 \
  --simplify-error 0.02
```

### Conservative Optimization

For maintaining maximum quality while removing redundancy:

```bash
gltfu optim input.gltf -o cleaned.glb \
  --skip-simplify \
  --skip-weld \
  -v
```

### Batch Processing

Process multiple files:

```bash
for file in models/*.gltf; do
  gltfu optim "$file" -o "optimized/$(basename "$file" .gltf).glb" --compress -v
done
```

---

## Understanding the Optimization Pipeline

The `optim` command runs operations in a specific order for best results:

1. **Merge** - Combine multiple input files (if applicable)
2. **Dedupe** - Remove duplicate resources first to reduce data before processing
3. **Flatten** - Simplify scene graph to enable better primitive joining
4. **Join** - Merge compatible primitives to reduce draw calls
5. **Weld** - Remove duplicate vertices after primitives are joined
6. **Simplify** - (Optional) Reduce triangle count
7. **Compress** - (Optional) Apply Draco compression after all geometry is finalized
8. **Prune** - Remove unused resources created during optimization
9. **Bounds** - Compute accessor bounds (required by glTF spec)

Each step prepares the model for the next, maximizing overall optimization.

---

## Draco Compression Details

Draco compression uses the `KHR_draco_mesh_compression` glTF extension. Benefits:

- **60-90% file size reduction** compared to uncompressed geometry
- Geometry data is compressed with quantization and entropy encoding
- Metadata (scene structure, materials, textures) remains uncompressed
- Widely supported in modern 3D viewers and game engines

**Compatibility:**
- Three.js (with DRACOLoader)
- Babylon.js (built-in support)
- Unity (via glTFast)
- Unreal Engine (via glTF plugin)
- Most modern web browsers and 3D tools

**Performance:**
- Smaller downloads = faster loading
- Decompression is fast (hardware-accelerated on modern GPUs)
- Slight CPU cost for decompression (negligible on modern devices)

---

## Tips and Best Practices

1. **Always use `--verbose` during development** to understand what each operation is doing
2. **Test compression settings** on your target platform - some quantization is imperceptible
3. **Use `info` command** before and after optimization to compare statistics
4. **Keep source files** - optimization is lossy (especially simplification)
5. **GLB format is preferred** for web delivery (single file, smaller)
6. **Pipeline order matters** - use `optim` instead of chaining individual commands
7. **Validate output** using online glTF validators or your 3D viewer of choice
8. **For AR/VR**, consider higher quantization bits (14-16) to maintain quality at close viewing distances
9. **For distant objects**, aggressive compression (10-12 bits) is often acceptable

---

## Building Without Draco

If you don't need compression support:

```bash
cmake -DGLTFU_ENABLE_DRACO=OFF ..
```

The `compress` option will not be available in `optim` and the standalone `compress` command will not exist.

---

## License

See LICENSE file for details.

---

## Contributing

Contributions are welcome! Please ensure code follows existing style and includes tests.

---

## Credits

Built with:
- [TinyGLTF](https://github.com/syoyo/tinygltf) - glTF file parsing
- [Draco](https://github.com/google/draco) - Mesh compression
- [meshoptimizer](https://github.com/zeux/meshoptimizer) - Mesh optimization
- [CLI11](https://github.com/CLIUtils/CLI11) - Command-line parsing
