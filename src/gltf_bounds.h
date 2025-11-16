#pragma once
#include "tiny_gltf.h"

namespace gltfu {

/**
 * @brief Utility to compute and set min/max bounds for accessors
 */
class GltfBounds {
public:
    /**
     * @brief Compute and set min/max bounds for all POSITION accessors in a model
     * @param model The GLTF model to process
     * @return Number of accessors updated
     */
    static int computeAllBounds(tinygltf::Model& model);
    
    /**
     * @brief Compute and set min/max bounds for a specific accessor
     * @param model The GLTF model
     * @param accessorIdx The accessor index
     * @return true if successful, false otherwise
     */
    static bool computeAccessorBounds(tinygltf::Model& model, int accessorIdx);
};

} // namespace gltfu
