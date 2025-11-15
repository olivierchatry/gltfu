#pragma once
#include "tiny_gltf.h"
#include <string>
#include <vector>
#include <set>

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

private:
    /**
     * Clear the parent of a node, moving it directly under the scene.
     * Applies inherited transforms to maintain world-space position.
     */
    static void clearNodeParent(tinygltf::Model& model, int nodeIndex);

    /**
     * Get the world matrix of a node by traversing up to root.
     */
    static std::vector<double> getWorldMatrix(const tinygltf::Model& model, int nodeIndex);

    /**
     * Find which scene(s) contain a given node.
     */
    static std::vector<int> listNodeScenes(const tinygltf::Model& model, int nodeIndex);

    /**
     * Get the parent node index, or -1 if node is at scene root.
     */
    static int getParentNode(const tinygltf::Model& model, int nodeIndex);

    /**
     * Multiply two 4x4 matrices in column-major order.
     */
    static std::vector<double> multiplyMatrices(const std::vector<double>& a, const std::vector<double>& b);

    /**
     * Convert TRS (translation, rotation, scale) to a 4x4 matrix.
     */
    static std::vector<double> getNodeMatrix(const tinygltf::Node& node);

    /**
     * Set node matrix from a 4x4 matrix (decomposes to TRS).
     */
    static void setNodeMatrix(tinygltf::Node& node, const std::vector<double>& matrix);

    /**
     * Check if a node is referenced by a skin as a joint.
     */
    static bool isJoint(const tinygltf::Model& model, int nodeIndex);

    /**
     * Check if a node is targeted by any animation.
     */
    static bool isAnimated(const tinygltf::Model& model, int nodeIndex);

    /**
     * Check if a node has a joint or animated parent.
     */
    static bool hasConstrainedParent(const tinygltf::Model& model, int nodeIndex, 
                                     const std::set<int>& joints, 
                                     const std::set<int>& animated);

    /**
     * Remove empty leaf nodes (nodes with no children and no attachments).
     */
    static void pruneEmptyNodes(tinygltf::Model& model);
};

} // namespace gltfu
