# Join Implementation

## Overview

The `join` command combines compatible primitives within meshes to reduce draw calls and improve rendering performance. This implementation is based on the [gltf-transform join](https://github.com/donmccurdy/glTF-Transform/blob/main/packages/functions/src/join.ts) algorithm.

## Algorithm

The join operation works in several phases:

### 1. Compatibility Grouping

Primitives are grouped by compatibility criteria:
- **Material**: Same material reference
- **Mode**: Same drawing mode (TRIANGLES, LINES, POINTS, etc.)
- **Vertex Attributes**: Same attribute semantics, types, and component types
- **Indices**: All indexed or all non-indexed
- **Morph Targets**: Number and structure of morph targets (currently not supported)

A group key is generated for each primitive combining these characteristics. Primitives with the same key can be joined.

### 2. Vertex Remapping

For each primitive group to be joined:
- Build a remap array for each primitive: `remap[srcVertexIndex] → dstVertexIndex`
- Count total unique vertices across all primitives
- Handle both indexed and non-indexed primitives

### 3. Buffer Allocation

Create new buffers for the joined primitive:
- **Vertex Attributes**: Allocate space for total unique vertex count
- **Indices**: Allocate space for total index count (if indexed)
- Choose appropriate component type for indices based on vertex count:
  - `UNSIGNED_BYTE` if ≤ 255 vertices
  - `UNSIGNED_SHORT` if ≤ 65,535 vertices
  - `UNSIGNED_INT` otherwise

### 4. Data Remapping

Copy vertex data and indices from source primitives:
- **remapAttribute()**: Copy vertex attributes using the remap array
- **remapIndices()**: Remap and copy indices to new index buffer
- Handle different component types (BYTE, SHORT, INT, FLOAT)
- Handle different accessor types (SCALAR, VEC2, VEC3, VEC4, MAT2, MAT3, MAT4)

### 5. Primitive Replacement

- Add the new joined primitive to the mesh
- Remove the original primitives that were joined

## Usage

```bash
# Basic join operation
./gltfu join input.gltf -o output.gltf

# Keep meshes separate (only join primitives within same mesh)
./gltfu join input.gltf -o output.gltf --keep-meshes

# Keep named meshes separate
./gltfu join input.gltf -o output.gltf --keep-named

# Combine with other options
./gltfu join input.gltf -o output.glb --binary --embed-images
```

## Implementation Files

- **src/gltf_join.h**: Header file with `GltfJoin` class and `JoinOptions` struct
- **src/gltf_join.cpp**: ~530 lines implementing the join algorithm
- **src/main.cpp**: CLI integration for the join command

## Key Functions

### `createPrimGroupKey()`
Generates a unique string key identifying a primitive's compatibility group based on material, mode, attributes, and targets.

### `joinPrimitives()`
Main joining logic:
1. Validate compatibility
2. Build vertex remaps for each primitive
3. Calculate total vertex and index counts
4. Allocate new buffers and accessors
5. Copy and remap vertex data and indices
6. Return the new joined primitive

### `remapAttribute()`
Copies vertex attribute data from source to destination, applying vertex index remapping. Handles both indexed and non-indexed primitives.

### `remapIndices()`
Remaps and copies index data, translating source vertex indices to destination vertex indices using the remap array.

## Limitations

Current implementation limitations:
- **Morph Targets**: Primitives with morph targets are skipped
- **Animations**: Animated nodes are not currently considered (would need scene graph traversal)
- **Skinning**: Skinned meshes are not currently considered
- **Extensions**: Volumetric materials and instancing are not supported
- **Cross-Mesh Joining**: Currently only joins within same mesh (--keep-meshes is implicit)

## Benefits

Joining primitives provides several advantages:
- **Reduced Draw Calls**: Fewer primitives means fewer draw calls during rendering
- **Better GPU Utilization**: Larger batches can be processed more efficiently
- **Simpler Scene Graph**: Fewer mesh instances to manage
- **Smaller File Size**: Reduced overhead from primitive structures

## Considerations

- **Memory**: Joining creates new buffers; ensure adequate memory is available
- **Vertex Count**: Large vertex counts may require 32-bit indices
- **Compatibility**: Only compatible primitives are joined; incompatible primitives remain unchanged
- **Name Preservation**: Use `--keep-named` to preserve intentional mesh organization

## Comparison with gltf-transform

This implementation follows the same algorithm as gltf-transform's `joinPrimitives()` function:
- Compatible primitive grouping by createPrimGroupKey
- Vertex deduplication within each primitive
- Remap array construction for efficient copying
- Buffer creation with appropriate component types
- Sequential attribute and index remapping

Key differences:
- Implemented in C++ instead of TypeScript
- Uses tinygltf instead of @gltf-transform/core
- Currently limited to same-mesh joining (scene graph traversal not implemented)
- No automatic flatten or dedup preprocessing

## Best Practices

For optimal results:
1. Run `dedupe` first to maximize joining opportunities
2. Run `flatten` to simplify scene hierarchy
3. Use `join` to reduce draw calls
4. Consider `--keep-named` to preserve intentional organization

Example workflow:
```bash
./gltfu dedupe input.gltf -o temp1.gltf
./gltfu flatten temp1.gltf -o temp2.gltf
./gltfu join temp2.gltf -o output.gltf
```

## Future Enhancements

Potential improvements:
- Cross-mesh joining with scene graph traversal
- Animation and skinning support
- Morph target preservation
- Parallel processing for large meshes
- Integration with weld/compaction
- Draco compression support
