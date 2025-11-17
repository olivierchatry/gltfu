#include "gltf_weld.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace gltfu {
namespace {

constexpr uint32_t kEmpty = 0xffffffffu;

size_t elementWidth(int type) {
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

size_t componentSize(int componentType) {
    switch (componentType) {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return 1;
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return 2;
        case TINYGLTF_COMPONENT_TYPE_INT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
            return 4;
        default:
            return 4;
    }
}

uint32_t ceilPowerOfTwo(uint32_t n) {
    if (n <= 1) {
        return 1;
    }

    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

const uint8_t* accessorData(const tinygltf::Model& model, int accessorIdx) {
    if (accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors.size())) {
        return nullptr;
    }

    const auto& accessor = model.accessors[accessorIdx];
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        return nullptr;
    }

    const auto& bufferView = model.bufferViews[accessor.bufferView];
    if (bufferView.buffer < 0 || bufferView.buffer >= static_cast<int>(model.buffers.size())) {
        return nullptr;
    }

    const auto& buffer = model.buffers[bufferView.buffer];
    size_t offset = bufferView.byteOffset + accessor.byteOffset;
    if (offset >= buffer.data.size()) {
        return nullptr;
    }

    return buffer.data.data() + offset;
}

size_t vertexStride(const tinygltf::Accessor& accessor, const tinygltf::Model& model) {
    if (accessor.bufferView >= 0 && accessor.bufferView < static_cast<int>(model.bufferViews.size())) {
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        if (bufferView.byteStride > 0) {
            return bufferView.byteStride;
        }
    }

    return elementWidth(accessor.type) * componentSize(accessor.componentType);
}

struct AttributeView {
    const uint8_t* base = nullptr;
    size_t stride = 0;
};

class VertexStream {
public:
    VertexStream(const tinygltf::Primitive& primitive, const tinygltf::Model& model) {
        attributes.reserve(primitive.attributes.size());
        for (const auto& attribute : primitive.attributes) {
            const int accessorIdx = attribute.second;
            const uint8_t* data = accessorData(model, accessorIdx);
            if (!data) {
                continue;
            }

            const auto& accessor = model.accessors[accessorIdx];
            AttributeView view{};
            view.base = data;
            view.stride = vertexStride(accessor, model);
            attributes.push_back(view);
        }
    }

    uint32_t hash(uint32_t index) const {
        uint32_t h = 0;
        constexpr uint32_t m = 0x5bd1e995;
        constexpr uint32_t r = 24;

        for (const auto& attr : attributes) {
            const uint8_t* src = attr.base + index * attr.stride;
            const auto* words = reinterpret_cast<const uint32_t*>(src);
            const size_t wordCount = attr.stride / 4;

            for (size_t i = 0; i < wordCount; ++i) {
                uint32_t k = words[i];
                k = (k * m) & 0xffffffffu;
                k = (k ^ (k >> r)) & 0xffffffffu;
                k = (k * m) & 0xffffffffu;
                h = (h * m) & 0xffffffffu;
                h = (h ^ k) & 0xffffffffu;
            }

            const size_t remaining = attr.stride % 4;
            if (remaining == 0) {
                continue;
            }

            uint32_t k = 0;
            const size_t offset = wordCount * 4;
            for (size_t i = 0; i < remaining; ++i) {
                k |= static_cast<uint32_t>(src[offset + i]) << (i * 8);
            }

            k = (k * m) & 0xffffffffu;
            k = (k ^ (k >> r)) & 0xffffffffu;
            k = (k * m) & 0xffffffffu;
            h = (h * m) & 0xffffffffu;
            h = (h ^ k) & 0xffffffffu;
        }

        return h;
    }

