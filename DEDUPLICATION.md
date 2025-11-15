# GLTF Deduplication Implementation

## Overview

The deduplication implementation in `gltfu` is based on the approach used by [gltf-transform](https://github.com/donmccurdy/glTF-Transform), providing memory-efficient removal of duplicate resources in GLTF files.

## Deduplication Strategy

### 1. Accessor Deduplication

**Purpose**: Remove duplicate vertex data, indices, and animation data.

**Algorithm**:
- Create hash groups based on accessor properties (count, type, componentType, normalized, sparse)
- Within each hash group, compare actual binary data byte-by-byte
- Handle different buffer strides correctly
- Update all references in:
  - Mesh primitive attributes
  - Mesh primitive indices
  - Morph target attributes
  - Animation samplers (input/output)
  - Skin inverse bind matrices

**Memory Efficiency**: O(n²) comparison within hash groups, but hash groups are typically small, reducing practical complexity.

### 2. Mesh Deduplication

**Purpose**: Remove duplicate mesh geometry.

**Algorithm**:
- Create a hash key for each mesh based on:
  - Primitive mode
  - Material index
  - Indices accessor index
  - All vertex attribute accessor indices (sorted for consistency)
  - Morph target attributes
- Meshes with identical keys are duplicates
- Update node references to point to retained meshes
- Remove duplicate meshes

**Key Insight**: By comparing accessor indices rather than data, we leverage accessor deduplication results.

### 3. Material Deduplication

**Purpose**: Remove duplicate materials.

**Algorithm**:
- Create hash based on all material properties:
  - PBR metallic-roughness values
  - Texture indices
  - Normal/occlusion/emissive properties
  - Alpha mode, cutoff, double-sided flag
- Materials with identical hashes are duplicates
- Update mesh primitive material references
- Remove duplicate materials

**Considerations**: Material extensions are included in the hash via tinygltf's material structure.

### 4. Texture/Image Deduplication

**Purpose**: Remove duplicate textures and images.

**Algorithm**:
1. **Image Deduplication** (first pass):
   - Compare image dimensions and MIME types
   - Byte-by-byte comparison of image data
   - Update texture source references

2. **Texture Deduplication** (second pass):
   - Compare texture source and sampler indices
   - Update all material texture references:
     - Base color texture
     - Metallic-roughness texture
     - Normal texture
     - Occlusion texture
     - Emissive texture

**Two-Pass Approach**: Images are deduplicated first, then textures, ensuring maximum reduction.

## Index Remapping

After removing duplicates, all indices must be updated:

1. Create an index remap array: `old_index → new_index`
2. Mark removed resources with `-1`
3. Update all references throughout the model
4. Remove marked resources from arrays

This ensures referential integrity across the entire GLTF model.

## Options

- **keepUniqueNames**: Preserve resources with unique names, even if data is identical
- **dedupAccessors**: Enable/disable accessor deduplication
- **dedupMeshes**: Enable/disable mesh deduplication
- **dedupMaterials**: Enable/disable material deduplication
- **dedupTextures**: Enable/disable texture/image deduplication
- **verbose**: Print detailed statistics

## Performance Characteristics

- **Time Complexity**: O(n²) for pairwise comparisons within each resource type
- **Space Complexity**: O(n) for hash maps and remap arrays
- **Optimization**: Hash-based grouping reduces practical comparison count
- **Memory Usage**: In-place modification of model; no full duplication

## Comparison with gltf-transform

### Similarities:
- Hash-based grouping for accessors
- Byte-by-byte data comparison
- Multi-pass approach for textures/images
- Index remapping strategy

### Differences:
- **C++ vs TypeScript**: Native performance, better memory control
- **In-place modification**: Avoids graph abstraction overhead
- **Simplified API**: Single function call vs. transform pipeline

## Example Usage

```bash
# Deduplicate all resource types
./gltfu dedupe input.gltf -o output.gltf

# Deduplicate with statistics
./gltfu dedupe input.gltf -o output.gltf --verbose

# Deduplicate only geometry
./gltfu dedupe input.gltf -o output.gltf --accessors --meshes

# Keep named resources
./gltfu dedupe input.gltf -o output.gltf --keep-unique-names
```

## Future Optimizations

1. **Parallel Comparison**: Use threading for pairwise comparisons
2. **Progressive Hashing**: Use rolling hashes for large buffers
3. **Approximate Matching**: Option for tolerance-based deduplication
4. **Buffer View Optimization**: Deduplicate buffer views separately
5. **Extension Support**: Handle KHR_draco, KHR_meshopt, etc.

## References

- [gltf-transform dedup.ts](https://github.com/donmccurdy/glTF-Transform/blob/main/packages/functions/src/dedup.ts)
- [glTF 2.0 Specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
- [tinygltf](https://github.com/syoyo/tinygltf)
