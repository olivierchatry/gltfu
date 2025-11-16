#include "gltf_compress.h"
#include <iostream>
#include <sstream>
#include <iomanip>

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
    int compressedPrimitives = 0;
    
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
            if (!compressPrimitive(model, mesh, primIdx, options)) {
                // Continue with next primitive on error
                continue;
            }
            
            // Calculate compressed size
            if (primitive.extensions.count("KHR_draco_mesh_compression") > 0) {
                const auto& ext = primitive.extensions["KHR_draco_mesh_compression"];
                if (ext.Has("bufferView") && ext.Get("bufferView").IsInt()) {
                    int bvIdx = ext.Get("bufferView").Get<int>();
                    if (bvIdx >= 0 && static_cast<size_t>(bvIdx) < model.bufferViews.size()) {
                        size_t compressedSize = model.bufferViews[bvIdx].byteLength;
                        totalCompressedSize += compressedSize;
                        
                        if (options.verbose) {
                            double ratio = originalSize > 0 ? (double)compressedSize / originalSize * 100.0 : 0.0;
                            std::cout << "  Compressed primitive " << compressedPrimitives 
                                      << ": " << originalSize << " → " << compressedSize 
                                      << " bytes (" << std::fixed << std::setprecision(1) << ratio << "%)" 
                                      << std::endl;
                        }
                    }
                }
            }
            
            compressedPrimitives++;
        }
    }
    
    // Generate statistics
    std::ostringstream statsStream;
    if (compressedPrimitives > 0) {
        double overallRatio = totalOriginalSize > 0 ? (double)totalCompressedSize / totalOriginalSize * 100.0 : 0.0;
        statsStream << "Compressed " << compressedPrimitives << " primitives\n"
                   << "Original size: " << totalOriginalSize << " bytes\n"
                   << "Compressed size: " << totalCompressedSize << " bytes\n"
                   << "Compression ratio: " << std::fixed << std::setprecision(1) << overallRatio << "%\n"
                   << "Space saved: " << (totalOriginalSize - totalCompressedSize) << " bytes";
        stats_ = statsStream.str();
    }
    
    return compressedPrimitives > 0;
#endif
}

