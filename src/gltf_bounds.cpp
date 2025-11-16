#include "gltf_bounds.h"
#include <limits>
#include <algorithm>

namespace gltfu {

int GltfBounds::computeAllBounds(tinygltf::Model& model) {
    int updated = 0;
    
    // Find all POSITION accessors
    for (const auto& mesh : model.meshes) {
        for (const auto& primitive : mesh.primitives) {
            auto posIt = primitive.attributes.find("POSITION");
            if (posIt != primitive.attributes.end()) {
                int accessorIdx = posIt->second;
                if (computeAccessorBounds(model, accessorIdx)) {
                    updated++;
                }
            }
        }
    }
    
    return updated;
}

bool GltfBounds::computeAccessorBounds(tinygltf::Model& model, int accessorIdx) {
    if (accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors.size())) {
        return false;
    }
    
    auto& accessor = model.accessors[accessorIdx];
    
    // Only compute for VEC3 float accessors (typical for POSITION)
    if (accessor.type != TINYGLTF_TYPE_VEC3 || 
        accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        return false;
    }
    
    // Get buffer data
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        return false;
    }
    
    const auto& bufferView = model.bufferViews[accessor.bufferView];
    if (bufferView.buffer < 0 || bufferView.buffer >= static_cast<int>(model.buffers.size())) {
        return false;
    }
    
    const auto& buffer = model.buffers[bufferView.buffer];
    const uint8_t* data = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    
    // Compute stride
    size_t stride = accessor.ByteStride(bufferView);
    if (stride == 0) {
        stride = 3 * sizeof(float); // Default for VEC3 float
    }
    
    // Initialize min/max
    std::vector<double> minValues(3, std::numeric_limits<double>::max());
    std::vector<double> maxValues(3, std::numeric_limits<double>::lowest());
    
    // Iterate through all elements
    for (size_t i = 0; i < accessor.count; ++i) {
        const float* vertex = reinterpret_cast<const float*>(data + i * stride);
        
        for (int c = 0; c < 3; ++c) {
            double value = static_cast<double>(vertex[c]);
            minValues[c] = std::min(minValues[c], value);
            maxValues[c] = std::max(maxValues[c], value);
        }
    }
    
    // Set min/max on accessor
    accessor.minValues = minValues;
    accessor.maxValues = maxValues;
    
    return true;
}

} // namespace gltfu
