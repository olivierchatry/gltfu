#include "gltf_compress.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <vector>

#ifdef GLTFU_ENABLE_DRACO
#include "draco/compression/encode.h"
#include "draco/core/encoder_buffer.h"
#include "draco/mesh/mesh.h"
#endif

namespace gltfu {
namespace {

constexpr const char* kDracoExtension = "KHR_draco_mesh_compression";

bool containsExtension(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

void addExtension(std::vector<std::string>& list, const std::string& value) {
    if (!containsExtension(list, value)) {
        list.push_back(value);
    }
}

int componentCount(int type) {
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

size_t accessorStride(const tinygltf::Accessor& accessor, const tinygltf::BufferView& view) {
    if (view.byteStride > 0) {
        return view.byteStride;
    }
    return componentCount(accessor.type) * componentSize(accessor.componentType);
}

size_t accessorByteLength(const tinygltf::Model& model, int accessorIdx) {
    if (accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors.size())) {
        return 0;
    }
    const auto& accessor = model.accessors[accessorIdx];
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        return 0;
    }
    const auto& bufferView = model.bufferViews[accessor.bufferView];
    return bufferView.byteLength;
}

struct PrimitiveRecord {
    size_t meshIdx;
    size_t primIdx;
    size_t offset;
    size_t length;
    size_t original;
};

void ensurePositionBounds(tinygltf::Model& model, tinygltf::Primitive& primitive) {
    auto positionIt = primitive.attributes.find("POSITION");
    if (positionIt == primitive.attributes.end()) {
        return;
    }

    const int accessorIdx = positionIt->second;
    if (accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors.size())) {
        return;
    }

    auto& accessor = model.accessors[accessorIdx];
    if (accessor.count == 0 || !accessor.minValues.empty()) {
        return;
    }
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        return;
    }

    const auto& bufferView = model.bufferViews[accessor.bufferView];
    if (bufferView.buffer < 0 || bufferView.buffer >= static_cast<int>(model.buffers.size())) {
        return;
    }

    const auto& buffer = model.buffers[bufferView.buffer];
    const size_t stride = accessorStride(accessor, bufferView);
    const size_t required = bufferView.byteOffset + accessor.byteOffset + stride * (accessor.count - 1) + sizeof(float) * 3;
    if (required > buffer.data.size()) {
        return;
    }

    if (accessor.type != TINYGLTF_TYPE_VEC3 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        return;
    }

    accessor.minValues = {std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::infinity()};
    accessor.maxValues = {-std::numeric_limits<double>::infinity(),
                          -std::numeric_limits<double>::infinity(),
                          -std::numeric_limits<double>::infinity()};

    const uint8_t* base = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    for (size_t i = 0; i < static_cast<size_t>(accessor.count); ++i) {
        const float* pos = reinterpret_cast<const float*>(base + i * stride);
        accessor.minValues[0] = std::min(accessor.minValues[0], static_cast<double>(pos[0]));
        accessor.minValues[1] = std::min(accessor.minValues[1], static_cast<double>(pos[1]));
        accessor.minValues[2] = std::min(accessor.minValues[2], static_cast<double>(pos[2]));
        accessor.maxValues[0] = std::max(accessor.maxValues[0], static_cast<double>(pos[0]));
        accessor.maxValues[1] = std::max(accessor.maxValues[1], static_cast<double>(pos[1]));
        accessor.maxValues[2] = std::max(accessor.maxValues[2], static_cast<double>(pos[2]));
    }
}

#ifdef GLTFU_ENABLE_DRACO

struct AccessorInfo {
    const uint8_t* data = nullptr;
    size_t stride = 0;
};

bool fetchAccessorInfo(const tinygltf::Model& model,
                       int accessorIdx,
                       AccessorInfo& info) {
    if (accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors.size())) {
        return false;
    }
    const auto& accessor = model.accessors[accessorIdx];
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        return false;
    }
    const auto& bufferView = model.bufferViews[accessor.bufferView];
    if (bufferView.buffer < 0 || bufferView.buffer >= static_cast<int>(model.buffers.size())) {
        return false;
    }

    const auto& buffer = model.buffers[bufferView.buffer];
    if (buffer.data.empty()) {
        return false;
    }

    info.data = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    info.stride = accessorStride(accessor, bufferView);
    return true;
}

