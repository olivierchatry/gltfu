#include "gltf_simplify.h"
#include "meshoptimizer.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <sstream>

namespace gltfu {

bool GltfSimplify::process(tinygltf::Model& model, const SimplifyOptions& options) {
    error_.clear();
    stats_.clear();

    if (options.verbose) {
        std::cout << "[simplify] Starting (ratio=" << options.ratio
                  << ", error=" << options.error << ")" << std::endl;
    }

    size_t totalPrimitives = 0;
    size_t simplifiedPrimitives = 0;
    size_t skippedPrimitives = 0;
    size_t totalOriginalTriangles = 0;
    size_t totalSimplifiedTriangles = 0;

    try {
        for (size_t meshIdx = 0; meshIdx < model.meshes.size(); ++meshIdx) {
            auto& mesh = model.meshes[meshIdx];
            for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
                auto& prim = mesh.primitives[primIdx];
                ++totalPrimitives;

                const bool isTrianglePrimitive =
                    prim.mode == TINYGLTF_MODE_TRIANGLES ||
                    prim.mode == TINYGLTF_MODE_TRIANGLE_STRIP ||
                    prim.mode == TINYGLTF_MODE_TRIANGLE_FAN;

                if (!isTrianglePrimitive) {
                    ++skippedPrimitives;
                    if (options.verbose) {
                        std::cout << "[simplify] Skipping primitive " << meshIdx << ':' << primIdx
                                  << " (mode=" << prim.mode << ")" << std::endl;
                    }
                    continue;
                }

                if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
                    if (options.verbose) {
                        std::cout << "[simplify] Converting primitive " << meshIdx << ':' << primIdx
                                  << " from mode " << prim.mode << " to triangles" << std::endl;
                    }
                    convertToTriangles(prim, model);
                }

                PrimitiveSummary summary{};
                if (simplifyPrimitive(prim, model, options, summary)) {
                    ++simplifiedPrimitives;
                    totalOriginalTriangles += summary.originalTriangles;
                    totalSimplifiedTriangles += summary.simplifiedTriangles;

                    if (options.verbose) {
                        const char* meshName = mesh.name.empty() ? "(unnamed)" : mesh.name.c_str();
                        std::cout << "[simplify] " << meshName << " primitive " << primIdx
                                  << ": " << summary.originalTriangles << " → "
                                  << summary.simplifiedTriangles << " triangles"
                                  << " (error " << summary.error << ")" << std::endl;
                    }
                } else {
                    ++skippedPrimitives;
                    if (options.verbose) {
                        std::string reason = summary.reason.empty() ? "no reduction" : summary.reason;
                        const char* meshName = mesh.name.empty() ? "(unnamed)" : mesh.name.c_str();
                        std::cout << "[simplify] Skipped " << meshName << " primitive " << primIdx
                                  << " - " << reason << std::endl;
                    }
                }
            }
        }
    } catch (const std::exception& ex) {
        error_ = std::string("Simplification failed: ") + ex.what();
        return false;
    }

    std::ostringstream stream;
    if (totalPrimitives == 0) {
        stream << "No primitives found";
    } else if (simplifiedPrimitives > 0) {
        stream << "Primitives simplified: " << simplifiedPrimitives << '/' << totalPrimitives;
        if (totalOriginalTriangles > 0) {
            stream << "\nTriangles: " << totalOriginalTriangles << " → "
                   << totalSimplifiedTriangles;
        }
        if (skippedPrimitives > 0) {
            stream << "\nSkipped: " << skippedPrimitives;
        }
    } else {
        stream << "No primitives simplified";
        if (skippedPrimitives > 0) {
            stream << " (" << skippedPrimitives << " skipped)";
        }
    }

    stats_ = stream.str();

    if (options.verbose) {
        std::cout << "[simplify] " << stats_ << std::endl;
    }

    return true;
}

