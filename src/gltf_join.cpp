#include "gltf_join.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace gltfu {
namespace {

struct ConstAccessorSpan {
    const uint8_t* data = nullptr;
    size_t stride = 0;
    size_t elementSize = 0;
    size_t count = 0;
};

struct AccessorSpan {
    uint8_t* data = nullptr;
    size_t stride = 0;
    size_t elementSize = 0;
    size_t count = 0;
};

size_t componentSize(int componentType) {
    switch (componentType) {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return 1;
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return 2;
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
        case TINYGLTF_COMPONENT_TYPE_INT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            return 4;
        default:
            return 4;
    }
}

size_t componentCount(int type) {
    switch (type) {
        case TINYGLTF_TYPE_SCALAR: return 1;
        case TINYGLTF_TYPE_VEC2: return 2;
        case TINYGLTF_TYPE_VEC3: return 3;
        case TINYGLTF_TYPE_VEC4: return 4;
        case TINYGLTF_TYPE_MAT2: return 4;
        case TINYGLTF_TYPE_MAT3: return 9;
        case TINYGLTF_TYPE_MAT4: return 16;
        default: return 1;
    }
}

bool resolveConstSpan(const tinygltf::Model& model, int accessorIdx, ConstAccessorSpan& span) {
    if (accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors.size())) {
        return false;
    }

    const auto& accessor = model.accessors[accessorIdx];
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        return false;
    }

    const auto& view = model.bufferViews[accessor.bufferView];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size())) {
        return false;
    }

    const auto& buffer = model.buffers[view.buffer];
    const size_t elemSize = componentCount(accessor.type) * componentSize(accessor.componentType);
    if (elemSize == 0) {
        return false;
    }

    const size_t stride = view.byteStride > 0 ? static_cast<size_t>(view.byteStride) : elemSize;
    const size_t offset = view.byteOffset + accessor.byteOffset;
    const size_t required = accessor.count == 0 ? 0 : offset + stride * (accessor.count - 1) + elemSize;
    if (required > buffer.data.size()) {
        return false;
    }

    span.data = buffer.data.data() + offset;
    span.stride = stride;
    span.elementSize = elemSize;
    span.count = accessor.count;
    return true;
}

bool resolveSpan(tinygltf::Model& model, int accessorIdx, AccessorSpan& span) {
    if (accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors.size())) {
        return false;
    }

    const auto& accessor = model.accessors[accessorIdx];
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        return false;
    }

    auto& view = model.bufferViews[accessor.bufferView];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size())) {
        return false;
    }

    auto& buffer = model.buffers[view.buffer];
    const size_t elemSize = componentCount(accessor.type) * componentSize(accessor.componentType);
    if (elemSize == 0) {
        return false;
    }

    const size_t stride = view.byteStride > 0 ? static_cast<size_t>(view.byteStride) : elemSize;
    const size_t offset = view.byteOffset + accessor.byteOffset;
    const size_t required = accessor.count == 0 ? 0 : offset + stride * (accessor.count - 1) + elemSize;
    if (required > buffer.data.size()) {
        return false;
    }

    span.data = buffer.data.data() + offset;
    span.stride = stride;
    span.elementSize = elemSize;
    span.count = accessor.count;
    return true;
}

bool readIndices(const tinygltf::Model& model,
                 int accessorIdx,
                 std::vector<uint32_t>& indices) {
    ConstAccessorSpan span;
    if (!resolveConstSpan(model, accessorIdx, span)) {
        return false;
    }

    indices.resize(span.count);
    const uint8_t* src = span.data;

    for (size_t i = 0; i < span.count; ++i) {
        switch (model.accessors[accessorIdx].componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                indices[i] = src[i * span.stride];
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                const auto* value = reinterpret_cast<const uint16_t*>(src + i * span.stride);
                indices[i] = *value;
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                const auto* value = reinterpret_cast<const uint32_t*>(src + i * span.stride);
                indices[i] = *value;
                break;
            }
            default:
                return false;
        }
    }

    return true;
}

int chooseIndexComponentType(size_t vertexCount) {
    if (vertexCount == 0) {
        return TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
    }

    if (vertexCount - 1 <= std::numeric_limits<uint8_t>::max()) {
        return TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    }
    if (vertexCount - 1 <= std::numeric_limits<uint16_t>::max()) {
        return TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
    }
    return TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
}

