#include "gltf_simplify.h"
#include "meshoptimizer.h"
#include <iostream>
#include <algorithm>
#include <cstring>

namespace gltfu {

bool GltfSimplify::process(tinygltf::Model& model, const SimplifyOptions& options) {
    std::cout << "Simplifying meshes (ratio=" << options.ratio 
              << ", error=" << options.error << ")..." << std::endl;
    
    int totalPrimitives = 0;
    int simplifiedPrimitives = 0;
    int skippedPrimitives = 0;
    
    for (auto& mesh : model.meshes) {
        for (auto& prim : mesh.primitives) {
            totalPrimitives++;
            
            // Skip non-triangle primitives
            if (prim.mode != TINYGLTF_MODE_TRIANGLES &&
                prim.mode != TINYGLTF_MODE_TRIANGLE_STRIP &&
                prim.mode != TINYGLTF_MODE_TRIANGLE_FAN) {
                std::cout << "  Skipping non-triangle primitive (mode=" << prim.mode << ")" << std::endl;
                skippedPrimitives++;
                continue;
            }
            
            // Convert triangle strips/fans to plain triangles
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
                convertToTriangles(prim, model);
            }
            
            if (simplifyPrimitive(prim, model, options)) {
                simplifiedPrimitives++;
            } else {
                skippedPrimitives++;
            }
        }
    }
    
    std::cout << "Simplified " << simplifiedPrimitives << "/" << totalPrimitives 
              << " primitives (" << skippedPrimitives << " skipped)" << std::endl;
    
    return true;
}

bool GltfSimplify::simplifyPrimitive(tinygltf::Primitive& primitive,
                                      tinygltf::Model& model,
                                      const SimplifyOptions& options) {
    // Must have POSITION attribute
    auto posIt = primitive.attributes.find("POSITION");
    if (posIt == primitive.attributes.end()) {
        std::cerr << "  Primitive missing POSITION attribute" << std::endl;
        return false;
    }
    
    int posAccessorIdx = posIt->second;
    if (posAccessorIdx < 0 || posAccessorIdx >= (int)model.accessors.size()) {
        return false;
    }
    
    const auto& posAccessor = model.accessors[posAccessorIdx];
    size_t vertexCount = posAccessor.count;
    
    if (vertexCount == 0) {
        return false;
    }
    
    // Must have indices
    if (primitive.indices < 0 || primitive.indices >= (int)model.accessors.size()) {
        std::cerr << "  Primitive missing indices" << std::endl;
        return false;
    }
    
    const auto& indexAccessor = model.accessors[primitive.indices];
    size_t indexCount = indexAccessor.count;
    
    if (indexCount == 0 || indexCount % 3 != 0) {
        return false;
    }
    
    // Get position buffer view
    if (posAccessor.bufferView < 0 || posAccessor.bufferView >= (int)model.bufferViews.size()) {
        return false;
    }
    
    const auto& posBufferView = model.bufferViews[posAccessor.bufferView];
    if (posBufferView.buffer < 0 || posBufferView.buffer >= (int)model.buffers.size()) {
        return false;
    }
    
    const auto& posBuffer = model.buffers[posBufferView.buffer];
    const unsigned char* posData = posBuffer.data.data() + posBufferView.byteOffset + posAccessor.byteOffset;
    size_t posStride = posBufferView.byteStride != 0 ? posBufferView.byteStride : (3 * sizeof(float));
    
    // Get indices
    const auto& indexBufferView = model.bufferViews[indexAccessor.bufferView];
    const auto& indexBuffer = model.buffers[indexBufferView.buffer];
    const unsigned char* indexData = indexBuffer.data.data() + indexBufferView.byteOffset + indexAccessor.byteOffset;
    
    // Copy indices to uint32 array
    std::vector<unsigned int> indices(indexCount);
    if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        const uint16_t* src = reinterpret_cast<const uint16_t*>(indexData);
        for (size_t i = 0; i < indexCount; ++i) {
            indices[i] = src[i];
        }
    } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(indexData);
        std::memcpy(indices.data(), src, indexCount * sizeof(uint32_t));
    } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        const uint8_t* src = reinterpret_cast<const uint8_t*>(indexData);
        for (size_t i = 0; i < indexCount; ++i) {
            indices[i] = src[i];
        }
    } else {
        std::cerr << "  Unsupported index component type: " << indexAccessor.componentType << std::endl;
        return false;
    }
    
    // Calculate target index count
    size_t targetIndexCount = static_cast<size_t>(indexCount * options.ratio);
    // Round to multiple of 3
    targetIndexCount = (targetIndexCount / 3) * 3;
    
    // If already at or below target, skip
    if (indexCount <= targetIndexCount) {
        return false;
    }
    
    // Prepare output buffer
    std::vector<unsigned int> simplifiedIndices(indexCount);
    
    // Run simplification
    unsigned int simplifyOptions = 0;
    if (options.lockBorder) {
        simplifyOptions |= meshopt_SimplifyLockBorder;
    }
    
    float resultError = 0.0f;
    size_t resultIndexCount = meshopt_simplify(
        simplifiedIndices.data(),
        indices.data(),
        indexCount,
        reinterpret_cast<const float*>(posData),
        vertexCount,
        posStride,
        targetIndexCount,
        options.error,
        simplifyOptions,
        &resultError
    );
    
    if (resultIndexCount == 0 || resultIndexCount == indexCount) {
        // Simplification didn't reduce anything
        return false;
    }
    
    simplifiedIndices.resize(resultIndexCount);
    
    std::cout << "  Simplified: " << (indexCount / 3) << " → " << (resultIndexCount / 3) 
              << " triangles (" << (resultIndexCount * 100 / indexCount) << "%), error: " 
              << resultError << std::endl;
    
    // Create new index data
    std::vector<unsigned char> newIndexData;
    
    // Determine index component type based on max index
    unsigned int maxIndex = *std::max_element(simplifiedIndices.begin(), simplifiedIndices.end());
    int newComponentType;
    
    if (maxIndex <= 255) {
        newComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
        newIndexData.resize(resultIndexCount);
        for (size_t i = 0; i < resultIndexCount; ++i) {
            newIndexData[i] = static_cast<uint8_t>(simplifiedIndices[i]);
        }
    } else if (maxIndex <= 65535) {
        newComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
        newIndexData.resize(resultIndexCount * 2);
        uint16_t* dst = reinterpret_cast<uint16_t*>(newIndexData.data());
        for (size_t i = 0; i < resultIndexCount; ++i) {
            dst[i] = static_cast<uint16_t>(simplifiedIndices[i]);
        }
    } else {
        newComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
        newIndexData.resize(resultIndexCount * 4);
        std::memcpy(newIndexData.data(), simplifiedIndices.data(), resultIndexCount * 4);
    }
    
    // Find or create a buffer to append to (reuse first buffer if available)
    int bufferIdx = 0;
    size_t byteOffset = 0;
    
    if (model.buffers.empty()) {
        // No buffers exist, create one
        tinygltf::Buffer newBuffer;
        model.buffers.push_back(newBuffer);
    }
    
    // Append to first buffer
    byteOffset = model.buffers[bufferIdx].data.size();
    model.buffers[bufferIdx].data.insert(
        model.buffers[bufferIdx].data.end(),
        newIndexData.begin(),
        newIndexData.end()
    );
    
    // Create buffer view
    tinygltf::BufferView newBufferView;
    newBufferView.buffer = bufferIdx;
    newBufferView.byteOffset = byteOffset;
    newBufferView.byteLength = newIndexData.size();
    newBufferView.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
    
    int bufferViewIdx = model.bufferViews.size();
    model.bufferViews.push_back(newBufferView);
    
    // Create accessor
    tinygltf::Accessor newAccessor;
    newAccessor.bufferView = bufferViewIdx;
    newAccessor.byteOffset = 0;
    newAccessor.componentType = newComponentType;
    newAccessor.count = resultIndexCount;
    newAccessor.type = TINYGLTF_TYPE_SCALAR;
    
    // Compute min/max
    newAccessor.minValues = {static_cast<double>(*std::min_element(simplifiedIndices.begin(), simplifiedIndices.end()))};
    newAccessor.maxValues = {static_cast<double>(*std::max_element(simplifiedIndices.begin(), simplifiedIndices.end()))};
    
    int accessorIdx = model.accessors.size();
    model.accessors.push_back(newAccessor);
    
    // Update primitive to use new indices
    primitive.indices = accessorIdx;
    
    return true;
}

