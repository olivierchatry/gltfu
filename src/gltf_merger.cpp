#include "gltf_merger.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <iterator>
#include <numeric>
#include <vector>

namespace gltfu {
namespace {

bool hasGlbExtension(const std::string& filename) {
    auto dot = filename.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }

    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == "glb";
}

struct MergeOffsets {
    int nodes = 0;
    int meshes = 0;
    int materials = 0;
    int textures = 0;
    int images = 0;
    int samplers = 0;
    int accessors = 0;
    int bufferViews = 0;
    int skins = 0;
    int cameras = 0;
};

struct MergeCounts {
    size_t nodes = 0;
    size_t meshes = 0;
    size_t materials = 0;
    size_t textures = 0;
    size_t images = 0;
    size_t samplers = 0;
    size_t accessors = 0;
    size_t bufferViews = 0;
    size_t animations = 0;
    size_t skins = 0;
};

void adjustNodes(tinygltf::Model& model, const MergeOffsets& offsets, const MergeCounts& counts) {
    if (counts.nodes == 0) {
        return;
    }

    const size_t start = model.nodes.size() - counts.nodes;
    for (size_t idx = start; idx < model.nodes.size(); ++idx) {
        auto& node = model.nodes[idx];
        for (int& child : node.children) {
            child += offsets.nodes;
        }
        if (node.mesh >= 0) {
            node.mesh += offsets.meshes;
        }
        if (node.skin >= 0) {
            node.skin += offsets.skins;
        }
        if (node.camera >= 0) {
            node.camera += offsets.cameras;
        }
    }
}

void adjustMeshes(tinygltf::Model& model, const MergeOffsets& offsets, const MergeCounts& counts) {
    if (counts.meshes == 0) {
        return;
    }

    const size_t start = model.meshes.size() - counts.meshes;
    for (size_t idx = start; idx < model.meshes.size(); ++idx) {
        auto& mesh = model.meshes[idx];
        for (auto& primitive : mesh.primitives) {
            if (primitive.material >= 0) {
                primitive.material += offsets.materials;
            }
            for (auto& attribute : primitive.attributes) {
                attribute.second += offsets.accessors;
            }
            if (primitive.indices >= 0) {
                primitive.indices += offsets.accessors;
            }
            for (auto& target : primitive.targets) {
                for (auto& attribute : target) {
                    attribute.second += offsets.accessors;
                }
            }
        }
    }
}

void adjustMaterials(tinygltf::Model& model, const MergeOffsets& offsets, const MergeCounts& counts) {
    if (counts.materials == 0) {
        return;
    }

    const size_t start = model.materials.size() - counts.materials;
    for (size_t idx = start; idx < model.materials.size(); ++idx) {
        auto& material = model.materials[idx];
        if (material.pbrMetallicRoughness.baseColorTexture.index >= 0) {
            material.pbrMetallicRoughness.baseColorTexture.index += offsets.textures;
        }
        if (material.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
            material.pbrMetallicRoughness.metallicRoughnessTexture.index += offsets.textures;
        }
        if (material.normalTexture.index >= 0) {
            material.normalTexture.index += offsets.textures;
        }
        if (material.occlusionTexture.index >= 0) {
            material.occlusionTexture.index += offsets.textures;
        }
        if (material.emissiveTexture.index >= 0) {
            material.emissiveTexture.index += offsets.textures;
        }
    }
}

void adjustTextures(tinygltf::Model& model, const MergeOffsets& offsets, const MergeCounts& counts) {
    if (counts.textures == 0) {
        return;
    }

    const size_t start = model.textures.size() - counts.textures;
    for (size_t idx = start; idx < model.textures.size(); ++idx) {
        auto& texture = model.textures[idx];
        if (texture.source >= 0) {
            texture.source += offsets.images;
        }
        if (texture.sampler >= 0) {
            texture.sampler += offsets.samplers;
        }
    }
}

void adjustImages(tinygltf::Model& model, const MergeOffsets& offsets, const MergeCounts& counts) {
    if (counts.images == 0) {
        return;
    }

    const size_t start = model.images.size() - counts.images;
    for (size_t idx = start; idx < model.images.size(); ++idx) {
        auto& image = model.images[idx];
        if (image.bufferView >= 0) {
            image.bufferView += offsets.bufferViews;
        }
    }
}

void adjustAccessors(tinygltf::Model& model, const MergeOffsets& offsets, const MergeCounts& counts) {
    if (counts.accessors == 0) {
        return;
    }

    const size_t start = model.accessors.size() - counts.accessors;
    for (size_t idx = start; idx < model.accessors.size(); ++idx) {
        auto& accessor = model.accessors[idx];
        if (accessor.bufferView >= 0) {
            accessor.bufferView += offsets.bufferViews;
        }
    }
}

void adjustBufferViews(tinygltf::Model& model, const MergeCounts& counts) {
    if (counts.bufferViews == 0) {
        return;
    }

    const size_t start = model.bufferViews.size() - counts.bufferViews;
    for (size_t idx = start; idx < model.bufferViews.size(); ++idx) {
        model.bufferViews[idx].buffer = 0;
    }
}

void adjustAnimations(tinygltf::Model& model, const MergeOffsets& offsets, const MergeCounts& counts) {
    if (counts.animations == 0) {
        return;
    }

    const size_t start = model.animations.size() - counts.animations;
    for (size_t idx = start; idx < model.animations.size(); ++idx) {
        auto& animation = model.animations[idx];
        for (auto& sampler : animation.samplers) {
            if (sampler.input >= 0) {
                sampler.input += offsets.accessors;
            }
            if (sampler.output >= 0) {
                sampler.output += offsets.accessors;
            }
        }
        for (auto& channel : animation.channels) {
            if (channel.target_node >= 0) {
                channel.target_node += offsets.nodes;
            }
        }
    }
}

void adjustSkins(tinygltf::Model& model, const MergeOffsets& offsets, const MergeCounts& counts) {
    if (counts.skins == 0) {
        return;
    }

    const size_t start = model.skins.size() - counts.skins;
    for (size_t idx = start; idx < model.skins.size(); ++idx) {
        auto& skin = model.skins[idx];
        if (skin.inverseBindMatrices >= 0) {
            skin.inverseBindMatrices += offsets.accessors;
        }
        if (skin.skeleton >= 0) {
            skin.skeleton += offsets.nodes;
        }
        for (int& joint : skin.joints) {
            joint += offsets.nodes;
        }
    }
}

void applyOffsets(tinygltf::Model& model, const MergeOffsets& offsets, const MergeCounts& counts) {
    adjustNodes(model, offsets, counts);
    adjustMeshes(model, offsets, counts);
    adjustMaterials(model, offsets, counts);
    adjustTextures(model, offsets, counts);
    adjustImages(model, offsets, counts);
    adjustAccessors(model, offsets, counts);
    adjustBufferViews(model, counts);
    adjustAnimations(model, offsets, counts);
    adjustSkins(model, offsets, counts);
}

std::vector<size_t> computeBufferOffsets(const std::vector<tinygltf::Buffer>& buffers) {
    std::vector<size_t> offsets;
    offsets.reserve(buffers.size());
    size_t running = 0;
    for (const auto& buffer : buffers) {
        offsets.push_back(running);
        running += buffer.data.size();
    }
    return offsets;
}

} // namespace