int allocateAccessor(tinygltf::Model& model,
                     size_t count,
                     int type,
                     int componentType,
                     int target) {
    const size_t elemSize = componentCount(type) * componentSize(componentType);
    tinygltf::Buffer buffer;
    buffer.data.resize(count * elemSize);
    const int bufferIdx = static_cast<int>(model.buffers.size());
    model.buffers.push_back(std::move(buffer));

    tinygltf::BufferView view;
    view.buffer = bufferIdx;
    view.byteOffset = 0;
    view.byteLength = count * elemSize;
    if (target != 0) {
        view.target = target;
    }
    const int viewIdx = static_cast<int>(model.bufferViews.size());
    model.bufferViews.push_back(std::move(view));

    tinygltf::Accessor accessor;
    accessor.bufferView = viewIdx;
    accessor.byteOffset = 0;
    accessor.componentType = componentType;
    accessor.count = count;
    accessor.type = type;
    model.accessors.push_back(std::move(accessor));

    return static_cast<int>(model.accessors.size() - 1);
}

std::string primitiveKey(const tinygltf::Primitive& primitive,
                         const tinygltf::Model& model) {
    std::ostringstream stream;
    stream << "mat:" << primitive.material << '|';
    stream << "mode:" << primitive.mode << '|';
    stream << "idx:" << (primitive.indices >= 0 ? 1 : 0) << '|';

    std::vector<std::string> semantics;
    semantics.reserve(primitive.attributes.size());
    for (const auto& attribute : primitive.attributes) {
        semantics.push_back(attribute.first);
    }
    std::sort(semantics.begin(), semantics.end());

    stream << "attrs:";
    for (const auto& semantic : semantics) {
        const int accessorIdx = primitive.attributes.at(semantic);
        if (accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors.size())) {
            continue;
        }
        const auto& accessor = model.accessors[accessorIdx];
        stream << semantic << ':'
               << componentCount(accessor.type) << ':'
               << accessor.componentType << '+';
    }

    stream << "targets:" << primitive.targets.size();
    return stream.str();
}

void writeIndexValue(AccessorSpan& span,
                     size_t index,
                     uint32_t value,
                     int componentType) {
    uint8_t* dst = span.data + index * span.stride;
    switch (componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            dst[0] = static_cast<uint8_t>(value);
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            reinterpret_cast<uint16_t*>(dst)[0] = static_cast<uint16_t>(value);
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            reinterpret_cast<uint32_t*>(dst)[0] = value;
            break;
        default:
            break;
    }
}

struct PrimitiveInfo {
    const tinygltf::Primitive* primitive = nullptr;
    size_t vertexBase = 0;
    size_t vertexCount = 0;
    size_t indexBase = 0;
    size_t indexCount = 0;
};

struct JoinSummary {
    size_t removedPrimitives = 0;
    size_t mergedVertices = 0;
    size_t mergedIndices = 0;
};