#ifdef GLTFU_ENABLE_DRACO
bool GltfCompress::compressPrimitive(tinygltf::Model& model,
                                     tinygltf::Mesh& mesh,
                                     size_t primitiveIndex,
                                     const CompressOptions& options) {
    auto& primitive = mesh.primitives[primitiveIndex];
    
    // Only compress triangle meshes
    if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
        return false;
    }
    
    // Create Draco mesh
    std::unique_ptr<draco::Mesh> dracoMesh(new draco::Mesh());
    
    // Helper to get accessor data
    auto getAccessorData = [&](int accessorIdx) -> const uint8_t* {
        if (accessorIdx < 0 || static_cast<size_t>(accessorIdx) >= model.accessors.size()) return nullptr;
        const auto& accessor = model.accessors[accessorIdx];
        if (accessor.bufferView < 0 || static_cast<size_t>(accessor.bufferView) >= model.bufferViews.size()) return nullptr;
        const auto& bufferView = model.bufferViews[accessor.bufferView];
        if (bufferView.buffer < 0 || static_cast<size_t>(bufferView.buffer) >= model.buffers.size()) return nullptr;
        const auto& buffer = model.buffers[bufferView.buffer];
        return buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    };
    
    // Get number of vertices from POSITION attribute
    if (primitive.attributes.count("POSITION") == 0) {
        return false;
    }
    
    const auto& posAccessor = model.accessors[primitive.attributes["POSITION"]];
    const size_t numVertices = posAccessor.count;
    
    // Add faces (indices)
    if (primitive.indices >= 0) {
        const auto& indexAccessor = model.accessors[primitive.indices];
        const uint8_t* indexData = getAccessorData(primitive.indices);
        if (!indexData) return false;
        
        const size_t numFaces = indexAccessor.count / 3;
        dracoMesh->SetNumFaces(numFaces);
        
        for (size_t i = 0; i < numFaces; ++i) {
            draco::Mesh::Face face;
            for (int j = 0; j < 3; ++j) {
                uint32_t idx = 0;
                if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    idx = reinterpret_cast<const uint16_t*>(indexData)[i * 3 + j];
                } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                    idx = reinterpret_cast<const uint32_t*>(indexData)[i * 3 + j];
                } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    idx = indexData[i * 3 + j];
                }
                face[j] = idx;
            }
            dracoMesh->SetFace(draco::FaceIndex(i), face);
        }
    }
    
    dracoMesh->set_num_points(numVertices);
    
    // Map to store attribute IDs for extension
    std::map<std::string, int> attributeIdMap;
    
    // Add attributes
    for (const auto& attr : primitive.attributes) {
        const std::string& attrName = attr.first;
        const int accessorIdx = attr.second;
        
        if (accessorIdx < 0 || static_cast<size_t>(accessorIdx) >= model.accessors.size()) continue;
        
        const auto& accessor = model.accessors[accessorIdx];
        const uint8_t* attrData = getAccessorData(accessorIdx);
        if (!attrData) continue;
        
        // Determine attribute type
        draco::GeometryAttribute::Type attrType = draco::GeometryAttribute::GENERIC;
        if (attrName == "POSITION") attrType = draco::GeometryAttribute::POSITION;
        else if (attrName == "NORMAL") attrType = draco::GeometryAttribute::NORMAL;
        else if (attrName == "TEXCOORD_0") attrType = draco::GeometryAttribute::TEX_COORD;
        else if (attrName == "COLOR_0") attrType = draco::GeometryAttribute::COLOR;
        else if (attrName == "TANGENT") attrType = draco::GeometryAttribute::GENERIC;
        
        // Determine data type
        draco::DataType dataType = draco::DT_FLOAT32;
        if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            dataType = draco::DT_FLOAT32;
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
            dataType = accessor.normalized ? draco::DT_UINT8 : draco::DT_UINT8;
        } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            dataType = accessor.normalized ? draco::DT_UINT16 : draco::DT_UINT16;
        }
        
        // Determine number of components
        int numComponents = 0;
        if (accessor.type == TINYGLTF_TYPE_SCALAR) numComponents = 1;
        else if (accessor.type == TINYGLTF_TYPE_VEC2) numComponents = 2;
        else if (accessor.type == TINYGLTF_TYPE_VEC3) numComponents = 3;
        else if (accessor.type == TINYGLTF_TYPE_VEC4) numComponents = 4;
        
        // Create and add attribute
        draco::GeometryAttribute attribute;
        attribute.Init(attrType, nullptr, numComponents, dataType, accessor.normalized, 
                      draco::DataTypeLength(dataType) * numComponents, 0);
        
        int attId = dracoMesh->AddAttribute(attribute, true, numVertices);
        attributeIdMap[attrName] = attId;
        
        // Copy attribute data
        size_t stride = draco::DataTypeLength(dataType) * numComponents;
        for (size_t v = 0; v < numVertices; ++v) {
            dracoMesh->attribute(attId)->SetAttributeValue(
                draco::AttributeValueIndex(v),
                attrData + v * stride
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
    
    // Set encoding method
    if (options.useEdgebreaker) {
        encoder.SetEncodingMethod(draco::MESH_EDGEBREAKER_ENCODING);
    } else {
        encoder.SetEncodingMethod(draco::MESH_SEQUENTIAL_ENCODING);
    }
    
    // Encode
    draco::EncoderBuffer buffer;
    draco::Status status = encoder.EncodeMeshToBuffer(*dracoMesh, &buffer);
    
    if (!status.ok()) {
        error_ = std::string("Draco encoding failed: ") + status.error_msg();
        return false;
    }
    
    // Create buffer for compressed data
    std::vector<uint8_t> compressedData(buffer.size());
    std::memcpy(compressedData.data(), buffer.data(), buffer.size());
    
    int bufferIdx = createCompressedBuffer(model, compressedData);
    int bufferViewIdx = createBufferView(model, bufferIdx, compressedData.size());
    
    // Add extension to primitive
    tinygltf::Value::Object dracoExt;
    dracoExt["bufferView"] = tinygltf::Value(bufferViewIdx);
    
    // Add attribute mapping
    tinygltf::Value::Object attributes;
    for (const auto& pair : attributeIdMap) {
        attributes[pair.first] = tinygltf::Value(pair.second);
    }
    dracoExt["attributes"] = tinygltf::Value(attributes);
    
    primitive.extensions["KHR_draco_mesh_compression"] = tinygltf::Value(dracoExt);
    
    return true;
}

int GltfCompress::createCompressedBuffer(tinygltf::Model& model, const std::vector<uint8_t>& data) {
    tinygltf::Buffer buffer;
    buffer.data = data;
    model.buffers.push_back(buffer);
    return static_cast<int>(model.buffers.size() - 1);
}

int GltfCompress::createBufferView(tinygltf::Model& model, int bufferIndex, size_t byteLength) {
    tinygltf::BufferView bufferView;
    bufferView.buffer = bufferIndex;
    bufferView.byteOffset = 0;
    bufferView.byteLength = byteLength;
    model.bufferViews.push_back(bufferView);
    return static_cast<int>(model.bufferViews.size() - 1);
}
#endif

} // namespace gltfu