    bool equal(uint32_t a, uint32_t b) const {
        if (a == b) {
            return true;
        }

        for (const auto& attr : attributes) {
            const uint8_t* lhs = attr.base + a * attr.stride;
            const uint8_t* rhs = attr.base + b * attr.stride;

            const size_t wordCount = attr.stride / 8;
            const auto* lhsWords = reinterpret_cast<const uint64_t*>(lhs);
            const auto* rhsWords = reinterpret_cast<const uint64_t*>(rhs);
            for (size_t i = 0; i < wordCount; ++i) {
                if (lhsWords[i] != rhsWords[i]) {
                    return false;
                }
            }

            const size_t remaining = attr.stride % 8;
            if (remaining == 0) {
                continue;
            }

            const size_t offset = wordCount * 8;
            if (std::memcmp(lhs + offset, rhs + offset, remaining) != 0) {
                return false;
            }
        }

        return true;
    }

private:
    std::vector<AttributeView> attributes;
};

uint32_t findSlot(const std::vector<uint32_t>& table,
                  uint32_t bucketCount,
                  const VertexStream& stream,
                  uint32_t key) {
    const uint32_t mask = bucketCount - 1;
    uint32_t bucket = stream.hash(key) & mask;

    for (uint32_t probe = 0; probe <= mask; ++probe) {
        const uint32_t value = table[bucket];
        if (value == kEmpty || stream.equal(value, key)) {
            return bucket;
        }
        bucket = (bucket + probe + 1) & mask;
    }

    return bucket;
}

std::vector<uint32_t> readIndices(const tinygltf::Primitive& primitive,
                                  const tinygltf::Model& model,
                                  uint32_t vertexCount) {
    if (primitive.indices < 0) {
        std::vector<uint32_t> indices(vertexCount);
        for (uint32_t i = 0; i < vertexCount; ++i) {
            indices[i] = i;
        }
        return indices;
    }

    std::vector<uint32_t> indices;
    const auto& accessor = model.accessors[primitive.indices];
    const uint8_t* data = accessorData(model, primitive.indices);
    if (!data) {
        return indices;
    }

    indices.resize(accessor.count);
    switch (accessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            const auto* src = reinterpret_cast<const uint8_t*>(data);
            for (size_t i = 0; i < accessor.count; ++i) {
                indices[i] = src[i];
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            const auto* src = reinterpret_cast<const uint16_t*>(data);
            for (size_t i = 0; i < accessor.count; ++i) {
                indices[i] = src[i];
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
            const auto* src = reinterpret_cast<const uint32_t*>(data);
            for (size_t i = 0; i < accessor.count; ++i) {
                indices[i] = src[i];
            }
            break;
        }
        default:
            indices.clear();
            break;
    }

    return indices;
}

bool compactPrimitive(tinygltf::Primitive& primitive,
                      tinygltf::Model& model,
                      const std::vector<uint32_t>& srcIndices,
                      const std::vector<uint32_t>& remap,
                      uint32_t dstVertexCount) {
    if (srcIndices.empty()) {
        return true;
    }

    const size_t indexCount = srcIndices.size();

    int dstComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
    if (dstVertexCount <= 255) {
        dstComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    } else if (dstVertexCount <= 65535) {
        dstComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
    }

    const size_t componentBytes = componentSize(dstComponentType);
    tinygltf::Buffer indexBuffer;
    indexBuffer.data.resize(indexCount * componentBytes);

    switch (dstComponentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            auto* dst = reinterpret_cast<uint8_t*>(indexBuffer.data.data());
            for (size_t i = 0; i < indexCount; ++i) {
                dst[i] = static_cast<uint8_t>(remap[srcIndices[i]]);
            }
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            auto* dst = reinterpret_cast<uint16_t*>(indexBuffer.data.data());
            for (size_t i = 0; i < indexCount; ++i) {
                dst[i] = static_cast<uint16_t>(remap[srcIndices[i]]);
            }
            break;
        }
        default: {
            auto* dst = reinterpret_cast<uint32_t*>(indexBuffer.data.data());
            for (size_t i = 0; i < indexCount; ++i) {
                dst[i] = remap[srcIndices[i]];
            }
            break;
        }
    }

    const int indexBufferIdx = static_cast<int>(model.buffers.size());
    model.buffers.push_back(indexBuffer);

    tinygltf::BufferView indexView;
    indexView.buffer = indexBufferIdx;
    indexView.byteOffset = 0;
    indexView.byteLength = indexBuffer.data.size();
    indexView.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
    const int indexViewIdx = static_cast<int>(model.bufferViews.size());
    model.bufferViews.push_back(indexView);

    tinygltf::Accessor indexAccessor;
    indexAccessor.bufferView = indexViewIdx;
    indexAccessor.byteOffset = 0;
    indexAccessor.componentType = dstComponentType;
    indexAccessor.count = indexCount;
    indexAccessor.type = TINYGLTF_TYPE_SCALAR;
    const int indexAccessorIdx = static_cast<int>(model.accessors.size());
    model.accessors.push_back(indexAccessor);

    primitive.indices = indexAccessorIdx;

    for (auto& entry : primitive.attributes) {
        const int accessorIdx = entry.second;
        if (accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors.size())) {
            continue;
        }

        const auto srcAccessor = model.accessors[accessorIdx];
        const size_t stride = vertexStride(srcAccessor, model);
        const uint8_t* srcData = accessorData(model, accessorIdx);
        if (!srcData || stride == 0) {
            continue;
        }

        tinygltf::Buffer attributeBuffer;
        attributeBuffer.data.resize(static_cast<size_t>(dstVertexCount) * stride);

        std::vector<char> written(dstVertexCount, 0);
        for (size_t i = 0; i < indexCount; ++i) {
            const uint32_t srcIdx = srcIndices[i];
            const uint32_t dstIdx = remap[srcIdx];
            if (written[dstIdx]) {
                continue;
            }

            const uint8_t* src = srcData + static_cast<size_t>(srcIdx) * stride;
            uint8_t* dst = attributeBuffer.data.data() + static_cast<size_t>(dstIdx) * stride;
            std::memcpy(dst, src, stride);
            written[dstIdx] = 1;
        }

        const int bufferIdx = static_cast<int>(model.buffers.size());
        model.buffers.push_back(attributeBuffer);

        tinygltf::BufferView view;
        view.buffer = bufferIdx;
        view.byteOffset = 0;
        view.byteLength = model.buffers[bufferIdx].data.size();
        view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
        const int viewIdx = static_cast<int>(model.bufferViews.size());
        model.bufferViews.push_back(view);

        tinygltf::Accessor accessor;
        accessor.bufferView = viewIdx;
        accessor.byteOffset = 0;
        accessor.componentType = srcAccessor.componentType;
        accessor.count = dstVertexCount;
        accessor.type = srcAccessor.type;
        accessor.normalized = srcAccessor.normalized;
        accessor.minValues = srcAccessor.minValues;
        accessor.maxValues = srcAccessor.maxValues;

        const int newAccessorIdx = static_cast<int>(model.accessors.size());
        model.accessors.push_back(accessor);
        entry.second = newAccessorIdx;
    }