bool joinPrimitiveGroup(const std::vector<int>& group,
                        tinygltf::Mesh& mesh,
                        tinygltf::Model& model,
                        JoinSummary& summary,
                        std::string& error) {
    if (group.size() < 2) {
        return false;
    }

    const auto& templatePrim = mesh.primitives[group.front()];
    if (!templatePrim.targets.empty()) {
        return false;
    }

    std::vector<PrimitiveInfo> infos;
    infos.reserve(group.size());

    size_t totalVertices = 0;
    size_t totalIndices = 0;

    // Validate and collect counts
    for (int primIndex : group) {
        if (primIndex < 0 || primIndex >= static_cast<int>(mesh.primitives.size())) {
            error = "Invalid primitive index";
            return false;
        }

        const auto& prim = mesh.primitives[primIndex];
        if (prim.targets.size() != templatePrim.targets.size()) {
            error = "Primitive targets mismatch";
            return false;
        }

        auto posIt = prim.attributes.find("POSITION");
        if (posIt == prim.attributes.end()) {
            error = "Primitive missing POSITION attribute";
            return false;
        }

        ConstAccessorSpan positionSpan;
        if (!resolveConstSpan(model, posIt->second, positionSpan)) {
            error = "Invalid POSITION accessor";
            return false;
        }

        PrimitiveInfo info;
        info.primitive = &prim;
        info.vertexBase = totalVertices;
        info.vertexCount = positionSpan.count;
        totalVertices += info.vertexCount;

        const bool templateHasIndices = templatePrim.indices >= 0;
        if (templateHasIndices) {
            if (prim.indices < 0) {
                error = "Primitive missing indices";
                return false;
            }
            ConstAccessorSpan indexSpan;
            if (!resolveConstSpan(model, prim.indices, indexSpan)) {
                error = "Invalid index accessor";
                return false;
            }
            info.indexBase = totalIndices;
            info.indexCount = indexSpan.count;
            totalIndices += info.indexCount;
        } else {
            info.indexBase = totalIndices;
            info.indexCount = info.vertexCount;
            totalIndices += info.indexCount;
        }

        infos.push_back(info);
    }

    if (totalVertices == 0) {
        return false;
    }

    // Validate attribute compatibility
    for (const auto& attribute : templatePrim.attributes) {
        const int templateAccessorIdx = attribute.second;
        if (templateAccessorIdx < 0 || templateAccessorIdx >= static_cast<int>(model.accessors.size())) {
            error = "Invalid template attribute accessor";
            return false;
        }
        const auto& templateAccessor = model.accessors[templateAccessorIdx];

        for (const auto& info : infos) {
            auto srcIt = info.primitive->attributes.find(attribute.first);
            if (srcIt == info.primitive->attributes.end()) {
                error = "Attribute mismatch across primitives";
                return false;
            }
            const int srcAccessorIdx = srcIt->second;
            if (srcAccessorIdx < 0 || srcAccessorIdx >= static_cast<int>(model.accessors.size())) {
                error = "Invalid attribute accessor";
                return false;
            }
            const auto& srcAccessor = model.accessors[srcAccessorIdx];
            if (srcAccessor.type != templateAccessor.type ||
                srcAccessor.componentType != templateAccessor.componentType) {
                error = "Attribute type mismatch";
                return false;
            }
            ConstAccessorSpan span;
            if (!resolveConstSpan(model, srcAccessorIdx, span)) {
                error = "Failed to access attribute data";
                return false;
            }
        }
    }

    const size_t accessorStart = model.accessors.size();
    const size_t viewStart = model.bufferViews.size();
    const size_t bufferStart = model.buffers.size();

    auto rollback = [&]() {
        model.accessors.resize(accessorStart);
        model.bufferViews.resize(viewStart);
        model.buffers.resize(bufferStart);
    };

    tinygltf::Primitive joined;
    joined.mode = templatePrim.mode;
    joined.material = templatePrim.material;

    // Allocate destination attributes
    struct AttributeTarget {
        std::string semantic;
        int accessorIdx = -1;
        AccessorSpan span;
    };

    std::vector<AttributeTarget> targets;
    targets.reserve(templatePrim.attributes.size());

    for (const auto& attribute : templatePrim.attributes) {
        const int templateAccessorIdx = attribute.second;
        const auto& templateAccessor = model.accessors[templateAccessorIdx];
        const int accessorIdx = allocateAccessor(model,
                                                 totalVertices,
                                                 templateAccessor.type,
                                                 templateAccessor.componentType,
                                                 TINYGLTF_TARGET_ARRAY_BUFFER);

        AccessorSpan span;
        if (!resolveSpan(model, accessorIdx, span)) {
            rollback();
            error = "Failed to allocate attribute buffer";
            return false;
        }

        joined.attributes[attribute.first] = accessorIdx;
        targets.push_back({attribute.first, accessorIdx, span});
    }

    const bool templateHasIndices = templatePrim.indices >= 0;
    int joinedIndicesAccessor = -1;
    AccessorSpan joinedIndexSpan;
    int joinedIndexComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;

    if (templateHasIndices) {
        joinedIndexComponentType = chooseIndexComponentType(totalVertices);
        joinedIndicesAccessor = allocateAccessor(model,
                                                 totalIndices,
                                                 TINYGLTF_TYPE_SCALAR,
                                                 joinedIndexComponentType,
                                                 TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);
        if (!resolveSpan(model, joinedIndicesAccessor, joinedIndexSpan)) {
            rollback();
            error = "Failed to allocate index buffer";
            return false;
        }
        joined.indices = joinedIndicesAccessor;
    } else {
        joined.indices = -1;
    }

    // Copy vertex attributes
    for (const auto& info : infos) {
        for (auto& target : targets) {
            auto srcIt = info.primitive->attributes.find(target.semantic);
            const int srcAccessorIdx = srcIt->second;
            ConstAccessorSpan srcSpan;
            if (!resolveConstSpan(model, srcAccessorIdx, srcSpan)) {
                rollback();
                error = "Failed to read attribute data";
                return false;
            }

            for (size_t i = 0; i < info.vertexCount; ++i) {
                const uint8_t* src = srcSpan.data + i * srcSpan.stride;
                uint8_t* dst = target.span.data + (info.vertexBase + i) * target.span.stride;
                std::memcpy(dst, src, srcSpan.elementSize);
            }
        }
    }

    // Copy indices (or generate sequential if original primitives were non-indexed)
    if (templateHasIndices) {
        for (const auto& info : infos) {
            std::vector<uint32_t> indices;
            if (!readIndices(model, info.primitive->indices, indices)) {
                rollback();
                error = "Failed to read index data";
                return false;
            }

            for (size_t i = 0; i < indices.size(); ++i) {
                const uint32_t value = indices[i] + static_cast<uint32_t>(info.vertexBase);
                writeIndexValue(joinedIndexSpan, info.indexBase + i, value, joinedIndexComponentType);
            }
        }
    }

    mesh.primitives.push_back(joined);

    summary.removedPrimitives = group.size();
    summary.mergedVertices = totalVertices;
    summary.mergedIndices = templateHasIndices ? totalIndices : 0;
    return true;
}

} // namespace

