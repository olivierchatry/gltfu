#include "gltf_compress.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cfloat>
#include <algorithm>

#ifdef GLTFU_ENABLE_DRACO
#include "draco/compression/encode.h"
#include "draco/core/encoder_buffer.h"
#include "draco/mesh/mesh.h"
#include "draco/point_cloud/point_cloud.h"
#endif

namespace gltfu {

GltfCompress::GltfCompress() = default;
GltfCompress::~GltfCompress() = default;

bool GltfCompress::process(tinygltf::Model& model, const CompressOptions& options) {
#ifndef GLTFU_ENABLE_DRACO
    error_ = "Draco compression is not enabled. Rebuild with Draco support.";
    return false;
#else
    stats_ = "";
    error_ = "";
    
    size_t totalOriginalSize = 0;
    size_t totalCompressedSize = 0;
    int skippedPrimitives = 0;
    
    // Add KHR_draco_mesh_compression to extensionsUsed if not already present
    bool hasExtension = false;
    for (const auto& ext : model.extensionsUsed) {
        if (ext == "KHR_draco_mesh_compression") {
            hasExtension = true;
            break;
        }
    }
    if (!hasExtension) {
        model.extensionsUsed.push_back("KHR_draco_mesh_compression");
    }
    
    // Add to extensionsRequired as well
    hasExtension = false;
    for (const auto& ext : model.extensionsRequired) {
        if (ext == "KHR_draco_mesh_compression") {
            hasExtension = true;
            break;
        }
    }
    if (!hasExtension) {
        model.extensionsRequired.push_back("KHR_draco_mesh_compression");
    }
    
    // Create a single consolidated buffer for all compressed data
    tinygltf::Buffer compressedBuffer;
    std::vector<uint8_t> bufferData;
    
    // Track primitives and their compressed data offsets
    struct PrimitiveCompressData {
        size_t meshIdx;
        size_t primIdx;
        size_t byteOffset;
        size_t byteLength;
    };
    std::vector<PrimitiveCompressData> compressedPrimitives;
    
    // Process each mesh
    for (size_t meshIdx = 0; meshIdx < model.meshes.size(); ++meshIdx) {
        auto& mesh = model.meshes[meshIdx];
        
        for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
            auto& primitive = mesh.primitives[primIdx];
            
            // Calculate original size
            size_t originalSize = 0;
            for (const auto& attr : primitive.attributes) {
                if (attr.second >= 0 && static_cast<size_t>(attr.second) < model.accessors.size()) {
                    const auto& accessor = model.accessors[attr.second];
                    if (accessor.bufferView >= 0 && static_cast<size_t>(accessor.bufferView) < model.bufferViews.size()) {
                        const auto& bufferView = model.bufferViews[accessor.bufferView];
                        originalSize += bufferView.byteLength;
                    }
                }
            }
            
            // Include indices
            if (primitive.indices >= 0 && static_cast<size_t>(primitive.indices) < model.accessors.size()) {
                const auto& accessor = model.accessors[primitive.indices];
                if (accessor.bufferView >= 0 && static_cast<size_t>(accessor.bufferView) < model.bufferViews.size()) {
                    const auto& bufferView = model.bufferViews[accessor.bufferView];
                    originalSize += bufferView.byteLength;
                }
            }
            
            totalOriginalSize += originalSize;
            
            // Compress the primitive
            std::vector<uint8_t> compressedData;
            if (!compressPrimitive(model, mesh, primIdx, options, compressedData)) {
                skippedPrimitives++;
                continue;
            }
            
            // Record offset and append to consolidated buffer
            size_t byteOffset = bufferData.size();
            bufferData.insert(bufferData.end(), compressedData.begin(), compressedData.end());
            
            compressedPrimitives.push_back({meshIdx, primIdx, byteOffset, compressedData.size()});
            totalCompressedSize += compressedData.size();
            
            if (options.verbose) {
                double ratio = originalSize > 0 ? (double)compressedData.size() / originalSize * 100.0 : 0.0;
                std::cout << "  Compressed primitive " << compressedPrimitives.size() - 1
                          << ": " << originalSize << " → " << compressedData.size() 
                          << " bytes (" << std::fixed << std::setprecision(1) << ratio << "%)" 
                          << std::endl;
            }
        }
    }
    