    return true;
}

bool weldPrimitive(tinygltf::Primitive& primitive,
                   tinygltf::Model& model,
                   const WeldOptions& options) {
    if (primitive.indices >= 0 && !options.overwrite) {
        return true;
    }

    if (primitive.mode == TINYGLTF_MODE_POINTS) {
        return true;
    }

    const auto positionIt = primitive.attributes.find("POSITION");
    if (positionIt == primitive.attributes.end()) {
        std::cerr << "Primitive missing POSITION attribute" << std::endl;
        return false;
    }

    const auto& positionAccessor = model.accessors[positionIt->second];
    const uint32_t vertexCount = static_cast<uint32_t>(positionAccessor.count);
    if (vertexCount == 0) {
        return true;
    }

    const std::vector<uint32_t> sourceIndices = readIndices(primitive, model, vertexCount);
    if (primitive.indices >= 0 && sourceIndices.empty()) {
        std::cerr << "Failed to read primitive indices" << std::endl;
        return false;
    }

    VertexStream stream(primitive, model);
    const uint32_t tableSize = ceilPowerOfTwo(std::max<uint32_t>(1, vertexCount + vertexCount / 4));
    std::vector<uint32_t> table(tableSize, kEmpty);
    std::vector<uint32_t> remap(vertexCount, kEmpty);

    uint32_t dstVertexCount = 0;
    for (uint32_t srcIdx : sourceIndices) {
        if (srcIdx >= vertexCount) {
            continue;
        }

        if (remap[srcIdx] != kEmpty) {
            continue;
        }

        const uint32_t slot = findSlot(table, tableSize, stream, srcIdx);
        if (table[slot] == kEmpty) {
            table[slot] = srcIdx;
            remap[srcIdx] = dstVertexCount++;
        } else {
            remap[srcIdx] = remap[table[slot]];
        }
    }

    if (dstVertexCount == 0) {
        return true;
    }

    if (options.verbose) {
        std::cout << "  Welded: " << vertexCount << " → " << dstVertexCount
                  << " vertices (" << (vertexCount - dstVertexCount) << " removed)" << std::endl;
    }

    return compactPrimitive(primitive, model, sourceIndices, remap, dstVertexCount);
}

} // namespace

bool GltfWeld::process(tinygltf::Model& model, const WeldOptions& options) {
    int weldedPrimitives = 0;
    int touchedMeshes = 0;

    for (auto& mesh : model.meshes) {
        bool meshChanged = false;
        for (auto& primitive : mesh.primitives) {
            if (weldPrimitive(primitive, model, options)) {
                meshChanged = true;
                ++weldedPrimitives;
            }
        }
        if (meshChanged) {
            ++touchedMeshes;
        }
    }

    if (options.verbose) {
        std::cout << "Weld complete: processed " << touchedMeshes << " meshes, welded "
                  << weldedPrimitives << " primitives" << std::endl;
    }

    return true;
}

} // namespace gltfu
