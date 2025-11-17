#pragma once

#include "tiny_gltf.h"
#include <string>

namespace gltfu {

/**
 * Options for the simplify operation.
 */
struct SimplifyOptions {
    float ratio = 0.0f;          // Target ratio (0-1) of vertices to keep (0 = maximum simplification)
    float error = 0.0001f;       // Error threshold as fraction of mesh radius (default 0.01%)
    bool lockBorder = false;     // Lock topological borders of the mesh
    bool verbose = false;        // Emit simplification summary
};

/**
 * Simplify reduces mesh complexity using quadric error metrics.
 * Uses meshoptimizer library for high-quality mesh simplification.
 * 
 * Note: Simplification is lossy but aims to preserve visual quality.
 * The algorithm tries to reach the target ratio while keeping error below threshold.
 */
class GltfSimplify {
public:
    GltfSimplify() = default;
    
    /**
     * Process the model and simplify all meshes.
     * @param model The GLTF model to simplify
     * @param options Simplification options
     * @return true if successful
     */
    bool process(tinygltf::Model& model, const SimplifyOptions& options = SimplifyOptions());
    std::string getStats() const { return stats_; }
    std::string getError() const { return error_; }
    
private:
    std::string stats_;
    std::string error_;

    struct PrimitiveSummary {
        size_t originalTriangles = 0;
        size_t simplifiedTriangles = 0;
        float error = 0.0f;
        std::string reason;
    };

    // Simplify a single primitive
    bool simplifyPrimitive(tinygltf::Primitive& primitive,
                          tinygltf::Model& model,
                          const SimplifyOptions& options,
                          PrimitiveSummary& summary);
    
    // Get accessor element count
    size_t getAccessorCount(const tinygltf::Accessor& accessor) const;
    
    // Get accessor element size in bytes
    size_t getAccessorElementSize(const tinygltf::Accessor& accessor) const;
    
    // Convert primitive to triangles if needed
    void convertToTriangles(tinygltf::Primitive& primitive, tinygltf::Model& model);
};

} // namespace gltfu
