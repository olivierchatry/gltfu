# GLTF Flatten Implementation

## Overview
Flatten functionality has been successfully implemented for the gltfu CLI tool. This feature flattens the scene graph hierarchy by removing unnecessary node nesting while preserving skeleton joints and animated nodes.

## Implementation Details

### Algorithm (based on gltf-transform)
The flatten implementation follows the same approach as gltf-transform:

1. **Mark skeleton joints** - Identify all nodes that are part of skins
2. **Mark animated nodes** - Identify nodes with TRS (translation/rotation/scale) animations
3. **Mark descendants** - Recursively mark all descendants of joints and animated nodes
4. **Clear parent relationships** - For unconstrained nodes, move them directly under the scene root by:
   - Computing their world matrix
   - Removing them from their parent
   - Adding them to scene roots
   - Applying world transform to local TRS

### Files Created
- `src/gltf_flatten.h` - Header with class definition and helper methods
- `src/gltf_flatten.cpp` - Implementation of flatten algorithm
- Updated `src/main.cpp` - Added flatten CLI command
- Updated `CMakeLists.txt` - Added flatten sources

### Key Features
- Matrix mathematics for TRS composition/decomposition
- Quaternion-based rotations
- World matrix computation through parent chain traversal
- Preservation of animated node hierarchies
- Preservation of skeleton structures

## Usage

```bash
./gltfu flatten input.gltf -o output.gltf [OPTIONS]
```

### Options
- `--no-cleanup` - Skip removal of empty leaf nodes
- `--embed-images` - Embed images in output
- `--embed-buffers` - Embed buffers in output
- `--no-pretty-print` - Disable JSON pretty printing
- `--binary` - Write .glb output

## Build System Improvements

### Header-Only Dependencies
All dependencies are now managed as single-header files downloaded with specific release versions:

- **tinygltf** v2.9.3 - GLTF parser
- **CLI11** v2.6.1 - Command-line parser  
- **nlohmann/json** v3.11.3 - JSON library
- **stb_image** / **stb_image_write** - Image I/O

### No Package Manager Required
The project no longer requires vcpkg or any package manager. Simply run:

```bash
./download_deps.sh  # Downloads all dependencies
mkdir build && cd build
cmake ..
cmake --build .
```

### Implementation Strategy
To avoid multiple definition errors with header-only libraries:
- Created `src/tinygltf_impl.cpp` with `TINYGLTF_IMPLEMENTATION` defined
- All other files include headers without implementation macros
- Fixed `gltf_dedup.h` to use proper constructor initialization

## Next Steps
According to the original plan:
1. ✅ Project structure
2. ✅ Merge functionality
3. ✅ Deduplication
4. ✅ Flatten (current)
5. ⏳ Join (joining primitives within meshes)
6. ⏳ Compress

The join functionality should be implemented next, followed by compression.
