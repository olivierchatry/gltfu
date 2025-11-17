#pragma once
#include "tiny_gltf.h"
#include <string>
#include <vector>

namespace gltfu {

/**
 * Flattens the scene graph by removing the node hierarchy where possible.
 * Nodes with meshes, cameras, and other attachments are moved directly under
 * the scene root. Skeleton joints and animated nodes are preserved in their
 * original hierarchy.
 */
class GltfFlatten {
public:
    /**
     * Process a GLTF model to flatten its scene graph.
     * 
     * @param model The GLTF model to flatten (modified in place)
     * @param cleanup If true, removes empty leaf nodes after flattening
     * @return Number of nodes flattened
     */
    static int process(tinygltf::Model& model, bool cleanup = true);
};

} // namespace gltfu