GltfMerger::GltfMerger() = default;

GltfMerger::~GltfMerger() = default;

bool GltfMerger::loadAndMergeFile(const std::string& filename,
                                  bool keepScenesIndependent,
                                  bool defaultScenesOnly) {
    tinygltf::Model model;
    std::string err;
    std::string warn;

    const bool isGlb = hasGlbExtension(filename);
    bool ok = false;
    if (isGlb) {
        ok = loader_.LoadBinaryFromFile(&model, &err, &warn, filename);
    } else {
        ok = loader_.LoadASCIIFromFile(&model, &err, &warn, filename);
    }

    if (!warn.empty()) {
        std::cerr << "Warning loading " << filename << ": " << warn << std::endl;
    }

    if (!ok || !err.empty()) {
        errorMsg_ = err.empty() ? ("Failed to load " + filename)
                                : ("Error loading " + filename + ": " + err);
        return false;
    }

    for (auto& buffer : model.buffers) {
        buffer.uri.clear();
    }

    return mergeModelStreaming(std::move(model), keepScenesIndependent, defaultScenesOnly);
}

bool GltfMerger::mergeModelStreaming(tinygltf::Model&& model,
                                     bool keepScenesIndependent,
                                     bool defaultScenesOnly) {
    if (firstModel_) {
        mergedModel_.asset = model.asset;
        mergedModel_.extensionsUsed = model.extensionsUsed;
        mergedModel_.extensionsRequired = model.extensionsRequired;
        mergedModel_.buffers.emplace_back();
        mergedModel_.buffers.back().name = "merged_buffer";
        firstModel_ = false;
    } else {
        mergedModel_.extensionsUsed.insert(mergedModel_.extensionsUsed.end(),
                                           model.extensionsUsed.begin(),
                                           model.extensionsUsed.end());
        mergedModel_.extensionsRequired.insert(mergedModel_.extensionsRequired.end(),
                                               model.extensionsRequired.begin(),
                                               model.extensionsRequired.end());
    }

    MergeOffsets offsets;
    offsets.nodes = static_cast<int>(mergedModel_.nodes.size());
    offsets.meshes = static_cast<int>(mergedModel_.meshes.size());
    offsets.materials = static_cast<int>(mergedModel_.materials.size());
    offsets.textures = static_cast<int>(mergedModel_.textures.size());
    offsets.images = static_cast<int>(mergedModel_.images.size());
    offsets.samplers = static_cast<int>(mergedModel_.samplers.size());
    offsets.accessors = static_cast<int>(mergedModel_.accessors.size());
    offsets.bufferViews = static_cast<int>(mergedModel_.bufferViews.size());
    offsets.skins = static_cast<int>(mergedModel_.skins.size());
    offsets.cameras = static_cast<int>(mergedModel_.cameras.size());

    MergeCounts counts;
    counts.nodes = model.nodes.size();
    counts.meshes = model.meshes.size();
    counts.materials = model.materials.size();
    counts.textures = model.textures.size();
    counts.images = model.images.size();
    counts.samplers = model.samplers.size();
    counts.accessors = model.accessors.size();
    counts.bufferViews = model.bufferViews.size();
    counts.animations = model.animations.size();
    counts.skins = model.skins.size();

    const size_t currentBufferSize = mergedModel_.buffers[0].data.size();
    const auto bufferOffsets = computeBufferOffsets(model.buffers);
    size_t appendedBytes = bufferOffsets.empty()
                               ? 0
                               : bufferOffsets.back() + model.buffers.back().data.size();
    mergedModel_.buffers[0].data.reserve(currentBufferSize + appendedBytes);
    for (auto& buffer : model.buffers) {
        mergedModel_.buffers[0].data.insert(mergedModel_.buffers[0].data.end(),
                                            std::make_move_iterator(buffer.data.begin()),
                                            std::make_move_iterator(buffer.data.end()));
    }

    mergedModel_.bufferViews.reserve(mergedModel_.bufferViews.size() + model.bufferViews.size());
    mergedModel_.accessors.reserve(mergedModel_.accessors.size() + model.accessors.size());
    mergedModel_.samplers.reserve(mergedModel_.samplers.size() + model.samplers.size());
    mergedModel_.images.reserve(mergedModel_.images.size() + model.images.size());
    mergedModel_.textures.reserve(mergedModel_.textures.size() + model.textures.size());
    mergedModel_.materials.reserve(mergedModel_.materials.size() + model.materials.size());
    mergedModel_.meshes.reserve(mergedModel_.meshes.size() + model.meshes.size());
    mergedModel_.skins.reserve(mergedModel_.skins.size() + model.skins.size());
    mergedModel_.cameras.reserve(mergedModel_.cameras.size() + model.cameras.size());
    mergedModel_.nodes.reserve(mergedModel_.nodes.size() + model.nodes.size());
    mergedModel_.animations.reserve(mergedModel_.animations.size() + model.animations.size());

    for (size_t idx = 0; idx < model.bufferViews.size(); ++idx) {
        auto view = model.bufferViews[idx];
        const int sourceBuffer = view.buffer;
        const size_t offsetAdjustment = currentBufferSize +
            (sourceBuffer >= 0 && static_cast<size_t>(sourceBuffer) < bufferOffsets.size()
                 ? bufferOffsets[static_cast<size_t>(sourceBuffer)]
                 : 0);
        view.buffer = 0;
        view.byteOffset += offsetAdjustment;
        mergedModel_.bufferViews.push_back(std::move(view));
    }

    mergedModel_.accessors.insert(mergedModel_.accessors.end(),
                                  std::make_move_iterator(model.accessors.begin()),
                                  std::make_move_iterator(model.accessors.end()));
    mergedModel_.samplers.insert(mergedModel_.samplers.end(),
                                 std::make_move_iterator(model.samplers.begin()),
                                 std::make_move_iterator(model.samplers.end()));
    mergedModel_.images.insert(mergedModel_.images.end(),
                               std::make_move_iterator(model.images.begin()),
                               std::make_move_iterator(model.images.end()));
    mergedModel_.textures.insert(mergedModel_.textures.end(),
                                 std::make_move_iterator(model.textures.begin()),
                                 std::make_move_iterator(model.textures.end()));
    mergedModel_.materials.insert(mergedModel_.materials.end(),
                                  std::make_move_iterator(model.materials.begin()),
                                  std::make_move_iterator(model.materials.end()));
    mergedModel_.meshes.insert(mergedModel_.meshes.end(),
                               std::make_move_iterator(model.meshes.begin()),
                               std::make_move_iterator(model.meshes.end()));
    mergedModel_.skins.insert(mergedModel_.skins.end(),
                              std::make_move_iterator(model.skins.begin()),
                              std::make_move_iterator(model.skins.end()));
    mergedModel_.cameras.insert(mergedModel_.cameras.end(),
                                std::make_move_iterator(model.cameras.begin()),
                                std::make_move_iterator(model.cameras.end()));
    mergedModel_.nodes.insert(mergedModel_.nodes.end(),
                              std::make_move_iterator(model.nodes.begin()),
                              std::make_move_iterator(model.nodes.end()));
    mergedModel_.animations.insert(mergedModel_.animations.end(),
                                   std::make_move_iterator(model.animations.begin()),
                                   std::make_move_iterator(model.animations.end()));

    applyOffsets(mergedModel_, offsets, counts);

    if (keepScenesIndependent) {
        if (defaultScenesOnly) {
            const int sceneIdx = model.defaultScene >= 0 ? model.defaultScene : 0;
            if (sceneIdx >= 0 && sceneIdx < static_cast<int>(model.scenes.size())) {
                auto scene = model.scenes[sceneIdx];
                for (int& node : scene.nodes) {
                    node += offsets.nodes;
                }
                mergedModel_.scenes.push_back(std::move(scene));
            }
        } else {
            for (auto scene : model.scenes) {
                for (int& node : scene.nodes) {
                    node += offsets.nodes;
                }
                mergedModel_.scenes.push_back(std::move(scene));
            }
        }

        if (mergedModel_.defaultScene < 0 && !mergedModel_.scenes.empty()) {
            mergedModel_.defaultScene = 0;
        }
    } else {
        if (mergedModel_.scenes.empty()) {
            tinygltf::Scene mergedScene;
            mergedScene.name = "Merged Scene";
            mergedModel_.scenes.push_back(std::move(mergedScene));
            mergedModel_.defaultScene = 0;
        }

        if (defaultScenesOnly) {
            const int sceneIdx = model.defaultScene >= 0 ? model.defaultScene : 0;
            if (sceneIdx >= 0 && sceneIdx < static_cast<int>(model.scenes.size())) {
                const auto& scene = model.scenes[sceneIdx];
                for (const int node : scene.nodes) {
                    mergedModel_.scenes[0].nodes.push_back(node + offsets.nodes);
                }
            }
        } else {
            for (const auto& scene : model.scenes) {
                for (const int node : scene.nodes) {
                    mergedModel_.scenes[0].nodes.push_back(node + offsets.nodes);
                }
            }
        }
    }

    return true;
}

