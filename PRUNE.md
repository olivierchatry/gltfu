# Prune - Remove Unused Resources

## Overview

The prune operation removes properties from a GLTF file if they are not referenced by any scene. This is different from deduplication - while dedupe removes duplicate copies of resources, prune removes resources that are completely unused (not reachable from any scene in the file).

## How It Works

Prune performs a **reachability analysis** starting from scenes:

1. **Mark Phase**: Starting from all scenes, recursively mark all reachable resources:
   - Nodes in scene hierarchies
   - Meshes, materials, and textures used by nodes
   - Accessors used by meshes (vertices, indices, morph targets)
   - Buffer views and buffers containing accessor data
   - Skins, cameras, and their dependencies
   - Animation targets and keyframe data

2. **Prune Phase**: Remove all resources that weren't marked as reachable

3. **Remap Phase**: Update all indices to account for removed resources

## What Gets Pruned

Prune can remove:

- **Nodes**: Empty leaf nodes not referenced by scenes
- **Meshes**: Meshes not used by any node
- **Materials**: Materials not used by any primitive
- **Textures**: Textures not used by any material
- **Images**: Images not used by any texture
- **Samplers**: Samplers not used by any texture
- **Accessors**: Accessors not used by primitives or animations
- **Buffer Views**: Buffer views not used by any accessor
- **Buffers**: Buffers not used by any buffer view
- **Skins**: Skins not used by any node
- **Cameras**: Cameras not used by any node

## Options

### `--keep-leaves`

Keep empty leaf nodes that have no mesh, skin, camera, or children. By default, these are removed iteratively until only nodes with actual content remain.

```bash
./gltfu prune input.gltf -o output.gltf --keep-leaves
```

### `--keep-attributes`

Keep all vertex attributes even if they're unused by the material. By default, prune removes:

- **TEXCOORD_n**: If no texture uses that texture coordinate index
- **TANGENT**: If material has no normal map
- **NORMAL**: If material uses `KHR_materials_unlit`
- **COLOR_1+**: Additional color attributes beyond COLOR_0

Always kept:
- **POSITION**: Always required
- **NORMAL**: Required for lit materials
- **COLOR_0**: Primary vertex color
- **JOINTS_n/WEIGHTS_n**: Required for skinning

```bash
./gltfu prune input.gltf -o output.gltf --keep-attributes
```

### `--keep-extras`

Prevent pruning of properties that have custom `extras` data. By default, extras are ignored when determining if a resource can be pruned.

```bash
./gltfu prune input.gltf -o output.gltf --keep-extras
```

## Usage Examples

### Basic Pruning

Remove all unused resources:

```bash
./gltfu prune input.gltf -o output.gltf
```

### Aggressive Cleanup

Remove everything unused including empty leaf nodes and unused vertex attributes:

```bash
./gltfu prune input.gltf -o output.gltf
```

### Conservative Pruning

Keep empty nodes and all vertex attributes:

```bash
./gltfu prune input.gltf -o output.gltf --keep-leaves --keep-attributes
```

### Preserve Custom Data

Keep resources that have custom extras:

```bash
./gltfu prune input.gltf -o output.gltf --keep-extras
```

## Common Use Cases

### After Node Removal

If you've detached or removed nodes from scenes, prune will clean up all orphaned resources:

```bash
# Manually remove a node from the scene
# Then prune to remove all resources that were only used by that node
./gltfu prune modified.gltf -o cleaned.gltf
```

### After Material Changes

If you've replaced materials, prune will remove the old unused materials and their textures:

```bash
./gltfu prune input.gltf -o output.gltf
```

### Optimization Pipeline

Use prune as the final cleanup step after other operations:

```bash
# Full optimization pipeline
./gltfu dedupe input.gltf -o temp1.gltf        # Remove duplicates
./gltfu flatten temp1.gltf -o temp2.gltf       # Flatten hierarchy
./gltfu join temp2.gltf -o temp3.gltf          # Join primitives
./gltfu weld temp3.gltf -o temp4.gltf          # Merge vertices
./gltfu prune temp4.gltf -o output.gltf        # Final cleanup
```

### File Size Reduction

Prune can significantly reduce file size if the GLTF contains many unused resources:

```bash
./gltfu prune bloated.gltf -o lean.gltf
```

## What Prune Doesn't Do

- **Remove duplicates**: Use `dedupe` for that
- **Merge vertices**: Use `weld` for that
- **Join primitives**: Use `join` for that
- **Flatten hierarchy**: Use `flatten` for that
- **Remove used resources**: Only removes resources completely unreachable from scenes

## Animations and Skins

Prune preserves all resources needed by animations and skins:

- Animation channel target nodes are marked as used
- Animation sampler accessors are marked as used
- Skin joint nodes are marked as used
- Inverse bind matrices accessors are marked as used

Even if a node isn't in the scene hierarchy, it will be preserved if it's animated or used as a skin joint.

## Performance

Prune is fast and memory-efficient:

- Single-pass marking phase using hash sets
- Linear-time removal and remapping
- No data copying until final resource arrays are rebuilt

## Relationship to Other Operations

- **Dedupe**: Removes duplicate resources (multiple copies of same data)
- **Prune**: Removes unused resources (zero references from scenes)
- **Together**: Run dedupe first to remove duplicates, then prune to remove unused

```bash
# Best practice: dedupe then prune
./gltfu dedupe input.gltf -o deduped.gltf
./gltfu prune deduped.gltf -o output.gltf
```

## Technical Details

### Reachability Algorithm

1. Start with all scene root nodes
2. Recursively traverse node hierarchy
3. For each node, mark:
   - The node itself
   - Its mesh and all primitive resources
   - Its skin and joint nodes
   - Its camera
   - All children recursively
4. Separately mark animation targets and data
5. Build index maps for remaining resources
6. Update all references to use new indices
7. Remove unmarked resources

### Attribute Pruning Logic

For each vertex attribute:

- **POSITION**: Always required
- **NORMAL**: Required unless material is unlit
- **TANGENT**: Required if material has normal texture
- **TEXCOORD_n**: Required if any material texture uses this UV index
- **COLOR_0**: Always kept
- **COLOR_1+**: Removed (not standard)
- **JOINTS_n/WEIGHTS_n**: Always kept (skinning)

This logic ensures the mesh will render correctly after pruning.
