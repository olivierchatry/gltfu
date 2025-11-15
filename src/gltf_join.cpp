#include "gltf_join.h"
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <cstring>

namespace gltfu {

namespace {
    const uint32_t EMPTY_U32 = 0xFFFFFFFF;

    // Get typed array size for component type
    size_t getTypedArraySize(int componentType) {
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

    // Get number of components for accessor type
    size_t getNumComponents(int type) {
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

    // Create a new buffer and return its index
    int createBuffer(tinygltf::Model& model, size_t size) {
        tinygltf::Buffer buffer;
        buffer.data.resize(size);
        model.buffers.push_back(buffer);
        return static_cast<int>(model.buffers.size() - 1);
    }

    // Create a new buffer view and return its index
    int createBufferView(tinygltf::Model& model, int bufferIdx, size_t byteOffset, size_t byteLength, int target = 0) {
        tinygltf::BufferView bv;
        bv.buffer = bufferIdx;
        bv.byteOffset = byteOffset;
        bv.byteLength = byteLength;
        if (target != 0) {
            bv.target = target;
        }
        model.bufferViews.push_back(bv);
        return static_cast<int>(model.bufferViews.size() - 1);
    }

    // Create a new accessor and return its index
    int createAccessor(tinygltf::Model& model, int bufferViewIdx, int componentType, 
                      size_t count, int type, size_t byteOffset = 0) {
        tinygltf::Accessor accessor;
        accessor.bufferView = bufferViewIdx;
        accessor.byteOffset = byteOffset;
        accessor.componentType = componentType;
        accessor.count = count;
        accessor.type = type;
        model.accessors.push_back(accessor);
        return static_cast<int>(model.accessors.size() - 1);
    }
}

GltfJoin::GltfJoin() = default;
GltfJoin::~GltfJoin() = default;

size_t GltfJoin::getComponentSize(int componentType) const {
    return getTypedArraySize(componentType);
}

size_t GltfJoin::getElementSize(int type) const {
    return getNumComponents(type);
}

std::string GltfJoin::createPrimGroupKey(const tinygltf::Primitive& prim, 
                                         const tinygltf::Model& model) const {
    std::stringstream ss;
    
    // Material index
    ss << "mat:" << prim.material << "|";
    
    // Mode
    ss << "mode:" << prim.mode << "|";
    
    // Has indices?
    ss << "idx:" << (prim.indices >= 0 ? 1 : 0) << "|";
    
    // Attributes (sorted by semantic)
    std::vector<std::string> attrKeys;
    for (const auto& attr : prim.attributes) {
        attrKeys.push_back(attr.first);
    }
    std::sort(attrKeys.begin(), attrKeys.end());
    
    ss << "attrs:";
    for (const auto& semantic : attrKeys) {
        int accessorIdx = prim.attributes.at(semantic);
        if (accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors.size())) {
            continue;
        }
        const auto& accessor = model.accessors[accessorIdx];
        ss << semantic << ":" 
           << getElementSize(accessor.type) << ":" 
           << accessor.componentType << "+";
    }
    
    // Morph targets
    ss << "|targets:" << prim.targets.size();
    
    return ss.str();
}

bool GltfJoin::arePrimitivesCompatible(const tinygltf::Primitive& prim1,
                                       const tinygltf::Primitive& prim2,
                                       const tinygltf::Model& model) const {
    return createPrimGroupKey(prim1, model) == createPrimGroupKey(prim2, model);
}

void GltfJoin::remapAttribute(int srcAccessorIdx,
                              int srcIndicesIdx,
                              const std::vector<uint32_t>& remap,
                              int dstAccessorIdx,
                              tinygltf::Model& model) {
    if (srcAccessorIdx < 0 || dstAccessorIdx < 0) return;
    
    const auto& srcAccessor = model.accessors[srcAccessorIdx];
    const auto& dstAccessor = model.accessors[dstAccessorIdx];
    
    const auto& srcBV = model.bufferViews[srcAccessor.bufferView];
    const auto& dstBV = model.bufferViews[dstAccessor.bufferView];
    
    auto& srcBuffer = model.buffers[srcBV.buffer];
    auto& dstBuffer = model.buffers[dstBV.buffer];
    
    size_t elementSize = getElementSize(srcAccessor.type);
    size_t componentSize = getComponentSize(srcAccessor.componentType);
    size_t stride = elementSize * componentSize;
    
    const uint8_t* srcData = srcBuffer.data.data() + srcBV.byteOffset + srcAccessor.byteOffset;
    uint8_t* dstData = dstBuffer.data.data() + dstBV.byteOffset + dstAccessor.byteOffset;
    
    // Build done array to avoid writing same vertex multiple times
    std::vector<uint8_t> done(dstAccessor.count, 0);
    
    // Determine iteration count
    size_t iterCount = srcAccessor.count;
    std::vector<uint32_t> srcIndices;
    
    if (srcIndicesIdx >= 0) {
        const auto& indicesAccessor = model.accessors[srcIndicesIdx];
        const auto& indicesBV = model.bufferViews[indicesAccessor.bufferView];
        const auto& indicesBuffer = model.buffers[indicesBV.buffer];
        const uint8_t* indicesData = indicesBuffer.data.data() + indicesBV.byteOffset + indicesAccessor.byteOffset;
        
        srcIndices.resize(indicesAccessor.count);
        iterCount = indicesAccessor.count;
        
        // Read indices based on component type
        if (indicesAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            const uint16_t* indices16 = reinterpret_cast<const uint16_t*>(indicesData);
            for (size_t i = 0; i < indicesAccessor.count; ++i) {
                srcIndices[i] = indices16[i];
            }
        } else if (indicesAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            const uint32_t* indices32 = reinterpret_cast<const uint32_t*>(indicesData);
            for (size_t i = 0; i < indicesAccessor.count; ++i) {
                srcIndices[i] = indices32[i];
            }
        } else if (indicesAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            const uint8_t* indices8 = indicesData;
            for (size_t i = 0; i < indicesAccessor.count; ++i) {
                srcIndices[i] = indices8[i];
            }
        }
    } else {
        // No indices, iterate sequentially
        srcIndices.resize(srcAccessor.count);
        for (size_t i = 0; i < srcAccessor.count; ++i) {
            srcIndices[i] = static_cast<uint32_t>(i);
        }
    }
    
    // Remap attributes
    for (size_t i = 0; i < iterCount; ++i) {
        uint32_t srcIndex = srcIndices[i];
        uint32_t dstIndex = remap[srcIndex];
        
        if (dstIndex >= dstAccessor.count || done[dstIndex]) continue;
        
        const uint8_t* src = srcData + srcIndex * stride;
        uint8_t* dst = dstData + dstIndex * stride;
        
        std::memcpy(dst, src, stride);
        done[dstIndex] = 1;
    }
}

void GltfJoin::remapIndices(int srcIndicesIdx,
                            const std::vector<uint32_t>& remap,
                            int dstIndicesIdx,
                            size_t dstOffset,
                            tinygltf::Model& model) {
    if (srcIndicesIdx < 0 || dstIndicesIdx < 0) return;
    
    const auto& srcAccessor = model.accessors[srcIndicesIdx];
    const auto& dstAccessor = model.accessors[dstIndicesIdx];
    
    const auto& srcBV = model.bufferViews[srcAccessor.bufferView];
    const auto& dstBV = model.bufferViews[dstAccessor.bufferView];
    
    const auto& srcBuffer = model.buffers[srcBV.buffer];
    auto& dstBuffer = model.buffers[dstBV.buffer];
    
    const uint8_t* srcData = srcBuffer.data.data() + srcBV.byteOffset + srcAccessor.byteOffset;
    uint8_t* dstData = dstBuffer.data.data() + dstBV.byteOffset + dstAccessor.byteOffset;
    
    // Read source indices
    std::vector<uint32_t> srcIndices(srcAccessor.count);
    if (srcAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        const uint16_t* indices16 = reinterpret_cast<const uint16_t*>(srcData);
        for (size_t i = 0; i < srcAccessor.count; ++i) {
            srcIndices[i] = indices16[i];
        }
    } else if (srcAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        const uint32_t* indices32 = reinterpret_cast<const uint32_t*>(srcData);
        for (size_t i = 0; i < srcAccessor.count; ++i) {
            srcIndices[i] = indices32[i];
        }
    } else if (srcAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        const uint8_t* indices8 = srcData;
        for (size_t i = 0; i < srcAccessor.count; ++i) {
            srcIndices[i] = indices8[i];
        }
    }
    
    // Write remapped indices to destination
    for (size_t i = 0; i < srcAccessor.count; ++i) {
        uint32_t srcIndex = srcIndices[i];
        uint32_t dstIndex = remap[srcIndex];
        size_t writeOffset = dstOffset + i;
        
        if (dstAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            uint16_t* dst16 = reinterpret_cast<uint16_t*>(dstData);
            dst16[writeOffset] = static_cast<uint16_t>(dstIndex);
        } else if (dstAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            uint32_t* dst32 = reinterpret_cast<uint32_t*>(dstData);
            dst32[writeOffset] = dstIndex;
        } else if (dstAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            uint8_t* dst8 = dstData;
            dst8[writeOffset] = static_cast<uint8_t>(dstIndex);
        }
    }
}

int GltfJoin::joinPrimitives(const std::vector<int>& primIndices,
                             tinygltf::Mesh& mesh,
                             tinygltf::Model& model) {
    if (primIndices.size() < 2) return -1;
    
    // Use first primitive as template
    const auto& templatePrim = mesh.primitives[primIndices[0]];
    
    // Validate compatibility
    for (size_t i = 1; i < primIndices.size(); ++i) {
        if (!arePrimitivesCompatible(templatePrim, mesh.primitives[primIndices[i]], model)) {
            std::cerr << "Primitives not compatible for joining" << std::endl;
            return -1;
        }
    }
    
    // Build remap lists and count vertices
    std::vector<std::vector<uint32_t>> primRemaps;
    std::vector<uint32_t> primVertexCounts(primIndices.size(), 0);
    
    uint32_t dstVertexCount = 0;
    uint32_t dstIndicesCount = 0;
    
    // Build remap for each primitive
    for (size_t primIdx = 0; primIdx < primIndices.size(); ++primIdx) {
        const auto& prim = mesh.primitives[primIndices[primIdx]];
        
        // Get position accessor to determine vertex count
        int posAccessorIdx = -1;
        auto posIt = prim.attributes.find("POSITION");
        if (posIt != prim.attributes.end()) {
            posAccessorIdx = posIt->second;
        }
        
        if (posAccessorIdx < 0) {
            std::cerr << "Primitive missing POSITION attribute" << std::endl;
            return -1;
        }
        
        const auto& posAccessor = model.accessors[posAccessorIdx];
        uint32_t srcVertexCount = static_cast<uint32_t>(posAccessor.count);
        
        // Get indices if present
        std::vector<uint32_t> srcIndices;
        if (prim.indices >= 0) {
            const auto& indicesAccessor = model.accessors[prim.indices];
            const auto& indicesBV = model.bufferViews[indicesAccessor.bufferView];
            const auto& indicesBuffer = model.buffers[indicesBV.buffer];
            const uint8_t* indicesData = indicesBuffer.data.data() + indicesBV.byteOffset + indicesAccessor.byteOffset;
            
            srcIndices.resize(indicesAccessor.count);
            
            if (indicesAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                const uint16_t* indices16 = reinterpret_cast<const uint16_t*>(indicesData);
                for (size_t i = 0; i < indicesAccessor.count; ++i) {
                    srcIndices[i] = indices16[i];
                }
            } else if (indicesAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                const uint32_t* indices32 = reinterpret_cast<const uint32_t*>(indicesData);
                for (size_t i = 0; i < indicesAccessor.count; ++i) {
                    srcIndices[i] = indices32[i];
                }
            } else if (indicesAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                const uint8_t* indices8 = indicesData;
                for (size_t i = 0; i < indicesAccessor.count; ++i) {
                    srcIndices[i] = indices8[i];
                }
            }
            
            dstIndicesCount += static_cast<uint32_t>(indicesAccessor.count);
        } else {
            srcIndices.resize(srcVertexCount);
            for (uint32_t i = 0; i < srcVertexCount; ++i) {
                srcIndices[i] = i;
            }
            dstIndicesCount += srcVertexCount;
        }
        
        // Build remap
        std::vector<uint32_t> remap(srcVertexCount, EMPTY_U32);
        
        for (uint32_t srcIndex : srcIndices) {
            if (srcIndex >= srcVertexCount) continue;
            
            if (remap[srcIndex] == EMPTY_U32) {
                remap[srcIndex] = dstVertexCount++;
                primVertexCounts[primIdx]++;
            }
        }
        
        primRemaps.push_back(std::move(remap));
    }
    
    // Create destination primitive
    tinygltf::Primitive dstPrim;
    dstPrim.mode = templatePrim.mode;
    dstPrim.material = templatePrim.material;
    
    // Allocate joined attributes
    for (const auto& attrPair : templatePrim.attributes) {
        const std::string& semantic = attrPair.first;
        int tplAccessorIdx = attrPair.second;
        const auto& tplAccessor = model.accessors[tplAccessorIdx];
        
        size_t elementSize = getElementSize(tplAccessor.type);
        size_t componentSize = getComponentSize(tplAccessor.componentType);
        size_t totalBytes = dstVertexCount * elementSize * componentSize;
        
        // Create buffer for this attribute
        int bufferIdx = createBuffer(model, totalBytes);
        int bufferViewIdx = createBufferView(model, bufferIdx, 0, totalBytes, TINYGLTF_TARGET_ARRAY_BUFFER);
        int accessorIdx = createAccessor(model, bufferViewIdx, tplAccessor.componentType, 
                                        dstVertexCount, tplAccessor.type);
        
        dstPrim.attributes[semantic] = accessorIdx;
    }
    
    // Allocate joined indices
    if (templatePrim.indices >= 0) {
        const auto& tplIndicesAccessor = model.accessors[templatePrim.indices];
        
        // Determine appropriate component type for destination indices
        int dstComponentType = tplIndicesAccessor.componentType;
        if (dstVertexCount > 65535) {
            dstComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
        } else if (dstVertexCount > 255) {
            dstComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
        } else {
            dstComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
        }
        
        size_t componentSize = getComponentSize(dstComponentType);
        size_t totalBytes = dstIndicesCount * componentSize;
        
        int bufferIdx = createBuffer(model, totalBytes);
        int bufferViewIdx = createBufferView(model, bufferIdx, 0, totalBytes, TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);
        int indicesIdx = createAccessor(model, bufferViewIdx, dstComponentType, dstIndicesCount, TINYGLTF_TYPE_SCALAR);
        
        dstPrim.indices = indicesIdx;
    } else {
        dstPrim.indices = -1;
    }
    
    // Remap attributes and indices into joined primitive
    size_t dstIndicesOffset = 0;
    
    for (size_t primIdx = 0; primIdx < primIndices.size(); ++primIdx) {
        const auto& srcPrim = mesh.primitives[primIndices[primIdx]];
        const auto& remap = primRemaps[primIdx];
        
        // Remap indices if present
        if (srcPrim.indices >= 0 && dstPrim.indices >= 0) {
            remapIndices(srcPrim.indices, remap, dstPrim.indices, dstIndicesOffset, model);
            
            const auto& srcIndicesAccessor = model.accessors[srcPrim.indices];
            dstIndicesOffset += srcIndicesAccessor.count;
        }
        
        // Remap all attributes
        for (const auto& attrPair : dstPrim.attributes) {
            const std::string& semantic = attrPair.first;
            int dstAccessorIdx = attrPair.second;
            
            auto srcIt = srcPrim.attributes.find(semantic);
            if (srcIt == srcPrim.attributes.end()) continue;
            
            int srcAccessorIdx = srcIt->second;
            remapAttribute(srcAccessorIdx, srcPrim.indices, remap, dstAccessorIdx, model);
        }
    }
    
    // Add the joined primitive to the mesh
    mesh.primitives.push_back(dstPrim);
    return static_cast<int>(mesh.primitives.size() - 1);
}

bool GltfJoin::process(tinygltf::Model& model, const JoinOptions& options) {
    std::cout << "Starting join operation..." << std::endl;
    
    int primitivesJoined = 0;
    int meshesProcessed = 0;
    
    // Process each mesh
    for (auto& mesh : model.meshes) {
        if (mesh.primitives.size() < 2) continue;
        
        // Group primitives by compatibility key
        std::unordered_map<std::string, std::vector<int>> groups;
        
        for (size_t i = 0; i < mesh.primitives.size(); ++i) {
            const auto& prim = mesh.primitives[i];
            
            // Skip primitives with morph targets (not supported yet)
            if (!prim.targets.empty()) {
                std::cout << "  Skipping primitive with morph targets" << std::endl;
                continue;
            }
            
            std::string key = createPrimGroupKey(prim, model);
            
            // If keepNamed is enabled, add mesh name to key
            if (options.keepNamed && !mesh.name.empty()) {
                key += "|mesh:" + mesh.name;
            }
            
            groups[key].push_back(static_cast<int>(i));
        }
        
        // Find groups with 2+ primitives
        std::vector<std::vector<int>> joinGroups;
        for (const auto& pair : groups) {
            if (pair.second.size() >= 2) {
                joinGroups.push_back(pair.second);
            }
        }
        
        if (joinGroups.empty()) continue;
        
        // Join each group
        std::unordered_set<int> primsToRemove;
        
        for (const auto& group : joinGroups) {
            std::cout << "  Joining " << group.size() << " primitives in mesh '" 
                     << mesh.name << "'" << std::endl;
            
            int joinedIdx = joinPrimitives(group, mesh, model);
            
            if (joinedIdx >= 0) {
                // Mark original primitives for removal
                for (int idx : group) {
                    primsToRemove.insert(idx);
                }
                primitivesJoined += static_cast<int>(group.size());
            }
        }
        
        // Remove joined primitives (iterate backwards to maintain indices)
        if (!primsToRemove.empty()) {
            std::vector<tinygltf::Primitive> newPrimitives;
            for (size_t i = 0; i < mesh.primitives.size(); ++i) {
                if (primsToRemove.find(static_cast<int>(i)) == primsToRemove.end()) {
                    newPrimitives.push_back(mesh.primitives[i]);
                }
            }
            mesh.primitives = std::move(newPrimitives);
            meshesProcessed++;
        }
    }
    
    std::cout << "Join complete: processed " << meshesProcessed << " meshes, "
             << "joined " << primitivesJoined << " primitives" << std::endl;
    
    return true;
}

} // namespace gltfu