bool GltfMerger::save(const std::string& filename,
                      bool embedImages,
                      bool embedBuffers,
                      bool prettyPrint,
                      bool writeBinary) {
    if (mergedModel_.scenes.empty()) {
        errorMsg_ = "No merged model to save";
        return false;
    }

    if (writeBinary) {
        for (auto& buffer : mergedModel_.buffers) {
            buffer.uri.clear();
        }
    }

    std::string err;
    std::string warn;
    bool ok = false;
    if (writeBinary) {
        ok = loader_.WriteGltfSceneToFile(&mergedModel_, filename,
                                          embedImages, true, prettyPrint, true);
    } else {
        ok = loader_.WriteGltfSceneToFile(&mergedModel_, filename,
                                          embedImages, embedBuffers, prettyPrint, false);
    }

    if (!warn.empty()) {
        std::cerr << "Warning saving " << filename << ": " << warn << std::endl;
    }

    if (!ok) {
        errorMsg_ = "Failed to write file: " + filename;
        return false;
    }

    return true;
}

tinygltf::Model GltfMerger::getMergedModel() const {
    return mergedModel_;
}

void GltfMerger::clear() {
    mergedModel_ = tinygltf::Model();
    loader_ = tinygltf::TinyGLTF();
    firstModel_ = true;
    errorMsg_.clear();
}

} // namespace gltfu