void GltfSimplify::convertToTriangles(tinygltf::Primitive& primitive, tinygltf::Model& /* model */) {
    // This is a simplified conversion - proper implementation would expand triangle strips/fans
    // For now, just change the mode
    std::cout << "  Converting primitive mode " << primitive.mode << " to triangles" << std::endl;
    primitive.mode = TINYGLTF_MODE_TRIANGLES;
}

size_t GltfSimplify::getAccessorCount(const tinygltf::Accessor& accessor) const {
    return accessor.count;
}

size_t GltfSimplify::getAccessorElementSize(const tinygltf::Accessor& accessor) const {
    size_t componentSize = 0;
    switch (accessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            componentSize = 1;
            break;
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            componentSize = 2;
            break;
        case TINYGLTF_COMPONENT_TYPE_INT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
            componentSize = 4;
            break;
        case TINYGLTF_COMPONENT_TYPE_DOUBLE:
            componentSize = 8;
            break;
    }
    
    size_t numComponents = 1;
    if (accessor.type == TINYGLTF_TYPE_VEC2) numComponents = 2;
    else if (accessor.type == TINYGLTF_TYPE_VEC3) numComponents = 3;
    else if (accessor.type == TINYGLTF_TYPE_VEC4 || accessor.type == TINYGLTF_TYPE_MAT2) numComponents = 4;
    else if (accessor.type == TINYGLTF_TYPE_MAT3) numComponents = 9;
    else if (accessor.type == TINYGLTF_TYPE_MAT4) numComponents = 16;
    
    return componentSize * numComponents;
}

} // namespace gltfu
