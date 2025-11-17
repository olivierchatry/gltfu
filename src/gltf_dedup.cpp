#include "gltf_dedup.h"

#include "progress_reporter.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gltfu {
namespace {

using DuplicateMap = std::unordered_map<int, int>;

struct DedupReport {
    size_t original = 0;
    size_t removed = 0;
    size_t remaining = 0;
};

class Reporter {
public:
    Reporter(const DedupOptions& options, const char* operation)
        : options_(options), operation_(operation) {}

    void log(const std::string& message,
             double progress = -1.0,
             const std::string& details = "") const {
        if (options_.progressReporter) {
            options_.progressReporter->report(operation_, message, progress, details);
            return;
        }

        if (!options_.verbose) {
            return;
        }

        std::cout << '[' << operation_ << "] " << message;
        if (progress >= 0.0) {
            std::cout << " (" << static_cast<int>(progress * 100.0) << "%)";
        }
        if (!details.empty()) {
            std::cout << " - " << details;
        }
        std::cout << std::endl;
    }

private:
    const DedupOptions& options_;
    const char* operation_;
};

uint64_t hashBuffer(const unsigned char* data, size_t size) {
    if (!data || size == 0) {
        return 0;
    }
    return XXH64(data, size, 0);
}

bool buffersEqual(const std::vector<unsigned char>& a,
                  const std::vector<unsigned char>& b) {
    return a.size() == b.size() &&
           (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
}

std::string accessorMetadata(const tinygltf::Accessor& accessor) {
    std::ostringstream stream;
    stream << accessor.count << ':'
           << accessor.type << ':'
           << accessor.componentType << ':'
           << accessor.normalized << ':'
           << accessor.bufferView << ':'
           << accessor.byteOffset << ':'
           << accessor.sparse.isSparse;
    return stream.str();
}

struct AccessorView {
    const unsigned char* data = nullptr;
    size_t stride = 0;
    size_t elementSize = 0;
};

bool resolveAccessorView(const tinygltf::Model& model,
                         const tinygltf::Accessor& accessor,
                         AccessorView& view) {
    if (accessor.bufferView < 0 ||
        accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        return false;
    }

    const auto& bufferView = model.bufferViews[accessor.bufferView];
    if (bufferView.buffer < 0 ||
        bufferView.buffer >= static_cast<int>(model.buffers.size())) {
        return false;
    }

    const auto& buffer = model.buffers[bufferView.buffer];
    if (buffer.data.empty()) {
        return false;
    }

    const size_t componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    const size_t componentCount = tinygltf::GetNumComponentsInType(accessor.type);
    if (componentSize == 0 || componentCount == 0) {
        return false;
    }

    view.elementSize = componentSize * componentCount;
    view.stride = bufferView.byteStride > 0 ? bufferView.byteStride : view.elementSize;

    const size_t offset = bufferView.byteOffset + accessor.byteOffset;
    const size_t required = offset + view.stride * (accessor.count ? accessor.count - 1 : 0) + view.elementSize;
    if (required > buffer.data.size()) {
        return false;
    }

    view.data = buffer.data.data() + offset;
    return true;
}

uint64_t accessorContentHash(const tinygltf::Model& model,
                             const tinygltf::Accessor& accessor) {
    AccessorView view;
    if (!resolveAccessorView(model, accessor, view)) {
        return 0;
    }

    if (view.stride == view.elementSize) {
        return hashBuffer(view.data, view.elementSize * accessor.count);
    }

    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, 0);
    for (size_t i = 0; i < accessor.count; ++i) {
        XXH64_update(state, view.data + i * view.stride, view.elementSize);
    }
    const uint64_t hash = XXH64_digest(state);
    XXH64_freeState(state);
    return hash;
}

bool accessorsEqual(const tinygltf::Model& model,
                    const tinygltf::Accessor& lhs,
                    const tinygltf::Accessor& rhs) {
    if (lhs.count != rhs.count) {
        return false;
    }

    AccessorView left;
    AccessorView right;
    if (!resolveAccessorView(model, lhs, left) ||
        !resolveAccessorView(model, rhs, right)) {
        return false;
    }

    if (left.elementSize != right.elementSize) {
        return false;
    }

    if (left.stride == right.stride && left.stride == left.elementSize) {
        return std::memcmp(left.data,
                           right.data,
                           lhs.count * left.elementSize) == 0;
    }

    for (size_t i = 0; i < lhs.count; ++i) {
        const unsigned char* a = left.data + i * left.stride;
        const unsigned char* b = right.data + i * right.stride;
        if (std::memcmp(a, b, left.elementSize) != 0) {
            return false;
        }
    }

    return true;
}

std::vector<int> buildRemap(size_t size, const DuplicateMap& duplicates) {
    std::vector<int> remap(size, -1);
    int next = 0;

    for (size_t i = 0; i < size; ++i) {
        if (!duplicates.count(static_cast<int>(i))) {
            remap[i] = next++;
        }
    }

    for (size_t i = 0; i < size; ++i) {
        auto it = duplicates.find(static_cast<int>(i));
        if (it != duplicates.end()) {
            remap[i] = remap[it->second];
        }
    }

    return remap;
}

template <typename T>
void compactVector(std::vector<T>& elements, const DuplicateMap& duplicates) {
    if (duplicates.empty()) {
        return;
    }

    std::vector<T> compacted;
    compacted.reserve(elements.size() - duplicates.size());
    for (size_t i = 0; i < elements.size(); ++i) {
        if (!duplicates.count(static_cast<int>(i))) {
            compacted.push_back(std::move(elements[i]));
        }
    }
    elements = std::move(compacted);
}

void applyAccessorRemap(tinygltf::Model& model, const std::vector<int>& remap) {
    auto update = [&](int& idx) {
        if (idx >= 0 && idx < static_cast<int>(remap.size())) {
            idx = remap[idx];
        }
    };

    for (auto& mesh : model.meshes) {
        for (auto& primitive : mesh.primitives) {
            for (auto& attribute : primitive.attributes) {
                update(attribute.second);
            }
            update(primitive.indices);
            for (auto& target : primitive.targets) {
                for (auto& entry : target) {
                    update(entry.second);
                }
            }
        }
    }

    for (auto& animation : model.animations) {
        for (auto& sampler : animation.samplers) {
            update(sampler.input);
            update(sampler.output);
        }
    }

    for (auto& skin : model.skins) {
        update(skin.inverseBindMatrices);
    }
}

std::string materialKey(const tinygltf::Material& material,
                        bool keepUniqueNames) {
    std::ostringstream stream;
    if (keepUniqueNames && !material.name.empty()) {
        stream << material.name << ';';
    }

    const auto& pbr = material.pbrMetallicRoughness;
    for (double value : pbr.baseColorFactor) {
        stream << value << ';';
    }
    stream << pbr.baseColorTexture.index << ';'
           << pbr.baseColorTexture.texCoord << ';';
    for (const auto& extension : pbr.baseColorTexture.extensions) {
        stream << "bc:" << extension.first << ';';
    }

    stream << pbr.metallicFactor << ';'
           << pbr.roughnessFactor << ';'
           << pbr.metallicRoughnessTexture.index << ';'
           << pbr.metallicRoughnessTexture.texCoord << ';';
    for (const auto& extension : pbr.metallicRoughnessTexture.extensions) {
        stream << "mr:" << extension.first << ';';
    }

    stream << material.normalTexture.index << ';'
           << material.normalTexture.texCoord << ';'
           << material.normalTexture.scale << ';';
    for (const auto& extension : material.normalTexture.extensions) {
        stream << "n:" << extension.first << ';';
    }

    stream << material.occlusionTexture.index << ';'
           << material.occlusionTexture.texCoord << ';'
           << material.occlusionTexture.strength << ';';
    for (const auto& extension : material.occlusionTexture.extensions) {
        stream << "o:" << extension.first << ';';
    }

    stream << material.emissiveTexture.index << ';'
           << material.emissiveTexture.texCoord << ';';
    for (const auto& extension : material.emissiveTexture.extensions) {
        stream << "e:" << extension.first << ';';
    }

    for (double value : material.emissiveFactor) {
        stream << value << ';';
    }

    stream << material.alphaMode << ';'
           << material.alphaCutoff << ';'
           << material.doubleSided << ';';

    for (const auto& extension : material.extensions) {
        stream << "mat:" << extension.first << ';';
    }

    if (material.extras.Type() != tinygltf::NULL_TYPE) {
        stream << "extras;";
    }

    return stream.str();
}

void applyMaterialRemap(tinygltf::Model& model, const std::vector<int>& remap) {
    auto update = [&](int& idx) {
        if (idx >= 0 && idx < static_cast<int>(remap.size())) {
            idx = remap[idx];
        }
    };

    for (auto& mesh : model.meshes) {
        for (auto& primitive : mesh.primitives) {
            update(primitive.material);
        }
    }
}

std::string meshKey(const tinygltf::Mesh& mesh, bool keepUniqueNames) {
    std::ostringstream stream;
    if (keepUniqueNames && !mesh.name.empty()) {
        stream << mesh.name << ';';
    }

    for (const auto& primitive : mesh.primitives) {
        stream << "mode:" << primitive.mode << ';'
               << "material:" << primitive.material << ';'
               << "indices:" << primitive.indices << ';';

        std::vector<std::pair<std::string, int>> attributes(primitive.attributes.begin(),
                                                            primitive.attributes.end());
        std::sort(attributes.begin(), attributes.end());
        for (const auto& attribute : attributes) {
            stream << attribute.first << ':' << attribute.second << ';';
        }

        stream << "targets{";
        for (const auto& target : primitive.targets) {
            std::vector<std::pair<std::string, int>> targetAttributes(target.begin(), target.end());
            std::sort(targetAttributes.begin(), targetAttributes.end());
            stream << '[';
            for (const auto& attribute : targetAttributes) {
                stream << attribute.first << ':' << attribute.second << ';';
            }
            stream << ']';
        }
        stream << "};";
    }

    return stream.str();
}

void applyMeshRemap(tinygltf::Model& model, const std::vector<int>& remap) {
    auto update = [&](int& idx) {
        if (idx >= 0 && idx < static_cast<int>(remap.size())) {
            idx = remap[idx];
        }
    };

    for (auto& node : model.nodes) {
        update(node.mesh);
    }
}

std::string imageKey(const tinygltf::Image& image, bool keepUniqueNames) {
    std::ostringstream stream;
    if (keepUniqueNames && !image.name.empty()) {
        stream << image.name << ';';
    }
    stream << image.mimeType << ';'
           << image.width << 'x' << image.height << ';'
           << image.component << ';'
           << image.bits << ';'
           << image.image.size();
    return stream.str();
}

std::string textureKey(const tinygltf::Texture& texture, bool keepUniqueNames) {
    std::ostringstream stream;
    if (keepUniqueNames && !texture.name.empty()) {
        stream << texture.name << ';';
    }
    stream << "source:" << texture.source << ';'
           << "sampler:" << texture.sampler;
    return stream.str();
}

void applyImageRemap(tinygltf::Model& model, const std::vector<int>& remap) {
    auto update = [&](int& idx) {
        if (idx >= 0 && idx < static_cast<int>(remap.size())) {
            idx = remap[idx];
        }
    };

    for (auto& texture : model.textures) {
        update(texture.source);
    }
}

void applyTextureRemap(tinygltf::Model& model, const std::vector<int>& remap) {
    auto update = [&](int& idx) {
        if (idx >= 0 && idx < static_cast<int>(remap.size())) {
            idx = remap[idx];
        }
    };

    auto updateTextureInfo = [&](tinygltf::TextureInfo& info) {
        update(info.index);
    };

    auto updateNormal = [&](tinygltf::NormalTextureInfo& info) {
        update(info.index);
    };

    auto updateOcclusion = [&](tinygltf::OcclusionTextureInfo& info) {
        update(info.index);
    };

    for (auto& material : model.materials) {
        updateTextureInfo(material.pbrMetallicRoughness.baseColorTexture);
        updateTextureInfo(material.pbrMetallicRoughness.metallicRoughnessTexture);
        updateNormal(material.normalTexture);
        updateOcclusion(material.occlusionTexture);
        updateTextureInfo(material.emissiveTexture);
    }
}

bool dedupAccessorsImpl(tinygltf::Model& model,
                        const DedupOptions& options,
                        DedupReport& report) {
    report.original = model.accessors.size();
    report.remaining = report.original;

    if (report.original < 2) {
        return false;
    }

    Reporter progress(options, "dedupe-accessors");
    progress.log("Scanning accessors", 0.0, std::to_string(report.original) + " total");

    struct BucketEntry {
        uint64_t hash = 0;
        int index = -1;
    };

    std::unordered_map<std::string, std::vector<BucketEntry>> buckets;
    DuplicateMap duplicates;

    for (size_t idx = 0; idx < model.accessors.size(); ++idx) {
        const auto& accessor = model.accessors[idx];
        const std::string metadata = accessorMetadata(accessor);
        const uint64_t contentHash = accessorContentHash(model, accessor);

        auto& bucket = buckets[metadata];
        bool matched = false;
        for (const auto& entry : bucket) {
            if (entry.hash != contentHash) {
                continue;
            }
            if (accessorsEqual(model, accessor, model.accessors[entry.index])) {
                duplicates[static_cast<int>(idx)] = entry.index;
                matched = true;
                break;
            }
        }

        if (!matched) {
            bucket.push_back({contentHash, static_cast<int>(idx)});
        }
    }

    report.removed = duplicates.size();
    if (duplicates.empty()) {
        progress.log("Accessors: no duplicates", 1.0, std::to_string(report.original) + " total");
        return false;
    }

    const auto remap = buildRemap(model.accessors.size(), duplicates);
    applyAccessorRemap(model, remap);
    compactVector(model.accessors, duplicates);
    report.remaining = model.accessors.size();

    progress.log("Accessors deduplicated", 1.0, std::to_string(report.removed) + " merged");
    return true;
}

bool dedupMaterialsImpl(tinygltf::Model& model,
                        const DedupOptions& options,
                        DedupReport& report) {
    report.original = model.materials.size();
    report.remaining = report.original;

    if (report.original < 2) {
        return false;
    }

    Reporter progress(options, "dedupe-materials");
    progress.log("Scanning materials", 0.0, std::to_string(report.original) + " total");

    std::unordered_map<std::string, int> seen;
    DuplicateMap duplicates;

    for (size_t idx = 0; idx < model.materials.size(); ++idx) {
        const auto& material = model.materials[idx];
        const std::string key = materialKey(material, options.keepUniqueNames);
        auto [it, inserted] = seen.emplace(key, static_cast<int>(idx));
        if (!inserted) {
            duplicates[static_cast<int>(idx)] = it->second;
        }
    }

    report.removed = duplicates.size();
    if (duplicates.empty()) {
        progress.log("Materials: no duplicates", 1.0, std::to_string(report.original) + " total");
        return false;
    }

    const auto remap = buildRemap(model.materials.size(), duplicates);
    applyMaterialRemap(model, remap);
    compactVector(model.materials, duplicates);
    report.remaining = model.materials.size();

    progress.log("Materials deduplicated", 1.0, std::to_string(report.removed) + " merged");
    return true;
}

bool dedupMeshesImpl(tinygltf::Model& model,
                     const DedupOptions& options,
                     DedupReport& report) {
    report.original = model.meshes.size();
    report.remaining = report.original;

    if (report.original < 2) {
        return false;
    }

    Reporter progress(options, "dedupe-meshes");
    progress.log("Scanning meshes", 0.0, std::to_string(report.original) + " total");

    std::unordered_map<std::string, int> seen;
    DuplicateMap duplicates;

    for (size_t idx = 0; idx < model.meshes.size(); ++idx) {
        const std::string key = meshKey(model.meshes[idx], options.keepUniqueNames);
        auto [it, inserted] = seen.emplace(key, static_cast<int>(idx));
        if (!inserted) {
            duplicates[static_cast<int>(idx)] = it->second;
        }
    }

    report.removed = duplicates.size();
    if (duplicates.empty()) {
        progress.log("Meshes: no duplicates", 1.0, std::to_string(report.original) + " total");
        return false;
    }

    const auto remap = buildRemap(model.meshes.size(), duplicates);
    applyMeshRemap(model, remap);
    compactVector(model.meshes, duplicates);
    report.remaining = model.meshes.size();

    progress.log("Meshes deduplicated", 1.0, std::to_string(report.removed) + " merged");
    return true;
}

bool dedupTexturesImpl(tinygltf::Model& model,
                       const DedupOptions& options,
                       DedupReport& imageReport,
                       DedupReport& textureReport) {
    imageReport.original = model.images.size();
    imageReport.remaining = imageReport.original;
    textureReport.original = model.textures.size();
    textureReport.remaining = textureReport.original;

    bool changed = false;

    Reporter imageProgress(options, "dedupe-images");
    if (imageReport.original > 1) {
        imageProgress.log("Scanning images", 0.0, std::to_string(imageReport.original) + " total");

        struct BucketEntry {
            uint64_t hash = 0;
            int index = -1;
        };

        std::unordered_map<std::string, std::vector<BucketEntry>> buckets;
        DuplicateMap duplicates;

        for (size_t idx = 0; idx < model.images.size(); ++idx) {
            const auto& image = model.images[idx];
            const std::string key = imageKey(image, options.keepUniqueNames);
            const uint64_t contentHash = hashBuffer(image.image.data(), image.image.size());

            auto& bucket = buckets[key];
            bool matched = false;
            for (const auto& entry : bucket) {
                if (entry.hash != contentHash) {
                    continue;
                }
                if (buffersEqual(image.image, model.images[entry.index].image)) {
                    duplicates[static_cast<int>(idx)] = entry.index;
                    matched = true;
                    break;
                }
            }

            if (!matched) {
                bucket.push_back({contentHash, static_cast<int>(idx)});
            }
        }

        imageReport.removed = duplicates.size();
        if (!duplicates.empty()) {
            const auto remap = buildRemap(model.images.size(), duplicates);
            applyImageRemap(model, remap);
            compactVector(model.images, duplicates);
            imageReport.remaining = model.images.size();
            changed = true;
        } else {
            imageProgress.log("Images: no duplicates", 1.0, std::to_string(imageReport.original) + " total");
        }
    }

    Reporter textureProgress(options, "dedupe-textures");
    if (model.textures.size() > 1) {
        textureProgress.log("Scanning textures", 0.0, std::to_string(textureReport.original) + " total");

        std::unordered_map<std::string, int> seen;
        DuplicateMap duplicates;

        for (size_t idx = 0; idx < model.textures.size(); ++idx) {
            const std::string key = textureKey(model.textures[idx], options.keepUniqueNames);
            auto [it, inserted] = seen.emplace(key, static_cast<int>(idx));
            if (!inserted) {
                duplicates[static_cast<int>(idx)] = it->second;
            }
        }

        textureReport.removed = duplicates.size();
        if (!duplicates.empty()) {
            const auto remap = buildRemap(model.textures.size(), duplicates);
            applyTextureRemap(model, remap);
            compactVector(model.textures, duplicates);
            textureReport.remaining = model.textures.size();
            changed = true;
        } else {
            textureProgress.log("Textures: no duplicates", 1.0, std::to_string(textureReport.original) + " total");
        }
    }

    return changed;
}

std::string formatSummary(const char* label, const DedupReport& report) {
    std::ostringstream stream;
    stream << label << ": Merged " << report.removed << " of " << report.original
           << " (" << report.remaining << " remaining)";
    return stream.str();
}

} // namespace