bool compressPrimitive(tinygltf::Model& model,
                       tinygltf::Mesh& mesh,
                       size_t primitiveIndex,
                       const CompressOptions& options,
                       std::vector<uint8_t>& compressedData) {
    auto& primitive = mesh.primitives[primitiveIndex];

    if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
        return false;
    }
    if (primitive.indices < 0 || primitive.attributes.count("POSITION") == 0) {
        return false;
    }

    const auto& positionAccessor = model.accessors[primitive.attributes["POSITION"]];
    const size_t vertexCount = positionAccessor.count;
    if (vertexCount == 0) {
        return false;
    }

    AccessorInfo indexInfo;
    if (!fetchAccessorInfo(model, primitive.indices, indexInfo)) {
        return false;
    }

    auto dracoMesh = std::make_unique<draco::Mesh>();
    const auto& indexAccessor = model.accessors[primitive.indices];
    const size_t faceCount = indexAccessor.count / 3;
    dracoMesh->SetNumFaces(faceCount);
    dracoMesh->set_num_points(vertexCount);

    for (size_t face = 0; face < faceCount; ++face) {
        draco::Mesh::Face dracoFace;
        for (int corner = 0; corner < 3; ++corner) {
            const uint8_t* ptr = indexInfo.data + (face * 3 + corner) * indexInfo.stride;
            uint32_t idx = 0;
            switch (indexAccessor.componentType) {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    idx = *ptr;
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    idx = *reinterpret_cast<const uint16_t*>(ptr);
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    idx = *reinterpret_cast<const uint32_t*>(ptr);
                    break;
                default:
                    return false;
            }
            dracoFace[corner] = idx;
        }
        dracoMesh->SetFace(draco::FaceIndex(face), dracoFace);
    }

    std::map<std::string, int> attributeIds;
    const bool hasMorphTargets = !primitive.targets.empty();
    const bool useSequential = !options.useEdgebreaker || hasMorphTargets;

    for (const auto& attributePair : primitive.attributes) {
        const std::string& name = attributePair.first;
        const int accessorIdx = attributePair.second;
        if (accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors.size())) {
            continue;
        }

        const auto& accessor = model.accessors[accessorIdx];
        AccessorInfo info;
        if (!fetchAccessorInfo(model, accessorIdx, info)) {
            continue;
        }

        draco::GeometryAttribute::Type attrType = draco::GeometryAttribute::GENERIC;
        if (name == "POSITION") {
            attrType = draco::GeometryAttribute::POSITION;
        } else if (name == "NORMAL") {
            attrType = draco::GeometryAttribute::NORMAL;
        } else if (name.rfind("TEXCOORD_", 0) == 0) {
            attrType = draco::GeometryAttribute::TEX_COORD;
        } else if (name.rfind("COLOR_", 0) == 0) {
            attrType = draco::GeometryAttribute::COLOR;
        }

        draco::DataType dataType = draco::DT_FLOAT32;
        switch (accessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_BYTE: dataType = draco::DT_INT8; break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: dataType = draco::DT_UINT8; break;
            case TINYGLTF_COMPONENT_TYPE_SHORT: dataType = draco::DT_INT16; break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: dataType = draco::DT_UINT16; break;
            case TINYGLTF_COMPONENT_TYPE_INT: dataType = draco::DT_INT32; break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: dataType = draco::DT_UINT32; break;
            case TINYGLTF_COMPONENT_TYPE_FLOAT: dataType = draco::DT_FLOAT32; break;
            default: break;
        }

        const int components = componentCount(accessor.type);
        draco::GeometryAttribute attribute;
        attribute.Init(attrType, nullptr, components, dataType, accessor.normalized,
                       draco::DataTypeLength(dataType) * components, 0);

        const int attributeId = dracoMesh->AddAttribute(attribute, true, vertexCount);
        attributeIds[name] = attributeId;

        for (size_t vertex = 0; vertex < vertexCount; ++vertex) {
            dracoMesh->attribute(attributeId)->SetAttributeValue(
                draco::AttributeValueIndex(vertex),
                info.data + vertex * info.stride);
        }
    }

    draco::Encoder encoder;
    encoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION, options.positionQuantizationBits);
    encoder.SetAttributeQuantization(draco::GeometryAttribute::NORMAL, options.normalQuantizationBits);
    encoder.SetAttributeQuantization(draco::GeometryAttribute::TEX_COORD, options.texCoordQuantizationBits);
    encoder.SetAttributeQuantization(draco::GeometryAttribute::COLOR, options.colorQuantizationBits);
    encoder.SetAttributeQuantization(draco::GeometryAttribute::GENERIC, options.genericQuantizationBits);
    encoder.SetSpeedOptions(options.encodingSpeed, options.decodingSpeed);
    encoder.SetEncodingMethod(useSequential ? draco::MESH_SEQUENTIAL_ENCODING
                                            : draco::MESH_EDGEBREAKER_ENCODING);

    draco::EncoderBuffer buffer;
    const draco::Status status = encoder.EncodeMeshToBuffer(*dracoMesh, &buffer);
    if (!status.ok()) {
        return false;
    }

    compressedData.resize(buffer.size());
    std::memcpy(compressedData.data(), buffer.data(), buffer.size());

    tinygltf::Value::Object dracoObject;
    tinygltf::Value::Object attributeMap;
    for (const auto& entry : attributeIds) {
        attributeMap[entry.first] = tinygltf::Value(entry.second);
    }
    dracoObject["attributes"] = tinygltf::Value(attributeMap);
    primitive.extensions[kDracoExtension] = tinygltf::Value(dracoObject);

    return true;
}
#endif // GLTFU_ENABLE_DRACO

} // namespace