GltfJoin::GltfJoin() = default;
GltfJoin::~GltfJoin() = default;

bool GltfJoin::process(tinygltf::Model& model, const JoinOptions& options) {
    error_.clear();
    stats_.clear();

    size_t meshesModified = 0;
    size_t groupsMerged = 0;
    size_t primitivesRemoved = 0;

    for (auto& mesh : model.meshes) {
        if (mesh.primitives.size() < 2) {
            continue;
        }

        std::unordered_map<std::string, std::vector<int>> buckets;
        buckets.reserve(mesh.primitives.size());

        for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
            const auto& primitive = mesh.primitives[primIdx];
            if (!primitive.targets.empty()) {
                if (options.verbose) {
                    std::cout << "[join] Skipping primitive with morph targets in mesh '"
                              << mesh.name << "'" << std::endl;
                }
                continue;
            }

            std::string key = primitiveKey(primitive, model);
            if (options.keepNamed && !mesh.name.empty()) {
                key.append("|mesh:").append(mesh.name);
            }

            buckets[key].push_back(static_cast<int>(primIdx));
        }

        std::vector<int> removal;
        removal.reserve(mesh.primitives.size());
        bool modified = false;

        for (const auto& entry : buckets) {
            const auto& group = entry.second;
            if (group.size() < 2) {
                continue;
            }

            if (options.verbose) {
                std::cout << "[join] Joining " << group.size() << " primitives in mesh '"
                          << mesh.name << "'" << std::endl;
            }

            JoinSummary summary;
            std::string error;
            const size_t accessorStart = model.accessors.size();
            const size_t viewStart = model.bufferViews.size();
            const size_t bufferStart = model.buffers.size();
            const size_t primitiveStart = mesh.primitives.size();

            if (!joinPrimitiveGroup(group, mesh, model, summary, error)) {
                model.accessors.resize(accessorStart);
                model.bufferViews.resize(viewStart);
                model.buffers.resize(bufferStart);
                mesh.primitives.resize(primitiveStart);

                if (!error.empty()) {
                    error_ = error;
                    return false;
                }
                continue;
            }

            removal.insert(removal.end(), group.begin(), group.end());
            primitivesRemoved += summary.removedPrimitives;
            groupsMerged++;
            modified = true;
        }

        if (modified) {
            std::sort(removal.begin(), removal.end());
            removal.erase(std::unique(removal.begin(), removal.end()), removal.end());

            for (auto it = removal.rbegin(); it != removal.rend(); ++it) {
                if (*it >= 0 && *it < static_cast<int>(mesh.primitives.size())) {
                    mesh.primitives.erase(mesh.primitives.begin() + *it);
                }
            }

            ++meshesModified;
        }
    }

    if (groupsMerged > 0) {
        std::ostringstream stream;
        stream << "Meshes modified: " << meshesModified << '\n'
               << "Groups merged: " << groupsMerged << '\n'
               << "Primitives removed: " << primitivesRemoved;
        stats_ = stream.str();
    } else {
        stats_ = "No compatible primitives found";
    }

    if (options.verbose) {
        std::cout << "[join] " << stats_ << std::endl;
    }

    return true;
}

} // namespace gltfu
