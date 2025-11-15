# Simplify - Mesh Simplification

## Overview

The simplify feature reduces mesh complexity by decreasing the number of triangles and vertices while maintaining visual quality. This uses meshoptimizer's quadric error metric simplification algorithm to intelligently preserve the shape and appearance of 3D models.

## Implementation

This feature is implemented using the [meshoptimizer](https://github.com/zeux/meshoptimizer) library, specifically its mesh simplification algorithms. The implementation wraps meshoptimizer's `meshopt_simplify()` function to provide mesh reduction while maintaining quality.

## How It Works

The simplify operation performs the following steps:

1. **Process each primitive** - Iterates through all mesh primitives in the GLTF model
2. **Skip non-triangles** - Only processes triangle primitives (skips points, lines, etc.)
3. **Convert triangle strips/fans** - Converts non-plain triangle modes to TRIANGLES
4. **Extract geometry data** - Reads vertex positions and indices from GLTF buffers
5. **Run simplification** - Calls meshoptimizer with:
   - Target triangle count (based on ratio)
   - Error threshold (maximum geometric deviation)
   - Border locking option (preserves mesh boundaries)
6. **Optimize indices** - Uses smallest index type possible (BYTE, SHORT, or INT)
7. **Create new buffers** - Writes simplified indices to new GLTF buffers/accessors

## Algorithm Details

meshoptimizer's simplification uses **quadric error metrics**:
- Assigns error quadric to each vertex measuring deviation from adjacent triangles
- Iteratively collapses edges with minimal error increase
- Respects topology constraints (borders, non-manifold edges)
- Preserves vertex attributes during collapse

The algorithm is sophisticated (~2800+ lines of C++) and handles:
- Edge collapse with optimal target vertex placement
- Triangle flip detection to avoid mesh inversion
- Attribute interpolation preservation
- Component-based simplification to prevent mesh from falling apart
- Border locking to preserve mesh boundaries

## Options

```cpp
struct SimplifyOptions {
    float ratio = 0.5f;         // Target ratio (0-1) of triangles to keep
    float error = 0.01f;        // Maximum error threshold
    bool lockBorder = false;    // Lock mesh borders (preserve boundaries)
};
```

### ratio
- **Range**: 0.0 - 1.0
- **Default**: 0.5
- **Description**: Target fraction of triangles to retain
  - 0.5 = reduce to 50% of original triangle count
  - 0.25 = reduce to 25% of original triangle count
  - Lower ratios = more aggressive simplification

### error
- **Range**: > 0.0
- **Default**: 0.01
- **Description**: Maximum geometric error tolerance
  - Measured as fraction of mesh extents
  - 0.01 = allow 1% deviation
  - 0.001 = allow 0.1% deviation
  - Lower values = higher quality, less simplification

### lockBorder
- **Type**: Boolean
- **Default**: false
- **Description**: Prevent border vertices from moving
  - Useful for terrain chunks that must align
  - Preserves mesh boundaries for seamless tiling
  - May reduce simplification effectiveness

## Usage Examples

```bash
# Basic simplification to 50% triangles
./gltfu simplify input.gltf -o output.gltf

# Aggressive simplification to 25%
./gltfu simplify input.gltf -o output.gltf --ratio 0.25

# High quality simplification to 75% with tight error bound
./gltfu simplify input.gltf -o output.gltf --ratio 0.75 --error 0.001

# Lock borders for terrain chunks
./gltfu simplify input.gltf -o output.gltf --ratio 0.5 --lock-border

# Output as GLB
./gltfu simplify input.gltf -o output.glb --binary

# Combined with other optimizations (full pipeline)
./gltfu dedupe input.gltf -o temp1.gltf
./gltfu flatten temp1.gltf -o temp2.gltf
./gltfu join temp2.gltf -o temp3.gltf
./gltfu weld temp3.gltf -o temp4.gltf
./gltfu simplify temp4.gltf -o temp5.gltf --ratio 0.5
./gltfu prune temp5.gltf -o output.gltf
```

## Output

The command provides detailed feedback:

```
Simplifying meshes (ratio=0.5, error=0.01)...
  Simplified: 5000 → 2500 triangles (50%), error: 0.0087
  Simplified: 3000 → 1500 triangles (50%), error: 0.0092
  Skipping non-triangle primitive (mode=0)
Simplified 2/3 primitives (1 skipped)
Written to: output.gltf
```

## When to Use

Simplify is useful when:

- **Reducing file size** - Fewer triangles = smaller geometry buffers
- **Improving performance** - Less geometry to render
- **Creating LOD levels** - Generate lower detail versions
- **Mobile/web optimization** - Reduce complexity for constrained devices
- **Distant objects** - Far objects don't need full detail

Consider simplify **after**:
- `dedupe` - Removes duplicate resources
- `flatten` - Simplifies node hierarchy
- `join` - Merges meshes (more effective simplification on larger meshes)
- `weld` - Merges duplicate vertices (simplify works better on welded meshes)

Consider simplify **before**:
- `prune` - Removes unused resources created by simplification

## Performance Characteristics

- **Time Complexity**: O(n log n) where n = vertex count
- **Memory**: Requires temporary buffers for meshoptimizer operations
- **Quality**: High - quadric error metrics produce visually accurate results
- **Reduction**: Typically achieves target ratio ±5%
- **Limitations**: 
  - Only processes triangle primitives
  - Requires valid indices (all primitives must be indexed)
  - Error may prevent achieving very low ratios on small meshes

## Dependencies

This feature requires:
- **meshoptimizer** - Included in `third_party/`
  - `meshoptimizer.h` - Header with API declarations
  - `meshoptimizer_simplifier.cpp` - Simplification implementation (~2800 lines)
  - `meshoptimizer_allocator.cpp` - Memory allocator

The meshoptimizer code is compiled directly into the gltfu executable.

## Technical Notes

### Index Type Optimization

After simplification, the implementation automatically selects the smallest index type:
- **UNSIGNED_BYTE** (1 byte) if max index ≤ 255
- **UNSIGNED_SHORT** (2 bytes) if max index ≤ 65535
- **UNSIGNED_INT** (4 bytes) otherwise

This minimizes buffer sizes without requiring manual specification.

### Vertex Attributes

The current implementation:
- ✅ Preserves POSITION data (required for simplification)
- ✅ Updates indices to reference simplified vertex set
- ⚠️ Does not currently simplify vertex attributes (NORMAL, TEXCOORD, etc.)

For full attribute simplification, future versions could use `meshopt_simplifyWithAttributes()`.

### Triangle Strips/Fans

Non-TRIANGLES modes are automatically converted, but the conversion is currently basic. For optimal results, use models with plain triangle primitives.

## Alternative: gltf-transform

For even more features, consider [gltf-transform](https://github.com/donmccurdy/glTF-Transform):

```bash
npm install -g @gltf-transform/cli
gltf-transform simplify input.gltf output.gltf --ratio 0.5 --error 0.001
```

gltf-transform provides additional options like attribute simplification and weld integration.
