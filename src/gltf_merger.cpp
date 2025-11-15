#include "gltf_merger.h"

#include <iostream>
#include <algorithm>
#include <fstream>

namespace gltfu {

struct GltfMerger::Impl {
    tinygltf::Model mergedModel;
    tinygltf::TinyGLTF loader;
    bool firstModel = true;
    
    // For streaming: track buffer sources to avoid loading all data
    struct BufferSource {
        std::string filename;
        size_t bufferIndex;
        size_t offset;  // Offset in merged buffer
        size_t size;
    };
    std::vector<BufferSource> bufferSources;
};

GltfMerger::GltfMerger() : impl_(std::make_unique<Impl>()) {}

GltfMerger::~GltfMerger() = default;

bool GltfMerger::loadAndMergeFile(const std::string& filename, bool keepScenesIndependent, bool defaultScenesOnly) {
    tinygltf::Model model;
    std::string err, warn;
    
    bool ret = false;
    if (filename.substr(filename.find_last_of(".") + 1) == "glb") {
        ret = impl_->loader.LoadBinaryFromFile(&model, &err, &warn, filename);
    } else {
        ret = impl_->loader.LoadASCIIFromFile(&model, &err, &warn, filename);
    }
    
    if (!warn.empty()) {
        std::cerr << "Warning loading " << filename << ": " << warn << std::endl;
    }
    
    if (!err.empty()) {
        errorMsg_ = "Error loading " + filename + ": " + err;
        return false;
    }
    
    if (!ret) {
        errorMsg_ = "Failed to load " + filename;
        return false;
    }
    
    // Clear buffer URIs from loaded models to ensure they can be embedded
    for (auto& buffer : model.buffers) {
        buffer.uri.clear();
    }
    
    // Merge metadata and stream buffer data
    return mergeModelStreaming(std::move(model), keepScenesIndependent, defaultScenesOnly);
}

void GltfMerger::updateIndices(tinygltf::Model& target, const tinygltf::Model& source,
                               int nodeOffset, int meshOffset, int materialOffset,
                               int textureOffset, int imageOffset, int samplerOffset,
                               int accessorOffset, int bufferViewOffset, int bufferOffset) {
    // Update node indices in scenes
    for (auto& scene : target.scenes) {
        for (auto& nodeIdx : scene.nodes) {
            if (nodeIdx >= nodeOffset) {
                // This is a newly added node, already has correct offset
                continue;
            }
        }
    }
    
    // Update nodes
    size_t targetNodeStart = target.nodes.size() - source.nodes.size();
    for (size_t i = 0; i < source.nodes.size(); ++i) {
        auto& node = target.nodes[targetNodeStart + i];
        
        // Update children indices
        for (auto& childIdx : node.children) {
            childIdx += nodeOffset;
        }
        
        // Update mesh index
        if (node.mesh >= 0) {
            node.mesh += meshOffset;
        }
        
        // Update skin index
        if (node.skin >= 0) {
            node.skin += (target.skins.size() - source.skins.size());
        }
        
        // Update camera index
        if (node.camera >= 0) {
            node.camera += (target.cameras.size() - source.cameras.size());
        }
    }
    
    // Update meshes
    size_t targetMeshStart = target.meshes.size() - source.meshes.size();
    for (size_t i = 0; i < source.meshes.size(); ++i) {
        auto& mesh = target.meshes[targetMeshStart + i];
        
        for (auto& primitive : mesh.primitives) {
            // Update material index
            if (primitive.material >= 0) {
                primitive.material += materialOffset;
            }
            
            // Update accessor indices in attributes
            for (auto& attr : primitive.attributes) {
                attr.second += accessorOffset;
            }
            
            // Update indices accessor
            if (primitive.indices >= 0) {
                primitive.indices += accessorOffset;
            }
            
            // Update morph target accessors
            for (auto& target : primitive.targets) {
                for (auto& attr : target) {
                    attr.second += accessorOffset;
                }
            }
        }
    }
    
    // Update materials
    size_t targetMaterialStart = target.materials.size() - source.materials.size();
    for (size_t i = 0; i < source.materials.size(); ++i) {
        auto& material = target.materials[targetMaterialStart + i];
        
        // Update texture indices
        if (material.pbrMetallicRoughness.baseColorTexture.index >= 0) {
            material.pbrMetallicRoughness.baseColorTexture.index += textureOffset;
        }
        if (material.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
            material.pbrMetallicRoughness.metallicRoughnessTexture.index += textureOffset;
        }
        if (material.normalTexture.index >= 0) {
            material.normalTexture.index += textureOffset;
        }
        if (material.occlusionTexture.index >= 0) {
            material.occlusionTexture.index += textureOffset;
        }
        if (material.emissiveTexture.index >= 0) {
            material.emissiveTexture.index += textureOffset;
        }
    }
    
    // Update textures
    size_t targetTextureStart = target.textures.size() - source.textures.size();
    for (size_t i = 0; i < source.textures.size(); ++i) {
        auto& texture = target.textures[targetTextureStart + i];
        
        if (texture.source >= 0) {
            texture.source += imageOffset;
        }
        if (texture.sampler >= 0) {
            texture.sampler += samplerOffset;
        }
    }
    
    // Update accessors
    size_t targetAccessorStart = target.accessors.size() - source.accessors.size();
    for (size_t i = 0; i < source.accessors.size(); ++i) {
        auto& accessor = target.accessors[targetAccessorStart + i];
        
        if (accessor.bufferView >= 0) {
            accessor.bufferView += bufferViewOffset;
        }
    }
    
    // Update buffer views
    size_t targetBufferViewStart = target.bufferViews.size() - source.bufferViews.size();
    for (size_t i = 0; i < source.bufferViews.size(); ++i) {
        auto& bufferView = target.bufferViews[targetBufferViewStart + i];
        
        if (bufferView.buffer >= 0) {
            bufferView.buffer += bufferOffset;
        }
    }
    
    // Update animations
    size_t targetAnimationStart = target.animations.size() - source.animations.size();
    for (size_t i = 0; i < source.animations.size(); ++i) {
        auto& animation = target.animations[targetAnimationStart + i];
        
        for (auto& sampler : animation.samplers) {
            if (sampler.input >= 0) sampler.input += accessorOffset;
            if (sampler.output >= 0) sampler.output += accessorOffset;
        }
        
        for (auto& channel : animation.channels) {
            if (channel.sampler >= 0) {
                // Sampler is relative to this animation
                // No offset needed
            }
            if (channel.target_node >= 0) {
                channel.target_node += nodeOffset;
            }
        }
    }
    
    // Update skins
    size_t targetSkinStart = target.skins.size() - source.skins.size();
    for (size_t i = 0; i < source.skins.size(); ++i) {
        auto& skin = target.skins[targetSkinStart + i];
        
        if (skin.inverseBindMatrices >= 0) {
            skin.inverseBindMatrices += accessorOffset;
        }
        if (skin.skeleton >= 0) {
            skin.skeleton += nodeOffset;
        }
        for (auto& joint : skin.joints) {
            joint += nodeOffset;
        }
    }
}

bool GltfMerger::mergeModelStreaming(tinygltf::Model&& model, bool keepScenesIndependent, bool defaultScenesOnly) {
    // Initialize merged model on first file
    if (impl_->firstModel) {
        impl_->mergedModel.asset = model.asset;
        impl_->firstModel = false;
        
        // Create single merged buffer that we'll stream data into
        tinygltf::Buffer mergedBuffer;
        mergedBuffer.name = "merged_buffer";
        impl_->mergedModel.buffers.push_back(mergedBuffer);
    }
    
    // Store current sizes for offset calculation
    int nodeOffset = impl_->mergedModel.nodes.size();
    int meshOffset = impl_->mergedModel.meshes.size();
    int materialOffset = impl_->mergedModel.materials.size();
    int textureOffset = impl_->mergedModel.textures.size();
    int imageOffset = impl_->mergedModel.images.size();
    int samplerOffset = impl_->mergedModel.samplers.size();
    int accessorOffset = impl_->mergedModel.accessors.size();
    int bufferViewOffset = impl_->mergedModel.bufferViews.size();
    int bufferOffset = 0;  // Always 0 since we use single merged buffer
    
    // Reserve space to avoid reallocations (metadata only - buffers handled separately)
    impl_->mergedModel.bufferViews.reserve(impl_->mergedModel.bufferViews.size() + model.bufferViews.size());
    impl_->mergedModel.accessors.reserve(impl_->mergedModel.accessors.size() + model.accessors.size());
    impl_->mergedModel.samplers.reserve(impl_->mergedModel.samplers.size() + model.samplers.size());
    impl_->mergedModel.images.reserve(impl_->mergedModel.images.size() + model.images.size());
    impl_->mergedModel.textures.reserve(impl_->mergedModel.textures.size() + model.textures.size());
    impl_->mergedModel.materials.reserve(impl_->mergedModel.materials.size() + model.materials.size());
    impl_->mergedModel.meshes.reserve(impl_->mergedModel.meshes.size() + model.meshes.size());
    impl_->mergedModel.skins.reserve(impl_->mergedModel.skins.size() + model.skins.size());
    impl_->mergedModel.cameras.reserve(impl_->mergedModel.cameras.size() + model.cameras.size());
    impl_->mergedModel.nodes.reserve(impl_->mergedModel.nodes.size() + model.nodes.size());
    impl_->mergedModel.animations.reserve(impl_->mergedModel.animations.size() + model.animations.size());
    
    // Stream buffer data into single merged buffer
    size_t currentBufferOffset = impl_->mergedModel.buffers[0].data.size();
    
    // Calculate buffer sizes before moving data
    std::vector<size_t> bufferSizes;
    bufferSizes.reserve(model.buffers.size());
    size_t totalNewBufferSize = 0;
    for (const auto& buffer : model.buffers) {
        bufferSizes.push_back(buffer.data.size());
        totalNewBufferSize += buffer.data.size();
    }
    
    // Reserve space in merged buffer to avoid reallocations
    impl_->mergedModel.buffers[0].data.reserve(currentBufferOffset + totalNewBufferSize);
    
    // Append all buffer data and clear source immediately
    for (auto& buffer : model.buffers) {
        impl_->mergedModel.buffers[0].data.insert(
            impl_->mergedModel.buffers[0].data.end(),
            std::make_move_iterator(buffer.data.begin()),
            std::make_move_iterator(buffer.data.end())
        );
        // Free source buffer memory immediately
        buffer.data.clear();
        buffer.data.shrink_to_fit();
    }
    
    // Move buffer views and update their buffer references and byte offsets
    for (auto& bufferView : model.bufferViews) {
        // All buffer views now reference the single merged buffer (index 0)
        size_t oldBufferIndex = bufferView.buffer;
        bufferView.buffer = 0;
        
        // Calculate offset adjustment: current offset + sum of previous buffer sizes
        size_t offsetAdjustment = currentBufferOffset;
        for (size_t i = 0; i < oldBufferIndex && i < bufferSizes.size(); ++i) {
            offsetAdjustment += bufferSizes[i];
        }
        
        bufferView.byteOffset += offsetAdjustment;
        impl_->mergedModel.bufferViews.push_back(std::move(bufferView));
    }
    impl_->mergedModel.accessors.insert(impl_->mergedModel.accessors.end(), 
                                       std::make_move_iterator(model.accessors.begin()), 
                                       std::make_move_iterator(model.accessors.end()));
    impl_->mergedModel.samplers.insert(impl_->mergedModel.samplers.end(), 
                                      std::make_move_iterator(model.samplers.begin()), 
                                      std::make_move_iterator(model.samplers.end()));
    impl_->mergedModel.images.insert(impl_->mergedModel.images.end(), 
                                    std::make_move_iterator(model.images.begin()), 
                                    std::make_move_iterator(model.images.end()));
    impl_->mergedModel.textures.insert(impl_->mergedModel.textures.end(), 
                                      std::make_move_iterator(model.textures.begin()), 
                                      std::make_move_iterator(model.textures.end()));
    impl_->mergedModel.materials.insert(impl_->mergedModel.materials.end(), 
                                       std::make_move_iterator(model.materials.begin()), 
                                       std::make_move_iterator(model.materials.end()));
    impl_->mergedModel.meshes.insert(impl_->mergedModel.meshes.end(), 
                                    std::make_move_iterator(model.meshes.begin()), 
                                    std::make_move_iterator(model.meshes.end()));
    impl_->mergedModel.skins.insert(impl_->mergedModel.skins.end(), 
                                   std::make_move_iterator(model.skins.begin()), 
                                   std::make_move_iterator(model.skins.end()));
    impl_->mergedModel.cameras.insert(impl_->mergedModel.cameras.end(), 
                                     std::make_move_iterator(model.cameras.begin()), 
                                     std::make_move_iterator(model.cameras.end()));
    impl_->mergedModel.nodes.insert(impl_->mergedModel.nodes.end(), 
                                   std::make_move_iterator(model.nodes.begin()), 
                                   std::make_move_iterator(model.nodes.end()));
    impl_->mergedModel.animations.insert(impl_->mergedModel.animations.end(), 
                                        std::make_move_iterator(model.animations.begin()), 
                                        std::make_move_iterator(model.animations.end()));
    
    // Update indices in the newly added components
    updateIndices(impl_->mergedModel, model, nodeOffset, meshOffset, materialOffset,
                 textureOffset, imageOffset, samplerOffset, accessorOffset, 
                 bufferViewOffset, bufferOffset);
    
    // Handle scenes based on flags
    if (keepScenesIndependent) {
        // Keep scenes separate - add all scenes (or just default) from this model
        if (defaultScenesOnly) {
            // Only add the default scene
            int sceneIdx = model.defaultScene >= 0 ? model.defaultScene : 0;
            if (sceneIdx < static_cast<int>(model.scenes.size())) {
                auto scene = model.scenes[sceneIdx];
                // Update node indices
                for (auto& nodeIdx : scene.nodes) {
                    nodeIdx += nodeOffset;
                }
                impl_->mergedModel.scenes.push_back(std::move(scene));
            }
        } else {
            // Add all scenes
            for (auto& scene : model.scenes) {
                // Update node indices
                for (auto& nodeIdx : scene.nodes) {
                    nodeIdx += nodeOffset;
                }
                impl_->mergedModel.scenes.push_back(std::move(scene));
            }
        }
        
        // Set default scene to the first scene if not already set
        if (impl_->mergedModel.defaultScene < 0 && !impl_->mergedModel.scenes.empty()) {
            impl_->mergedModel.defaultScene = 0;
        }
    } else {
        // Merge into a single combined scene (default behavior)
        if (impl_->mergedModel.scenes.empty()) {
            tinygltf::Scene mergedScene;
            mergedScene.name = "Merged Scene";
            impl_->mergedModel.scenes.push_back(mergedScene);
            impl_->mergedModel.defaultScene = 0;
        }
        
        // Add nodes from scenes to the merged scene
        if (defaultScenesOnly) {
            // Only add nodes from the default scene
            int sceneIdx = model.defaultScene >= 0 ? model.defaultScene : 0;
            if (sceneIdx < static_cast<int>(model.scenes.size())) {
                const auto& scene = model.scenes[sceneIdx];
                for (int nodeIdx : scene.nodes) {
                    impl_->mergedModel.scenes[0].nodes.push_back(nodeIdx + nodeOffset);
                }
            }
        } else {
            // Add nodes from all scenes
            for (const auto& scene : model.scenes) {
                for (int nodeIdx : scene.nodes) {
                    impl_->mergedModel.scenes[0].nodes.push_back(nodeIdx + nodeOffset);
                }
            }
        }
    }
    
    // Clear source model to free memory immediately
    model = tinygltf::Model();
    
    return true;
}

bool GltfMerger::save(const std::string& filename, 
                     bool embedImages, 
                     bool embedBuffers,
                     bool prettyPrint,
                     bool writeBinary) {
    if (impl_->mergedModel.scenes.empty()) {
        errorMsg_ = "No merged model to save";
        return false;
    }
    
    // When writing to GLB, clear buffer URIs so data is embedded in binary chunk
    if (writeBinary) {
        for (auto& buffer : impl_->mergedModel.buffers) {
            buffer.uri.clear();
        }
    }
    
    std::string err, warn;
    bool ret = false;
    
    if (writeBinary) {
        // GLB format must embed buffers in the binary chunk
        ret = impl_->loader.WriteGltfSceneToFile(&impl_->mergedModel, filename,
                                                embedImages, true, prettyPrint, true);
    } else {
        ret = impl_->loader.WriteGltfSceneToFile(&impl_->mergedModel, filename,
                                                embedImages, embedBuffers, prettyPrint, false);
    }
    
    if (!ret) {
        errorMsg_ = "Failed to write file: " + filename;
        return false;
    }
    
    return true;
}

void GltfMerger::clear() {
    impl_->mergedModel = tinygltf::Model();
    impl_->firstModel = true;
    errorMsg_.clear();
}

} // namespace gltfu
