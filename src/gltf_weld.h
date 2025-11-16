#pragma once

#include "tiny_gltf.h"
#include <vector>
#include <cstdint>

namespace gltfu {

/**
 * Options for the weld operation
 */
struct WeldOptions {
    // Whether to overwrite existing indices
    bool overwrite = true;
    
    // Verbose output
    bool verbose = false;
    
    // Constructor with default values
    WeldOptions() = default;
};

/**
 * Class responsible for welding (merging) identical vertices
 * 
 * This class uses a hash table approach to identify bitwise-identical vertices
 * and merges them, creating indexed geometry that shares vertex data efficiently.
 * This reduces file size and improves GPU vertex cache utilization.
 */
class GltfWeld {
public:
    GltfWeld();
    ~GltfWeld();

    /**
     * Process the model to weld identical vertices
     * @param model The GLTF model to process
     * @param options Weld options
     * @return true on success
     */
    bool process(tinygltf::Model& model, const WeldOptions& options = WeldOptions());

    /**
     * Weld a single primitive
     * @param primitive The primitive to weld
     * @param mesh The mesh containing the primitive
     * @param model The model
     * @param options Weld options
     * @return true on success
     */
    bool weldPrimitive(tinygltf::Primitive& primitive,
                      tinygltf::Mesh& mesh,
                      tinygltf::Model& model,
                      const WeldOptions& options = WeldOptions());

private:
    /**
     * Vertex stream for hashing and comparing vertices
     */
    class VertexStream {
    public:
        VertexStream(const tinygltf::Primitive& prim, const tinygltf::Model& model);
        
        uint32_t hash(uint32_t index) const;
        bool equal(uint32_t a, uint32_t b) const;
        
    private:
        struct AttributeView {
            const uint8_t* data;
            size_t byteStride;
        };
        
        std::vector<AttributeView> attributes;
    };

    /**
     * MurmurHash2 implementation
     */
    static uint32_t murmurHash2(uint32_t h, const uint32_t* key, size_t len);

    /**
     * Hash table lookup with linear probing
     */
    static uint32_t hashLookup(const std::vector<uint32_t>& table,
                               uint32_t buckets,
                               const VertexStream& stream,
                               uint32_t key,
                               uint32_t empty);

    /**
     * Compute next power of two
     */
    static uint32_t ceilPowerOfTwo(uint32_t n);

    /**
     * Compact primitive using remap
     */
    bool compactPrimitive(tinygltf::Primitive& primitive,
                         tinygltf::Mesh& mesh,
                         tinygltf::Model& model,
                         const std::vector<uint32_t>& remap,
                         uint32_t dstVertexCount);

    /**
     * Get accessor data pointer
     */
    const uint8_t* getAccessorData(int accessorIdx, const tinygltf::Model& model) const;

    /**
     * Get element size (number of components)
     */
    size_t getElementSize(int type) const;

    /**
     * Get component size (bytes per component)
     */
    size_t getComponentSize(int componentType) const;
};

} // namespace gltfu
