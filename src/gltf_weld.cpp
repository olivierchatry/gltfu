#include "gltf_weld.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <unordered_set>

namespace gltfu {

namespace {
    const uint32_t EMPTY_U32 = 0xFFFFFFFF;
}

GltfWeld::GltfWeld() = default;
GltfWeld::~GltfWeld() = default;

size_t GltfWeld::getElementSize(int type) const {
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

size_t GltfWeld::getComponentSize(int componentType) const {
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

const uint8_t* GltfWeld::getAccessorData(int accessorIdx, const tinygltf::Model& model) const {
    if (accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors.size())) {
        return nullptr;
    }
    
    const auto& accessor = model.accessors[accessorIdx];
    const auto& bufferView = model.bufferViews[accessor.bufferView];
    const auto& buffer = model.buffers[bufferView.buffer];
    
    return buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
}

uint32_t GltfWeld::murmurHash2(uint32_t h, const uint32_t* key, size_t len) {
    const uint32_t m = 0x5bd1e995;
    const uint32_t r = 24;
    
    for (size_t i = 0; i < len; ++i) {
        uint32_t k = key[i];
        k = (k * m) & 0xFFFFFFFF;
        k = (k ^ (k >> r)) & 0xFFFFFFFF;
        k = (k * m) & 0xFFFFFFFF;
        
        h = (h * m) & 0xFFFFFFFF;
        h = (h ^ k) & 0xFFFFFFFF;
    }
    
    return h;
}

uint32_t GltfWeld::ceilPowerOfTwo(uint32_t n) {
    if (n <= 1) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

uint32_t GltfWeld::hashLookup(const std::vector<uint32_t>& table,
                              uint32_t buckets,
                              const VertexStream& stream,
                              uint32_t key,
                              uint32_t empty) {
    uint32_t hashmod = buckets - 1;
    uint32_t hashval = stream.hash(key);
    uint32_t bucket = hashval & hashmod;
    
    for (uint32_t probe = 0; probe <= hashmod; ++probe) {
        uint32_t item = table[bucket];
        
        if (item == empty || stream.equal(item, key)) {
            return bucket;
        }
        
        bucket = (bucket + probe + 1) & hashmod; // Hash collision
    }
    
    // Table full (shouldn't happen with proper sizing)
    return 0;
}

// VertexStream implementation
GltfWeld::VertexStream::VertexStream(const tinygltf::Primitive& prim, const tinygltf::Model& model) {
    // Collect all attributes
    for (const auto& attrPair : prim.attributes) {
        int accessorIdx = attrPair.second;
        if (accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors.size())) {
            continue;
        }
        
        const auto& accessor = model.accessors[accessorIdx];
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        const auto& buffer = model.buffers[bufferView.buffer];
        
        AttributeView view;
        view.data = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
        
        size_t elementSize = 1;
        switch (accessor.type) {
            case TINYGLTF_TYPE_SCALAR: elementSize = 1; break;
            case TINYGLTF_TYPE_VEC2: elementSize = 2; break;
            case TINYGLTF_TYPE_VEC3: elementSize = 3; break;
            case TINYGLTF_TYPE_VEC4: elementSize = 4; break;
            case TINYGLTF_TYPE_MAT2: elementSize = 4; break;
            case TINYGLTF_TYPE_MAT3: elementSize = 9; break;
            case TINYGLTF_TYPE_MAT4: elementSize = 16; break;
        }
        
        size_t componentSize = 1;
        switch (accessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_BYTE:
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                componentSize = 1; break;
            case TINYGLTF_COMPONENT_TYPE_SHORT:
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                componentSize = 2; break;
            case TINYGLTF_COMPONENT_TYPE_INT:
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                componentSize = 4; break;
        }
        
        view.byteStride = elementSize * componentSize;
        
        attributes.push_back(view);
    }
}

uint32_t GltfWeld::VertexStream::hash(uint32_t index) const {
    // Compute hash directly from attribute data without copying
    uint32_t h = 0;
    const uint32_t m = 0x5bd1e995;
    const uint32_t r = 24;
    
    for (const auto& attr : attributes) {
        const uint8_t* src = attr.data + index * attr.byteStride;
        const uint32_t* src32 = reinterpret_cast<const uint32_t*>(src);
        size_t numFullWords = attr.byteStride / 4;
        
        // Hash full 32-bit words
        for (size_t i = 0; i < numFullWords; ++i) {
            uint32_t k = src32[i];
            k = (k * m) & 0xFFFFFFFF;
            k = (k ^ (k >> r)) & 0xFFFFFFFF;
            k = (k * m) & 0xFFFFFFFF;
            h = (h * m) & 0xFFFFFFFF;
            h = (h ^ k) & 0xFFFFFFFF;
        }
        
        // Handle remaining bytes
        size_t remainingBytes = attr.byteStride % 4;
        if (remainingBytes > 0) {
            uint32_t k = 0;
            size_t offset = numFullWords * 4;
            for (size_t i = 0; i < remainingBytes; ++i) {
                k |= static_cast<uint32_t>(src[offset + i]) << (i * 8);
            }
            k = (k * m) & 0xFFFFFFFF;
            k = (k ^ (k >> r)) & 0xFFFFFFFF;
            k = (k * m) & 0xFFFFFFFF;
            h = (h * m) & 0xFFFFFFFF;
            h = (h ^ k) & 0xFFFFFFFF;
        }
    }
    
    return h;
}

bool GltfWeld::VertexStream::equal(uint32_t a, uint32_t b) const {
    if (a == b) return true;
    
    for (const auto& attr : attributes) {
        const uint8_t* dataA = attr.data + a * attr.byteStride;
        const uint8_t* dataB = attr.data + b * attr.byteStride;
        
        // Fast comparison using 64-bit words when possible
        size_t numWords = attr.byteStride / 8;
        const uint64_t* ptrA64 = reinterpret_cast<const uint64_t*>(dataA);
        const uint64_t* ptrB64 = reinterpret_cast<const uint64_t*>(dataB);
        
        for (size_t i = 0; i < numWords; ++i) {
            if (ptrA64[i] != ptrB64[i]) return false;
        }
        
        // Compare remaining bytes
        size_t remaining = attr.byteStride % 8;
        if (remaining > 0) {
            size_t offset = numWords * 8;
            if (std::memcmp(dataA + offset, dataB + offset, remaining) != 0) {
                return false;
            }
        }
    }
    return true;
}

bool GltfWeld::compactPrimitive(tinygltf::Primitive& primitive,
                                tinygltf::Mesh& /* mesh */,
                                tinygltf::Model& model,
                                const std::vector<uint32_t>& remap,
                                uint32_t dstVertexCount) {
    // Create new indices
    int srcIndicesIdx = primitive.indices;
    size_t srcIndicesCount;
    
    if (srcIndicesIdx >= 0) {
        const auto& srcIndices = model.accessors[srcIndicesIdx];
        srcIndicesCount = srcIndices.count;
    } else {
        // If no indices, we need to create them
        int posAccessorIdx = -1;
        auto it = primitive.attributes.find("POSITION");
        if (it != primitive.attributes.end()) {
            posAccessorIdx = it->second;
        }
        if (posAccessorIdx < 0) return false;
        
        const auto& posAccessor = model.accessors[posAccessorIdx];
        srcIndicesCount = posAccessor.count;
    }
    
    // Determine component type for new indices
    int dstComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
    if (dstVertexCount <= 255) {
        dstComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    } else if (dstVertexCount <= 65535) {
        dstComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
    }
    
    size_t dstComponentSize = getComponentSize(dstComponentType);
    size_t dstIndicesBytes = srcIndicesCount * dstComponentSize;
    
    // Create new buffer for indices
    tinygltf::Buffer indicesBuffer;
    indicesBuffer.data.resize(dstIndicesBytes);
    
    // Write remapped indices
    const uint8_t* srcData = nullptr;
    std::vector<uint32_t> srcIndicesArray;
    
    if (srcIndicesIdx >= 0) {
        const auto& srcAccessor = model.accessors[srcIndicesIdx];
        const auto& srcBV = model.bufferViews[srcAccessor.bufferView];
        const auto& srcBuffer = model.buffers[srcBV.buffer];
        srcData = srcBuffer.data.data() + srcBV.byteOffset + srcAccessor.byteOffset;
        
        srcIndicesArray.resize(srcIndicesCount);
        if (srcAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            const uint16_t* src16 = reinterpret_cast<const uint16_t*>(srcData);
            for (size_t i = 0; i < srcIndicesCount; ++i) {
                srcIndicesArray[i] = src16[i];
            }
        } else if (srcAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            const uint32_t* src32 = reinterpret_cast<const uint32_t*>(srcData);
            for (size_t i = 0; i < srcIndicesCount; ++i) {
                srcIndicesArray[i] = src32[i];
            }
        } else if (srcAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            const uint8_t* src8 = srcData;
            for (size_t i = 0; i < srcIndicesCount; ++i) {
                srcIndicesArray[i] = src8[i];
            }
        }
    } else {
        srcIndicesArray.resize(srcIndicesCount);
        for (size_t i = 0; i < srcIndicesCount; ++i) {
            srcIndicesArray[i] = static_cast<uint32_t>(i);
        }
    }
    
    // Write remapped indices to new buffer
    for (size_t i = 0; i < srcIndicesCount; ++i) {
        uint32_t newIdx = remap[srcIndicesArray[i]];
        
        if (dstComponentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            uint16_t* dst = reinterpret_cast<uint16_t*>(indicesBuffer.data.data());
            dst[i] = static_cast<uint16_t>(newIdx);
        } else if (dstComponentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            uint32_t* dst = reinterpret_cast<uint32_t*>(indicesBuffer.data.data());
            dst[i] = newIdx;
        } else if (dstComponentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            uint8_t* dst = indicesBuffer.data.data();
            dst[i] = static_cast<uint8_t>(newIdx);
        }
    }
    
    // Add indices buffer and create buffer view + accessor
    int indicesBufferIdx = static_cast<int>(model.buffers.size());
    model.buffers.push_back(indicesBuffer);
    
    tinygltf::BufferView indicesBV;
    indicesBV.buffer = indicesBufferIdx;
    indicesBV.byteOffset = 0;
    indicesBV.byteLength = dstIndicesBytes;
    indicesBV.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
    int indicesBVIdx = static_cast<int>(model.bufferViews.size());
    model.bufferViews.push_back(indicesBV);
    
    tinygltf::Accessor indicesAccessor;
    indicesAccessor.bufferView = indicesBVIdx;
    indicesAccessor.byteOffset = 0;
    indicesAccessor.componentType = dstComponentType;
    indicesAccessor.count = srcIndicesCount;
    indicesAccessor.type = TINYGLTF_TYPE_SCALAR;
    int indicesAccessorIdx = static_cast<int>(model.accessors.size());
    model.accessors.push_back(indicesAccessor);
    
    primitive.indices = indicesAccessorIdx;
    
    // Compact attributes
    for (auto& attrPair : primitive.attributes) {
        int srcAccessorIdx = attrPair.second;
        const auto& srcAccessor = model.accessors[srcAccessorIdx];
        
        size_t elementSize = getElementSize(srcAccessor.type);
        size_t componentSize = getComponentSize(srcAccessor.componentType);
        size_t vertexStride = elementSize * componentSize;
        
        // Create new buffer for compacted attribute
        tinygltf::Buffer attrBuffer;
        attrBuffer.data.resize(dstVertexCount * vertexStride);
        
        const uint8_t* srcAttrData = getAccessorData(srcAccessorIdx, model);
        if (!srcAttrData) continue;
        
        // Copy vertices according to remap, avoiding duplicates
        std::vector<bool> written(dstVertexCount, false);
        
        for (size_t i = 0; i < srcIndicesCount; ++i) {
            uint32_t srcIdx = srcIndicesArray[i];
            uint32_t dstIdx = remap[srcIdx];
            
            if (written[dstIdx]) continue;
            
            const uint8_t* src = srcAttrData + srcIdx * vertexStride;
            uint8_t* dst = attrBuffer.data.data() + dstIdx * vertexStride;
            std::memcpy(dst, src, vertexStride);
            
            written[dstIdx] = true;
        }
        
        // Add buffer, buffer view, and accessor
        int attrBufferIdx = static_cast<int>(model.buffers.size());
        model.buffers.push_back(attrBuffer);
        
        tinygltf::BufferView attrBV;
        attrBV.buffer = attrBufferIdx;
        attrBV.byteOffset = 0;
        attrBV.byteLength = attrBuffer.data.size();
        attrBV.target = TINYGLTF_TARGET_ARRAY_BUFFER;
        int attrBVIdx = static_cast<int>(model.bufferViews.size());
        model.bufferViews.push_back(attrBV);
        
        tinygltf::Accessor attrAccessor;
        attrAccessor.bufferView = attrBVIdx;
        attrAccessor.byteOffset = 0;
        attrAccessor.componentType = srcAccessor.componentType;
        attrAccessor.count = dstVertexCount;
        attrAccessor.type = srcAccessor.type;
        attrAccessor.normalized = srcAccessor.normalized;
        
        if (!srcAccessor.minValues.empty()) {
            attrAccessor.minValues = srcAccessor.minValues;
        }
        if (!srcAccessor.maxValues.empty()) {
            attrAccessor.maxValues = srcAccessor.maxValues;
        }
        
        int attrAccessorIdx = static_cast<int>(model.accessors.size());
        model.accessors.push_back(attrAccessor);
        
        attrPair.second = attrAccessorIdx;
    }
    
    return true;
}

bool GltfWeld::weldPrimitive(tinygltf::Primitive& primitive,
                             tinygltf::Mesh& mesh,
                             tinygltf::Model& model,
                             const WeldOptions& options) {
    // Skip if already indexed and overwrite is false
    if (primitive.indices >= 0 && !options.overwrite) {
        return true;
    }
    
    // Skip POINTS mode
    if (primitive.mode == TINYGLTF_MODE_POINTS) {
        return true;
    }
    
    // Get position accessor to determine vertex count
    int posAccessorIdx = -1;
    auto it = primitive.attributes.find("POSITION");
    if (it != primitive.attributes.end()) {
        posAccessorIdx = it->second;
    }
    
    if (posAccessorIdx < 0) {
        std::cerr << "Primitive missing POSITION attribute" << std::endl;
        return false;
    }
    
    const auto& posAccessor = model.accessors[posAccessorIdx];
    uint32_t srcVertexCount = static_cast<uint32_t>(posAccessor.count);
    
    // Get source indices count
    uint32_t srcIndicesCount;
    if (primitive.indices >= 0) {
        const auto& indicesAccessor = model.accessors[primitive.indices];
        srcIndicesCount = static_cast<uint32_t>(indicesAccessor.count);
    } else {
        srcIndicesCount = srcVertexCount;
    }
    
    // Create vertex stream for hashing
    VertexStream stream(primitive, model);
    
    // Create hash table
    uint32_t tableSize = ceilPowerOfTwo(srcVertexCount + srcVertexCount / 4);
    std::vector<uint32_t> table(tableSize, EMPTY_U32);
    std::vector<uint32_t> writeMap(srcVertexCount, EMPTY_U32);
    
    // Build index remapping
    uint32_t dstVertexCount = 0;
    
    // Get source indices
    std::vector<uint32_t> srcIndicesArray;
    if (primitive.indices >= 0) {
        const auto& indicesAccessor = model.accessors[primitive.indices];
        const uint8_t* indicesData = getAccessorData(primitive.indices, model);
        
        srcIndicesArray.resize(srcIndicesCount);
        
        if (indicesAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            const uint16_t* indices16 = reinterpret_cast<const uint16_t*>(indicesData);
            for (uint32_t i = 0; i < srcIndicesCount; ++i) {
                srcIndicesArray[i] = indices16[i];
            }
        } else if (indicesAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            const uint32_t* indices32 = reinterpret_cast<const uint32_t*>(indicesData);
            for (uint32_t i = 0; i < srcIndicesCount; ++i) {
                srcIndicesArray[i] = indices32[i];
            }
        } else if (indicesAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            const uint8_t* indices8 = indicesData;
            for (uint32_t i = 0; i < srcIndicesCount; ++i) {
                srcIndicesArray[i] = indices8[i];
            }
        }
    } else {
        srcIndicesArray.resize(srcIndicesCount);
        for (uint32_t i = 0; i < srcIndicesCount; ++i) {
            srcIndicesArray[i] = i;
        }
    }
    
    // Compare and identify indices to weld - process unique vertices only
    std::vector<uint32_t> uniqueVertices;
    uniqueVertices.reserve(srcVertexCount);
    
    for (uint32_t i = 0; i < srcIndicesCount; ++i) {
        uint32_t srcIndex = srcIndicesArray[i];
        if (writeMap[srcIndex] == EMPTY_U32) {
            uniqueVertices.push_back(srcIndex);
            writeMap[srcIndex] = 0; // Mark as seen
        }
    }
    
    // Reset writeMap and process unique vertices only
    for (uint32_t srcIndex : uniqueVertices) {
        writeMap[srcIndex] = EMPTY_U32;
    }
    
    for (uint32_t srcIndex : uniqueVertices) {
        uint32_t hashIndex = hashLookup(table, tableSize, stream, srcIndex, EMPTY_U32);
        uint32_t dstIndex = table[hashIndex];
        
        if (dstIndex == EMPTY_U32) {
            table[hashIndex] = srcIndex;
            writeMap[srcIndex] = dstVertexCount++;
        } else {
            writeMap[srcIndex] = writeMap[dstIndex];
        }
    }
    
    if (options.verbose) {
        std::cout << "  Welded: " << srcVertexCount << " → " << dstVertexCount 
                 << " vertices (" << (srcVertexCount - dstVertexCount) << " removed)" << std::endl;
    }
    
    // Compact primitive
    return compactPrimitive(primitive, mesh, model, writeMap, dstVertexCount);
}

bool GltfWeld::process(tinygltf::Model& model, const WeldOptions& options) {
    int primitivesWelded = 0;
    int meshesProcessed = 0;
    
    for (auto& mesh : model.meshes) {
        bool meshModified = false;
        
        for (auto& prim : mesh.primitives) {
            if (weldPrimitive(prim, mesh, model, options)) {
                primitivesWelded++;
                meshModified = true;
            }
        }
        
        if (meshModified) {
            meshesProcessed++;
        }
    }
    
    if (options.verbose) {
        std::cout << "Weld complete: processed " << meshesProcessed << " meshes, "
                 << "welded " << primitivesWelded << " primitives" << std::endl;
    }
    
    return true;
}

} // namespace gltfu
