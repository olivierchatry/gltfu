#pragma once

#include "tiny_gltf.h"
#include <string>
#include <vector>

namespace gltfu {

/**
 * Options for the join operation
 */
struct JoinOptions {
    // Keep meshes separate (join only primitives within same mesh)
    bool keepMeshes = false;
    
    // Keep named meshes and nodes separate
    bool keepNamed = false;
    
    // Verbose output
    bool verbose = false;
    
    // Constructor with default values
    JoinOptions() = default;
};

/**
 * Class responsible for joining compatible primitives to reduce draw calls
 * 
 * This class joins primitives that share the same material, mode, and vertex 
 * attribute structure. It can join primitives within the same mesh or across 
 * sibling nodes in the scene hierarchy.
 */
class GltfJoin {
public:
    GltfJoin();
    ~GltfJoin();

    /**
     * Process the model to join compatible primitives
     * @param model The GLTF model to process
     * @param options Join options
     * @return true on success
     */
    bool process(tinygltf::Model& model, const JoinOptions& options = JoinOptions());

private:
    /**
     * Create a key identifying compatible primitives that can be joined
     * @param prim The primitive
     * @param model The model
     * @return A string key representing primitive characteristics
     */
    std::string createPrimGroupKey(const tinygltf::Primitive& prim, 
                                    const tinygltf::Model& model) const;

    /**
     * Check if two primitives are compatible for joining
     * @param prim1 First primitive
     * @param prim2 Second primitive
     * @param model The model
     * @return true if primitives can be joined
     */
    bool arePrimitivesCompatible(const tinygltf::Primitive& prim1,
                                 const tinygltf::Primitive& prim2,
                                 const tinygltf::Model& model) const;

    /**
     * Join a list of compatible primitives into a single primitive
     * @param prims List of primitive indices within the mesh
     * @param mesh The mesh containing the primitives
     * @param model The model
     * @return Index of the new joined primitive (-1 on failure)
     */
    int joinPrimitives(const std::vector<int>& primIndices,
                       tinygltf::Mesh& mesh,
                       tinygltf::Model& model);

    /**
     * Remap vertex attributes when joining primitives
     * @param srcAccessorIdx Source accessor index
     * @param srcIndicesIdx Source indices accessor index (-1 if none)
     * @param remap Remapping array (srcIndex -> dstIndex)
     * @param dstAccessorIdx Destination accessor index
     * @param model The model
     */
    void remapAttribute(int srcAccessorIdx,
                       int srcIndicesIdx,
                       const std::vector<uint32_t>& remap,
                       int dstAccessorIdx,
                       tinygltf::Model& model);

    /**
     * Remap indices when joining primitives
     * @param srcIndicesIdx Source indices accessor index
     * @param remap Remapping array (srcIndex -> dstIndex)
     * @param dstIndicesIdx Destination indices accessor index
     * @param dstOffset Offset in destination indices
     * @param model The model
     */
    void remapIndices(int srcIndicesIdx,
                     const std::vector<uint32_t>& remap,
                     int dstIndicesIdx,
                     size_t dstOffset,
                     tinygltf::Model& model);

    /**
     * Get the byte size of a component type
     */
    size_t getComponentSize(int componentType) const;

    /**
     * Get the number of components for an accessor type
     */
    size_t getElementSize(int type) const;
};

} // namespace gltfu