bool GltfCompress::process(tinygltf::Model& model, const CompressOptions& options) {
#ifndef GLTFU_ENABLE_DRACO
    error_ = "Draco compression is not enabled. Rebuild with Draco support.";
    stats_.clear();
    return false;
#else
    error_.clear();
    stats_.clear();

    addExtension(model.extensionsUsed, kDracoExtension);
    addExtension(model.extensionsRequired, kDracoExtension);

    std::vector<uint8_t> compressedBufferData;
    std::vector<PrimitiveRecord> records;
    records.reserve(model.meshes.size());

    size_t totalOriginal = 0;
    size_t totalCompressed = 0;
    int skipped = 0;

    for (size_t meshIdx = 0; meshIdx < model.meshes.size(); ++meshIdx) {
        auto& mesh = model.meshes[meshIdx];
        for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
            auto& primitive = mesh.primitives[primIdx];

            size_t original = 0;
            for (const auto& attribute : primitive.attributes) {
                original += accessorByteLength(model, attribute.second);
            }
            original += accessorByteLength(model, primitive.indices);

#ifdef GLTFU_ENABLE_DRACO
            std::vector<uint8_t> compressed;
            if (!compressPrimitive(model, mesh, primIdx, options, compressed)) {
                ++skipped;
                continue;
            }
#endif

            const size_t offset = compressedBufferData.size();
            compressedBufferData.insert(compressedBufferData.end(),
                                        compressed.begin(), compressed.end());

            records.push_back({meshIdx, primIdx, offset, compressed.size(), original});
            totalOriginal += original;
            totalCompressed += compressed.size();

            if (options.verbose) {
                const double ratio = original ? (static_cast<double>(compressed.size()) / original) * 100.0 : 0.0;
                std::cout << "  Compressed primitive " << meshIdx << ':' << primIdx
                          << " " << original << " → " << compressed.size()
                          << " bytes (" << std::fixed << std::setprecision(1) << ratio << "%)"
                          << std::endl;
            }
        }
    }

    if (records.empty()) {
        if (skipped > 0) {
            std::ostringstream stream;
            stream << "Skipped " << skipped << " primitives (not suitable for Draco).";
            stats_ = stream.str();
        }
        return false;
    }

    tinygltf::Buffer buffer;
    buffer.data = std::move(compressedBufferData);
    model.buffers.push_back(std::move(buffer));
    const int bufferIdx = static_cast<int>(model.buffers.size() - 1);

    for (const auto& record : records) {
        tinygltf::BufferView view;
        view.buffer = bufferIdx;
        view.byteOffset = record.offset;
        view.byteLength = record.length;
        model.bufferViews.push_back(std::move(view));
        const int viewIdx = static_cast<int>(model.bufferViews.size() - 1);

        auto& primitive = model.meshes[record.meshIdx].primitives[record.primIdx];
        auto& extension = primitive.extensions[kDracoExtension];
        if (extension.IsObject()) {
            extension.Get<tinygltf::Value::Object>()["bufferView"] = tinygltf::Value(viewIdx);
        }

        ensurePositionBounds(model, primitive);

        for (const auto& attribute : primitive.attributes) {
            if (attribute.second >= 0 && attribute.second < static_cast<int>(model.accessors.size())) {
                model.accessors[attribute.second].bufferView = -1;
            }
        }
        if (primitive.indices >= 0 && primitive.indices < static_cast<int>(model.accessors.size())) {
            model.accessors[primitive.indices].bufferView = -1;
        }
    }

    std::ostringstream summary;
    summary << "Compressed " << records.size() << " primitives";
    if (skipped > 0) {
        summary << " (skipped " << skipped << ")";
    }
    summary << "\nOriginal size: " << totalOriginal << " bytes";
    summary << "\nCompressed size: " << totalCompressed << " bytes";
    const size_t saved = totalOriginal > totalCompressed ? totalOriginal - totalCompressed : 0;
    const double ratio = totalOriginal ? (static_cast<double>(totalCompressed) / totalOriginal) * 100.0 : 0.0;
    summary << "\nCompression ratio: " << std::fixed << std::setprecision(1) << ratio << '%';
    summary << "\nSpace saved: " << saved << " bytes";
    stats_ = summary.str();

    return true;
#endif
}

} // namespace gltfu