GltfDedup::GltfDedup() = default;
GltfDedup::~GltfDedup() = default;

bool GltfDedup::process(tinygltf::Model& model, const DedupOptions& options) {
    error_.clear();
    stats_.clear();

    try {
        if (options.dedupAccessors) {
            DedupReport accessors;
            if (dedupAccessorsImpl(model, options, accessors)) {
                stats_ += formatSummary("Accessors", accessors) + '\n';
            }
        }

        if (options.dedupTextures) {
            DedupReport images;
            DedupReport textures;
            if (dedupTexturesImpl(model, options, images, textures)) {
                if (images.removed > 0) {
                    stats_ += formatSummary("Images", images) + '\n';
                }
                if (textures.removed > 0) {
                    stats_ += formatSummary("Textures", textures) + '\n';
                }
            }
        }

        if (options.dedupMaterials) {
            DedupReport materials;
            if (dedupMaterialsImpl(model, options, materials)) {
                stats_ += formatSummary("Materials", materials) + '\n';
            }
        }

        if (options.dedupMeshes) {
            DedupReport meshes;
            if (dedupMeshesImpl(model, options, meshes)) {
                stats_ += formatSummary("Meshes", meshes) + '\n';
            }
        }

        return true;
    } catch (const std::exception& ex) {
        error_ = std::string("Deduplication failed: ") + ex.what();
        return false;
    }
}

} // namespace gltfu