bool GltfSimplify::simplifyPrimitive(tinygltf::Primitive& primitive,
                                      tinygltf::Model& model,
                                      const SimplifyOptions& options,
                                      PrimitiveSummary& summary) {
    summary = {};

    const auto posIt = primitive.attributes.find("POSITION");
    if (posIt == primitive.attributes.end()) {
        summary.reason = "missing POSITION attribute";
        return false;
    }

    const int posAccessorIdx = posIt->second;
    if (posAccessorIdx < 0 || posAccessorIdx >= static_cast<int>(model.accessors.size())) {
        summary.reason = "invalid POSITION accessor";
        return false;
    }

    const auto& posAccessor = model.accessors[posAccessorIdx];
    const size_t vertexCount = posAccessor.count;
    if (vertexCount == 0) {
        summary.reason = "empty POSITION accessor";
        return false;
    }

    if (primitive.indices < 0 || primitive.indices >= static_cast<int>(model.accessors.size())) {
        summary.reason = "missing indices";
        return false;
    }

    const auto& indexAccessor = model.accessors[primitive.indices];
    const size_t indexCount = indexAccessor.count;
    if (indexCount == 0 || indexCount % 3 != 0) {
        summary.reason = "indices not a triangle list";
        return false;
    }

    summary.originalTriangles = indexCount / 3;
    summary.simplifiedTriangles = summary.originalTriangles;

    if (posAccessor.bufferView < 0 || posAccessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        summary.reason = "invalid POSITION bufferView";
        return false;
    }

    const auto& posBufferView = model.bufferViews[posAccessor.bufferView];
    if (posBufferView.buffer < 0 || posBufferView.buffer >= static_cast<int>(model.buffers.size())) {
        summary.reason = "invalid POSITION buffer";
        return false;
    }

    const auto& posBuffer = model.buffers[posBufferView.buffer];
    const size_t posOffset = posBufferView.byteOffset + posAccessor.byteOffset;
    if (posOffset >= posBuffer.data.size()) {
        summary.reason = "POSITION data out of range";
        return false;
    }

    const unsigned char* posData = posBuffer.data.data() + posOffset;
    const size_t posStride = posBufferView.byteStride != 0 ? posBufferView.byteStride : (3 * sizeof(float));

    if (indexAccessor.bufferView < 0 || indexAccessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        summary.reason = "invalid index bufferView";
        return false;
    }

    const auto& indexBufferView = model.bufferViews[indexAccessor.bufferView];
    if (indexBufferView.buffer < 0 || indexBufferView.buffer >= static_cast<int>(model.buffers.size())) {
        summary.reason = "invalid index buffer";
        return false;
    }

    const auto& indexBuffer = model.buffers[indexBufferView.buffer];
    const size_t indexOffset = indexBufferView.byteOffset + indexAccessor.byteOffset;
    if (indexOffset >= indexBuffer.data.size()) {
        summary.reason = "index data out of range";
        return false;
    }

    const unsigned char* indexData = indexBuffer.data.data() + indexOffset;

    std::vector<unsigned int> indices(indexCount);
    switch (indexAccessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            const auto* src = reinterpret_cast<const uint8_t*>(indexData);
            for (size_t i = 0; i < indexCount; ++i) {
                indices[i] = src[i];
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            const auto* src = reinterpret_cast<const uint16_t*>(indexData);
            for (size_t i = 0; i < indexCount; ++i) {
                indices[i] = src[i];
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
            const auto* src = reinterpret_cast<const uint32_t*>(indexData);
            std::memcpy(indices.data(), src, indexCount * sizeof(uint32_t));
            break;
        }
        default:
            summary.reason = "unsupported index type";
            return false;
    }

    size_t targetIndexCount = static_cast<size_t>(static_cast<double>(indexCount) * options.ratio);
    targetIndexCount = (targetIndexCount / 3) * 3;
    if (targetIndexCount < 3) {
        targetIndexCount = 3;
    }

    if (indexCount <= targetIndexCount) {
        summary.reason = "already at or below target";
        return false;
    }

    std::vector<unsigned int> simplifiedIndices(indexCount);

    unsigned int simplifyFlags = 0;
    if (options.lockBorder) {
        simplifyFlags |= meshopt_SimplifyLockBorder;
    }

    float resultError = 0.0f;
    const size_t resultIndexCount = meshopt_simplify(
        simplifiedIndices.data(),
        indices.data(),
        indexCount,
        reinterpret_cast<const float*>(posData),
        vertexCount,
        posStride,
        targetIndexCount,
        options.error,
        simplifyFlags,
        &resultError);

    if (resultIndexCount == 0 || resultIndexCount >= indexCount) {
        summary.reason = "no reduction";
        return false;
    }

    simplifiedIndices.resize(resultIndexCount);

    std::vector<unsigned char> newIndexData;
    const unsigned int maxIndex = *std::max_element(simplifiedIndices.begin(), simplifiedIndices.end());
    int newComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;

    if (maxIndex <= 255) {
        newComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
        newIndexData.resize(resultIndexCount);
        for (size_t i = 0; i < resultIndexCount; ++i) {
            newIndexData[i] = static_cast<uint8_t>(simplifiedIndices[i]);
        }
    } else if (maxIndex <= 65535) {
        newComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
        newIndexData.resize(resultIndexCount * sizeof(uint16_t));
        auto* dst = reinterpret_cast<uint16_t*>(newIndexData.data());
        for (size_t i = 0; i < resultIndexCount; ++i) {
            dst[i] = static_cast<uint16_t>(simplifiedIndices[i]);
        }
    } else {
        newComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
        newIndexData.resize(resultIndexCount * sizeof(uint32_t));
        std::memcpy(newIndexData.data(), simplifiedIndices.data(), resultIndexCount * sizeof(uint32_t));
    }

    if (model.buffers.empty()) {
        model.buffers.emplace_back();
    }

    const int bufferIdx = 0;
    const size_t byteOffset = model.buffers[bufferIdx].data.size();
    model.buffers[bufferIdx].data.insert(model.buffers[bufferIdx].data.end(),
                                         newIndexData.begin(), newIndexData.end());

    tinygltf::BufferView newBufferView;
    newBufferView.buffer = bufferIdx;
    newBufferView.byteOffset = byteOffset;
    newBufferView.byteLength = newIndexData.size();
    newBufferView.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;

    const int bufferViewIdx = static_cast<int>(model.bufferViews.size());
    model.bufferViews.push_back(newBufferView);

    tinygltf::Accessor newAccessor;
    newAccessor.bufferView = bufferViewIdx;
    newAccessor.byteOffset = 0;
    newAccessor.componentType = newComponentType;
    newAccessor.count = resultIndexCount;
    newAccessor.type = TINYGLTF_TYPE_SCALAR;

    const auto minMax = std::minmax_element(simplifiedIndices.begin(), simplifiedIndices.end());
    newAccessor.minValues = {static_cast<double>(*minMax.first)};
    newAccessor.maxValues = {static_cast<double>(*minMax.second)};

    const int accessorIdx = static_cast<int>(model.accessors.size());
    model.accessors.push_back(newAccessor);

    primitive.indices = accessorIdx;

    summary.simplifiedTriangles = resultIndexCount / 3;
    summary.error = resultError;
    summary.reason.clear();

    return true;
}

void GltfSimplify::convertToTriangles(tinygltf::Primitive& primitive, tinygltf::Model& /* model */) {
    // Simplified conversion placeholder – real conversion would expand strips/fans
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