    // Add the consolidated buffer to the model and create bufferViews
    if (!compressedPrimitives.empty() && !bufferData.empty()) {
        compressedBuffer.data = std::move(bufferData);
        model.buffers.push_back(compressedBuffer);
        int bufferIdx = static_cast<int>(model.buffers.size() - 1);
        
        // Create bufferViews and update primitive extensions
        for (const auto& compData : compressedPrimitives) {
            tinygltf::BufferView bufferView;
            bufferView.buffer = bufferIdx;
            bufferView.byteOffset = compData.byteOffset;
            bufferView.byteLength = compData.byteLength;
            model.bufferViews.push_back(bufferView);
            int bufferViewIdx = static_cast<int>(model.bufferViews.size() - 1);
            
            // Update the primitive extension with the bufferView index
            auto& primitive = model.meshes[compData.meshIdx].primitives[compData.primIdx];
            if (primitive.extensions.count("KHR_draco_mesh_compression") > 0) {
                auto& ext = primitive.extensions["KHR_draco_mesh_compression"];
                if (ext.IsObject()) {
                    auto& obj = ext.Get<tinygltf::Value::Object>();
                    obj["bufferView"] = tinygltf::Value(bufferViewIdx);
                }
                
                // CRITICAL: Compute bounds BEFORE clearing bufferView references
                // The glTF spec requires min/max for POSITION accessors
                for (auto& attrPair : primitive.attributes) {
                    if (attrPair.first == "POSITION" && attrPair.second >= 0 && 
                        static_cast<size_t>(attrPair.second) < model.accessors.size()) {
                        auto& accessor = model.accessors[attrPair.second];
                        
                        // Only compute if not already set and we still have data
                        if (accessor.bufferView >= 0 && accessor.minValues.empty()) {
                            if (accessor.bufferView < static_cast<int>(model.bufferViews.size())) {
                                const auto& bufferView = model.bufferViews[accessor.bufferView];
                                if (bufferView.buffer >= 0 && bufferView.buffer < static_cast<int>(model.buffers.size())) {
                                    const auto& buffer = model.buffers[bufferView.buffer];
                                    const uint8_t* data = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
                                    
                                    // Calculate stride
                                    size_t stride = bufferView.byteStride > 0 ? bufferView.byteStride : (sizeof(float) * 3);
                                    
                                    // Compute min/max for VEC3 float positions
                                    if (accessor.type == TINYGLTF_TYPE_VEC3 && accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                                        accessor.minValues = {FLT_MAX, FLT_MAX, FLT_MAX};
                                        accessor.maxValues = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
                                        
                                        for (size_t i = 0; i < static_cast<size_t>(accessor.count); ++i) {
                                            const float* pos = reinterpret_cast<const float*>(data + i * stride);
                                            for (int j = 0; j < 3; ++j) {
                                                accessor.minValues[j] = std::min(accessor.minValues[j], static_cast<double>(pos[j]));
                                                accessor.maxValues[j] = std::max(accessor.maxValues[j], static_cast<double>(pos[j]));
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                
                // Now clear bufferView references from accessors so pruning removes old data
                // Since we added KHR_draco_mesh_compression to extensionsRequired, 
                // decoders MUST support it, so fallback data is unnecessary
                for (auto& attrPair : primitive.attributes) {
                    if (attrPair.second >= 0 && static_cast<size_t>(attrPair.second) < model.accessors.size()) {
                        model.accessors[attrPair.second].bufferView = -1;
                    }
                }
                if (primitive.indices >= 0 && static_cast<size_t>(primitive.indices) < model.accessors.size()) {
                    model.accessors[primitive.indices].bufferView = -1;
                }
            }
        }
    }
    
    // Generate statistics
    std::ostringstream statsStream;
    if (!compressedPrimitives.empty()) {
        double overallRatio = totalOriginalSize > 0 ? (double)totalCompressedSize / totalOriginalSize * 100.0 : 0.0;
        statsStream << "Compressed " << compressedPrimitives.size() << " primitives";
        if (skippedPrimitives > 0) {
            statsStream << " (skipped " << skippedPrimitives << ")";
        }
        statsStream << "\n"
                   << "Original size: " << totalOriginalSize << " bytes\n"
                   << "Compressed size: " << totalCompressedSize << " bytes\n"
                   << "Compression ratio: " << std::fixed << std::setprecision(1) << overallRatio << "%\n"
                   << "Space saved: " << (totalOriginalSize - totalCompressedSize) << " bytes";
        stats_ = statsStream.str();
    }
    
    return !compressedPrimitives.empty();
#endif
}

#ifdef GLTFU_ENABLE_DRACO
bool GltfCompress::compressPrimitive(tinygltf::Model& model,
                                     tinygltf::Mesh& mesh,
                                     size_t primitiveIndex,
                                     const CompressOptions& options,
                                     std::vector<uint8_t>& compressedData) {
    auto& primitive = mesh.primitives[primitiveIndex];
    
    // Only compress indexed triangle meshes
    if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
        return false;
    }
    
    if (primitive.indices < 0) {
        return false;
    }
    
    // Get number of vertices from POSITION attribute
    if (primitive.attributes.count("POSITION") == 0) {
        return false;
    }
    
    const auto& posAccessor = model.accessors[primitive.attributes["POSITION"]];
    const size_t numVertices = posAccessor.count;
    
    // Create Draco mesh
    std::unique_ptr<draco::Mesh> dracoMesh(new draco::Mesh());
    
    // Helper to get accessor buffer data with proper stride handling
    auto getAccessorInfo = [&](int accessorIdx, const uint8_t*& dataPtr, size_t& stride) -> bool {
        if (accessorIdx < 0 || static_cast<size_t>(accessorIdx) >= model.accessors.size()) return false;
        const auto& accessor = model.accessors[accessorIdx];
        if (accessor.bufferView < 0 || static_cast<size_t>(accessor.bufferView) >= model.bufferViews.size()) return false;
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        if (bufferView.buffer < 0 || static_cast<size_t>(bufferView.buffer) >= model.buffers.size()) return false;
        const auto& buffer = model.buffers[bufferView.buffer];
        
        dataPtr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
        
        // Calculate stride
        int componentSize = 1;
        if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_BYTE || 
            accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            componentSize = 1;
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_SHORT || 
                   accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            componentSize = 2;
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_INT || 
                   accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT || 
                   accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            componentSize = 4;
        }
        
        int numComponents = 1;
        if (accessor.type == TINYGLTF_TYPE_VEC2) numComponents = 2;
        else if (accessor.type == TINYGLTF_TYPE_VEC3) numComponents = 3;
        else if (accessor.type == TINYGLTF_TYPE_VEC4) numComponents = 4;
        else if (accessor.type == TINYGLTF_TYPE_MAT2) numComponents = 4;
        else if (accessor.type == TINYGLTF_TYPE_MAT3) numComponents = 9;
        else if (accessor.type == TINYGLTF_TYPE_MAT4) numComponents = 16;
        
        stride = bufferView.byteStride > 0 ? bufferView.byteStride : (componentSize * numComponents);
        return true;
    };
    
    // Add faces (indices)
    const auto& indexAccessor = model.accessors[primitive.indices];
    const uint8_t* indexData;
    size_t indexStride;
    if (!getAccessorInfo(primitive.indices, indexData, indexStride)) {
        return false;
    }
    
    const size_t numFaces = indexAccessor.count / 3;
    dracoMesh->SetNumFaces(numFaces);
    
    for (size_t i = 0; i < numFaces; ++i) {
        draco::Mesh::Face face;
        for (int j = 0; j < 3; ++j) {
            uint32_t idx = 0;
            const uint8_t* idxPtr = indexData + (i * 3 + j) * indexStride;
            
            if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                idx = *idxPtr;
            } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                idx = *reinterpret_cast<const uint16_t*>(idxPtr);
            } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                idx = *reinterpret_cast<const uint32_t*>(idxPtr);
            }
            face[j] = idx;
        }
        dracoMesh->SetFace(draco::FaceIndex(i), face);
    }
    
    dracoMesh->set_num_points(numVertices);
    
    // Map to store attribute IDs for extension
    std::map<std::string, int> attributeIdMap;
    
    // Check if we have morph targets - if so, use sequential encoding
    bool hasMorphTargets = !primitive.targets.empty();
    bool useSequential = !options.useEdgebreaker || hasMorphTargets;
    
    // Add attributes
    for (const auto& attr : primitive.attributes) {
        const std::string& attrName = attr.first;
        const int accessorIdx = attr.second;
        
        if (accessorIdx < 0 || static_cast<size_t>(accessorIdx) >= model.accessors.size()) continue;
        
        const auto& accessor = model.accessors[accessorIdx];
        const uint8_t* attrData;
        size_t attrStride;
        
        if (!getAccessorInfo(accessorIdx, attrData, attrStride)) continue;
        
        // Determine attribute type
        draco::GeometryAttribute::Type attrType = draco::GeometryAttribute::GENERIC;
        if (attrName == "POSITION") attrType = draco::GeometryAttribute::POSITION;
        else if (attrName == "NORMAL") attrType = draco::GeometryAttribute::NORMAL;
        else if (attrName.rfind("TEXCOORD_", 0) == 0) attrType = draco::GeometryAttribute::TEX_COORD;
        else if (attrName.rfind("COLOR_", 0) == 0) attrType = draco::GeometryAttribute::COLOR;
        else if (attrName == "TANGENT") attrType = draco::GeometryAttribute::GENERIC;
        else if (attrName.rfind("JOINTS_", 0) == 0) attrType = draco::GeometryAttribute::GENERIC;
        else if (attrName.rfind("WEIGHTS_", 0) == 0) attrType = draco::GeometryAttribute::GENERIC;
        
        // Determine data type
        draco::DataType dataType = draco::DT_FLOAT32;
        if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_BYTE) {
            dataType = draco::DT_INT8;
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            dataType = draco::DT_UINT8;
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_SHORT) {
            dataType = draco::DT_INT16;
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            dataType = draco::DT_UINT16;
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_INT) {
            dataType = draco::DT_INT32;
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            dataType = draco::DT_UINT32;
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            dataType = draco::DT_FLOAT32;
        }
        
        // Determine number of components
        int numComponents = 1;
        if (accessor.type == TINYGLTF_TYPE_VEC2) numComponents = 2;
        else if (accessor.type == TINYGLTF_TYPE_VEC3) numComponents = 3;
        else if (accessor.type == TINYGLTF_TYPE_VEC4) numComponents = 4;
        else if (accessor.type == TINYGLTF_TYPE_MAT2) numComponents = 4;
        else if (accessor.type == TINYGLTF_TYPE_MAT3) numComponents = 9;
        else if (accessor.type == TINYGLTF_TYPE_MAT4) numComponents = 16;
        
        // Create and add attribute
        draco::GeometryAttribute attribute;
        attribute.Init(attrType, nullptr, numComponents, dataType, accessor.normalized, 
                      draco::DataTypeLength(dataType) * numComponents, 0);
        
        int attId = dracoMesh->AddAttribute(attribute, true, numVertices);
        attributeIdMap[attrName] = attId;
        
        // Copy attribute data with proper stride handling
        size_t componentSize = draco::DataTypeLength(dataType) * numComponents;
        for (size_t v = 0; v < numVertices; ++v) {
            dracoMesh->attribute(attId)->SetAttributeValue(
                draco::AttributeValueIndex(v),
                attrData + v * attrStride
            );
        }
    }
    
    // Configure encoder
    draco::Encoder encoder;
    
    // Set quantization
    encoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION, options.positionQuantizationBits);
    encoder.SetAttributeQuantization(draco::GeometryAttribute::NORMAL, options.normalQuantizationBits);
    encoder.SetAttributeQuantization(draco::GeometryAttribute::TEX_COORD, options.texCoordQuantizationBits);
    encoder.SetAttributeQuantization(draco::GeometryAttribute::COLOR, options.colorQuantizationBits);
    encoder.SetAttributeQuantization(draco::GeometryAttribute::GENERIC, options.genericQuantizationBits);
    
    // Set speed options
    encoder.SetSpeedOptions(options.encodingSpeed, options.decodingSpeed);
    
    // Set encoding method (sequential for morph targets)
    if (useSequential) {
        encoder.SetEncodingMethod(draco::MESH_SEQUENTIAL_ENCODING);
    } else {
        encoder.SetEncodingMethod(draco::MESH_EDGEBREAKER_ENCODING);
    }
    
    // Encode
    draco::EncoderBuffer buffer;
    draco::Status status = encoder.EncodeMeshToBuffer(*dracoMesh, &buffer);
    
    if (!status.ok()) {
        if (options.verbose) {
            std::cerr << "Draco encoding failed: " << status.error_msg() << std::endl;
        }
        return false;
    }
    
    // Copy compressed data to output
    compressedData.resize(buffer.size());
    std::memcpy(compressedData.data(), buffer.data(), buffer.size());
    
    // Now we need to update the primitive to reference the compressed data
    // The buffer and bufferView will be created later when we consolidate
    // For now, store the offset where this data will be in the consolidated buffer
    
    // Add extension to primitive
    tinygltf::Value::Object dracoExt;
    
    // Add attribute mapping
    tinygltf::Value::Object attributes;
    for (const auto& pair : attributeIdMap) {
        attributes[pair.first] = tinygltf::Value(pair.second);
    }
    dracoExt["attributes"] = tinygltf::Value(attributes);
    
    primitive.extensions["KHR_draco_mesh_compression"] = tinygltf::Value(dracoExt);
    
    // CRITICAL: Remove references to uncompressed data
    // When Draco extension is present, it replaces the standard vertex data
    // The old accessors will be pruned later as unused
    // We must NOT clear the accessor references themselves - just leave them
    // The spec says: "When this extension is used, geometry data is stored in the Draco format
    // and the mesh primitive's attribute accessors and indices reference the Draco buffer."
    // However, the old accessors remain for fallback decoders, but we want to remove them
    // to save space since we made extension required.
    
    return true;
}
#endif

} // namespace gltfu
