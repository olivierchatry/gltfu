#include "gltf_prune.h"
#include <iostream>
#include <algorithm>
#include <unordered_map>

namespace gltfu {

bool GltfPrune::process(tinygltf::Model& model, const PruneOptions& options) {
    std::cout << "Pruning unused resources..." << std::endl;
    
    // Track which resources are actually used
    std::unordered_set<int> usedNodes;
    std::unordered_set<int> usedMeshes;
    std::unordered_set<int> usedMaterials;
    std::unordered_set<int> usedAccessors;
    std::unordered_set<int> usedTextures;
    std::unordered_set<int> usedImages;
    std::unordered_set<int> usedSamplers;
    std::unordered_set<int> usedBufferViews;
    std::unordered_set<int> usedBuffers;
    std::unordered_set<int> usedSkins;
    std::unordered_set<int> usedCameras;
    
    // Mark all resources reachable from scenes
    markReachableFromScenes(model, usedNodes, usedMeshes, usedMaterials,
                           usedAccessors, usedTextures, usedImages, usedSamplers,
                           usedBufferViews, usedBuffers, usedSkins, usedCameras);
    
    // Mark resources used by animations
    markAnimationResources(model, usedNodes, usedAccessors, usedBufferViews, usedBuffers);
    
    // Prune empty leaf nodes if requested
    if (!options.keepLeaves) {
        pruneEmptyLeafNodes(model, options);
        // Re-mark after leaf pruning
        usedNodes.clear();
        usedMeshes.clear();
        usedMaterials.clear();
        usedAccessors.clear();
        usedTextures.clear();
        usedImages.clear();
        usedSamplers.clear();
        usedBufferViews.clear();
        usedBuffers.clear();
        usedSkins.clear();
        usedCameras.clear();
        markReachableFromScenes(model, usedNodes, usedMeshes, usedMaterials,
                               usedAccessors, usedTextures, usedImages, usedSamplers,
                               usedBufferViews, usedBuffers, usedSkins, usedCameras);
        markAnimationResources(model, usedNodes, usedAccessors, usedBufferViews, usedBuffers);
    }
    
    // Prune unused vertex attributes if requested
    if (!options.keepAttributes) {
        pruneUnusedAttributes(model);

        usedNodes.clear();
        usedMeshes.clear();
        usedMaterials.clear();
        usedAccessors.clear();
        usedTextures.clear();
        usedImages.clear();
        usedSamplers.clear();
        usedBufferViews.clear();
        usedBuffers.clear();
        usedSkins.clear();
        usedCameras.clear();

        markReachableFromScenes(model, usedNodes, usedMeshes, usedMaterials,
                               usedAccessors, usedTextures, usedImages, usedSamplers,
                               usedBufferViews, usedBuffers, usedSkins, usedCameras);
        markAnimationResources(model, usedNodes, usedAccessors, usedBufferViews, usedBuffers);
    }

    auto ensureAccessorResourcesMarked = [&]() {
        for (int accessorIdx : usedAccessors) {
            if (accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors.size())) {
                continue;
            }
            const auto& accessor = model.accessors[accessorIdx];
            if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
                continue;
            }

            usedBufferViews.insert(accessor.bufferView);

            const auto& bufferView = model.bufferViews[accessor.bufferView];
            if (bufferView.buffer >= 0 && bufferView.buffer < static_cast<int>(model.buffers.size())) {
                usedBuffers.insert(bufferView.buffer);
            }
        }
    };

    ensureAccessorResourcesMarked();
    
    // Build index maps for remapping
    auto nodeMap = buildIndexMap(model.nodes.size(), usedNodes);
    auto meshMap = buildIndexMap(model.meshes.size(), usedMeshes);
    auto materialMap = buildIndexMap(model.materials.size(), usedMaterials);
    auto accessorMap = buildIndexMap(model.accessors.size(), usedAccessors);
    auto textureMap = buildIndexMap(model.textures.size(), usedTextures);
    auto imageMap = buildIndexMap(model.images.size(), usedImages);
    auto samplerMap = buildIndexMap(model.samplers.size(), usedSamplers);
    auto bufferViewMap = buildIndexMap(model.bufferViews.size(), usedBufferViews);
    auto bufferMap = buildIndexMap(model.buffers.size(), usedBuffers);
    auto skinMap = buildIndexMap(model.skins.size(), usedSkins);
    auto cameraMap = buildIndexMap(model.cameras.size(), usedCameras);
    
    // Count what we're removing
    int removedNodes = model.nodes.size() - usedNodes.size();
    int removedMeshes = model.meshes.size() - usedMeshes.size();
    int removedMaterials = model.materials.size() - usedMaterials.size();
    int removedAccessors = model.accessors.size() - usedAccessors.size();
    int removedTextures = model.textures.size() - usedTextures.size();
    int removedImages = model.images.size() - usedImages.size();
    int removedSamplers = model.samplers.size() - usedSamplers.size();
    int removedBufferViews = model.bufferViews.size() - usedBufferViews.size();
    int removedBuffers = model.buffers.size() - usedBuffers.size();
    int removedSkins = model.skins.size() - usedSkins.size();
    int removedCameras = model.cameras.size() - usedCameras.size();
    
    // Update all indices in scenes
    for (auto& scene : model.scenes) {
        std::vector<int> newNodes;
        for (int nodeIdx : scene.nodes) {
            if (nodeMap[nodeIdx] != -1) {
                newNodes.push_back(nodeMap[nodeIdx]);
            }
        }
        scene.nodes = newNodes;
    }
    
    // Update all indices in nodes
    for (auto& node : model.nodes) {
        // Update children
        std::vector<int> newChildren;
        for (int childIdx : node.children) {
            if (nodeMap[childIdx] != -1) {
                newChildren.push_back(nodeMap[childIdx]);
            }
        }
        node.children = newChildren;
        
        // Update mesh reference
        if (node.mesh >= 0 && meshMap[node.mesh] != -1) {
            node.mesh = meshMap[node.mesh];
        } else if (node.mesh >= 0) {
            node.mesh = -1;
        }
        
        // Update skin reference
        if (node.skin >= 0 && skinMap[node.skin] != -1) {
            node.skin = skinMap[node.skin];
        } else if (node.skin >= 0) {
            node.skin = -1;
        }
        
        // Update camera reference
        if (node.camera >= 0 && cameraMap[node.camera] != -1) {
            node.camera = cameraMap[node.camera];
        } else if (node.camera >= 0) {
            node.camera = -1;
        }
    }
    
    // Update mesh primitives
    for (auto& mesh : model.meshes) {
        for (auto& prim : mesh.primitives) {
            // Update material
            if (prim.material >= 0 && materialMap[prim.material] != -1) {
                prim.material = materialMap[prim.material];
            } else if (prim.material >= 0) {
                prim.material = -1;
            }
            
            // Update indices
            if (prim.indices >= 0 && accessorMap[prim.indices] != -1) {
                prim.indices = accessorMap[prim.indices];
            } else if (prim.indices >= 0) {
                prim.indices = -1;
            }
            
            // Update attributes
            for (auto& attr : prim.attributes) {
                if (accessorMap[attr.second] != -1) {
                    attr.second = accessorMap[attr.second];
                } else {
                    attr.second = -1;
                }
            }
            
            // Update morph targets
            for (auto& target : prim.targets) {
                for (auto& attr : target) {
                    if (accessorMap[attr.second] != -1) {
                        attr.second = accessorMap[attr.second];
                    } else {
                        attr.second = -1;
                    }
                }
            }
            
            // Update Draco compression extension bufferView
            if (prim.extensions.count("KHR_draco_mesh_compression") > 0) {
                auto& dracoExt = prim.extensions.at("KHR_draco_mesh_compression");
                if (dracoExt.Has("bufferView") && dracoExt.Get("bufferView").IsInt()) {
                    int oldBvIdx = dracoExt.Get("bufferView").Get<int>();
                    if (oldBvIdx >= 0 && oldBvIdx < static_cast<int>(bufferViewMap.size())) {
                        int newBvIdx = bufferViewMap[oldBvIdx];
                        if (newBvIdx != -1) {
                            // Need to get the mutable object and update it
                            if (dracoExt.IsObject()) {
                                auto& obj = dracoExt.Get<tinygltf::Value::Object>();
                                obj["bufferView"] = tinygltf::Value(newBvIdx);
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Update materials
    for (auto& material : model.materials) {
        auto updateTextureInfo = [&](tinygltf::TextureInfo& info) {
            if (info.index >= 0 && textureMap[info.index] != -1) {
                info.index = textureMap[info.index];
            } else if (info.index >= 0) {
                info.index = -1;
            }
        };
        
        auto updateNormalTextureInfo = [&](tinygltf::NormalTextureInfo& info) {
            if (info.index >= 0 && textureMap[info.index] != -1) {
                info.index = textureMap[info.index];
            } else if (info.index >= 0) {
                info.index = -1;
            }
        };
        
        auto updateOcclusionTextureInfo = [&](tinygltf::OcclusionTextureInfo& info) {
            if (info.index >= 0 && textureMap[info.index] != -1) {
                info.index = textureMap[info.index];
            } else if (info.index >= 0) {
                info.index = -1;
            }
        };
        
        updateTextureInfo(material.pbrMetallicRoughness.baseColorTexture);
        updateTextureInfo(material.pbrMetallicRoughness.metallicRoughnessTexture);
        updateNormalTextureInfo(material.normalTexture);
        updateOcclusionTextureInfo(material.occlusionTexture);
        updateTextureInfo(material.emissiveTexture);
    }
    
    // Update textures
    for (auto& texture : model.textures) {
        if (texture.source >= 0 && imageMap[texture.source] != -1) {
            texture.source = imageMap[texture.source];
        } else if (texture.source >= 0) {
            texture.source = -1;
        }
        
        if (texture.sampler >= 0 && samplerMap[texture.sampler] != -1) {
            texture.sampler = samplerMap[texture.sampler];
        } else if (texture.sampler >= 0) {
            texture.sampler = -1;
        }
    }
    
    // Update accessors
    for (auto& accessor : model.accessors) {
        if (accessor.bufferView >= 0 && bufferViewMap[accessor.bufferView] != -1) {
            accessor.bufferView = bufferViewMap[accessor.bufferView];
        } else if (accessor.bufferView >= 0) {
            accessor.bufferView = -1;
        }
    }
    
    // Update buffer views
    for (auto& bufferView : model.bufferViews) {
        if (bufferView.buffer >= 0 && bufferMap[bufferView.buffer] != -1) {
            bufferView.buffer = bufferMap[bufferView.buffer];
        } else if (bufferView.buffer >= 0) {
            bufferView.buffer = -1;
        }
    }
    
    // Update skins
    for (auto& skin : model.skins) {
        if (skin.inverseBindMatrices >= 0 && accessorMap[skin.inverseBindMatrices] != -1) {
            skin.inverseBindMatrices = accessorMap[skin.inverseBindMatrices];
        } else if (skin.inverseBindMatrices >= 0) {
            skin.inverseBindMatrices = -1;
        }
        
        if (skin.skeleton >= 0 && nodeMap[skin.skeleton] != -1) {
            skin.skeleton = nodeMap[skin.skeleton];
        } else if (skin.skeleton >= 0) {
            skin.skeleton = -1;
        }
        
        std::vector<int> newJoints;
        for (int jointIdx : skin.joints) {
            if (nodeMap[jointIdx] != -1) {
                newJoints.push_back(nodeMap[jointIdx]);
            }
        }
        skin.joints = newJoints;
    }
    
    // Update animations
    for (auto& anim : model.animations) {
        for (auto& channel : anim.channels) {
            if (channel.target_node >= 0 && nodeMap[channel.target_node] != -1) {
                channel.target_node = nodeMap[channel.target_node];
            } else if (channel.target_node >= 0) {
                channel.target_node = -1;
            }
        }
        
        for (auto& sampler : anim.samplers) {
            if (sampler.input >= 0 && accessorMap[sampler.input] != -1) {
                sampler.input = accessorMap[sampler.input];
            } else if (sampler.input >= 0) {
                sampler.input = -1;
            }
            
            if (sampler.output >= 0 && accessorMap[sampler.output] != -1) {
                sampler.output = accessorMap[sampler.output];
            } else if (sampler.output >= 0) {
                sampler.output = -1;
            }
        }
    }
    
    // Actually remove unused resources
    model.nodes = removeUnused(model.nodes, usedNodes);
    model.meshes = removeUnused(model.meshes, usedMeshes);
    model.materials = removeUnused(model.materials, usedMaterials);
    model.accessors = removeUnused(model.accessors, usedAccessors);
    model.textures = removeUnused(model.textures, usedTextures);
    model.images = removeUnused(model.images, usedImages);
    model.samplers = removeUnused(model.samplers, usedSamplers);
    model.bufferViews = removeUnused(model.bufferViews, usedBufferViews);
    model.buffers = removeUnused(model.buffers, usedBuffers);
    model.skins = removeUnused(model.skins, usedSkins);
    model.cameras = removeUnused(model.cameras, usedCameras);
    
    // Report results
    int totalRemoved = removedNodes + removedMeshes + removedMaterials + removedAccessors +
                      removedTextures + removedImages + removedSamplers + removedBufferViews +
                      removedBuffers + removedSkins + removedCameras;
    
    if (totalRemoved > 0) {
        std::cout << "Removed:" << std::endl;
        if (removedNodes > 0) std::cout << "  Nodes: " << removedNodes << std::endl;
        if (removedMeshes > 0) std::cout << "  Meshes: " << removedMeshes << std::endl;
        if (removedMaterials > 0) std::cout << "  Materials: " << removedMaterials << std::endl;
        if (removedAccessors > 0) std::cout << "  Accessors: " << removedAccessors << std::endl;
        if (removedTextures > 0) std::cout << "  Textures: " << removedTextures << std::endl;
        if (removedImages > 0) std::cout << "  Images: " << removedImages << std::endl;
        if (removedSamplers > 0) std::cout << "  Samplers: " << removedSamplers << std::endl;
        if (removedBufferViews > 0) std::cout << "  Buffer Views: " << removedBufferViews << std::endl;
        if (removedBuffers > 0) std::cout << "  Buffers: " << removedBuffers << std::endl;
        if (removedSkins > 0) std::cout << "  Skins: " << removedSkins << std::endl;
        if (removedCameras > 0) std::cout << "  Cameras: " << removedCameras << std::endl;
    } else {
        std::cout << "No unused resources found." << std::endl;
    }
    
    return true;
}

void GltfPrune::markReachableFromScenes(const tinygltf::Model& model,
                                         std::unordered_set<int>& usedNodes,
                                         std::unordered_set<int>& usedMeshes,
                                         std::unordered_set<int>& usedMaterials,
                                         std::unordered_set<int>& usedAccessors,
                                         std::unordered_set<int>& usedTextures,
                                         std::unordered_set<int>& usedImages,
                                         std::unordered_set<int>& usedSamplers,
                                         std::unordered_set<int>& usedBufferViews,
                                         std::unordered_set<int>& usedBuffers,
                                         std::unordered_set<int>& usedSkins,
                                         std::unordered_set<int>& usedCameras) {
    for (const auto& scene : model.scenes) {
        for (int nodeIdx : scene.nodes) {
            markNode(nodeIdx, model, usedNodes, usedMeshes, usedMaterials,
                    usedAccessors, usedTextures, usedImages, usedSamplers,
                    usedBufferViews, usedBuffers, usedSkins, usedCameras);
        }
    }
}

void GltfPrune::markAnimationResources(const tinygltf::Model& model,
                                        std::unordered_set<int>& usedNodes,
                                        std::unordered_set<int>& usedAccessors,
                                        std::unordered_set<int>& usedBufferViews,
                                        std::unordered_set<int>& usedBuffers) {
    for (const auto& anim : model.animations) {
        for (const auto& channel : anim.channels) {
            if (channel.target_node >= 0) {
                usedNodes.insert(channel.target_node);
            }
        }
        
        for (const auto& sampler : anim.samplers) {
            if (sampler.input >= 0) {
                markAccessor(sampler.input, model, usedBufferViews, usedBuffers);
                usedAccessors.insert(sampler.input);
            }
            if (sampler.output >= 0) {
                markAccessor(sampler.output, model, usedBufferViews, usedBuffers);
                usedAccessors.insert(sampler.output);
            }
        }
    }
}

void GltfPrune::markNode(int nodeIdx,
                          const tinygltf::Model& model,
                          std::unordered_set<int>& usedNodes,
                          std::unordered_set<int>& usedMeshes,
                          std::unordered_set<int>& usedMaterials,
                          std::unordered_set<int>& usedAccessors,
                          std::unordered_set<int>& usedTextures,
                          std::unordered_set<int>& usedImages,
                          std::unordered_set<int>& usedSamplers,
                          std::unordered_set<int>& usedBufferViews,
                          std::unordered_set<int>& usedBuffers,
                          std::unordered_set<int>& usedSkins,
                          std::unordered_set<int>& usedCameras) {
    if (nodeIdx < 0 || nodeIdx >= (int)model.nodes.size()) return;
    if (usedNodes.count(nodeIdx)) return; // Already visited
    
    usedNodes.insert(nodeIdx);
    const auto& node = model.nodes[nodeIdx];
    
    // Mark mesh
    if (node.mesh >= 0) {
        markMesh(node.mesh, model, usedMaterials, usedAccessors, usedTextures,
                usedImages, usedSamplers, usedBufferViews, usedBuffers);
        usedMeshes.insert(node.mesh);
    }
    
    // Mark skin
    if (node.skin >= 0) {
        markSkin(node.skin, model, usedNodes, usedAccessors, usedBufferViews, usedBuffers);
        usedSkins.insert(node.skin);
    }
    
    // Mark camera
    if (node.camera >= 0) {
        usedCameras.insert(node.camera);
    }
    
    // Recurse to children
    for (int childIdx : node.children) {
        markNode(childIdx, model, usedNodes, usedMeshes, usedMaterials,
                usedAccessors, usedTextures, usedImages, usedSamplers,
                usedBufferViews, usedBuffers, usedSkins, usedCameras);
    }
}

void GltfPrune::markMesh(int meshIdx,
                          const tinygltf::Model& model,
                          std::unordered_set<int>& usedMaterials,
                          std::unordered_set<int>& usedAccessors,
                          std::unordered_set<int>& usedTextures,
                          std::unordered_set<int>& usedImages,
                          std::unordered_set<int>& usedSamplers,
                          std::unordered_set<int>& usedBufferViews,
                          std::unordered_set<int>& usedBuffers) {
    if (meshIdx < 0 || meshIdx >= (int)model.meshes.size()) return;
    
    const auto& mesh = model.meshes[meshIdx];
    for (const auto& prim : mesh.primitives) {
        // Mark material
        if (prim.material >= 0) {
            markMaterial(prim.material, model, usedTextures, usedImages, usedSamplers);
            usedMaterials.insert(prim.material);
        }
        
        // Mark indices
        if (prim.indices >= 0) {
            markAccessor(prim.indices, model, usedBufferViews, usedBuffers);
            usedAccessors.insert(prim.indices);
        }
        
        // Mark attributes
        for (const auto& attr : prim.attributes) {
            markAccessor(attr.second, model, usedBufferViews, usedBuffers);
            usedAccessors.insert(attr.second);
        }
        
        // Mark morph targets
        for (const auto& target : prim.targets) {
            for (const auto& attr : target) {
                markAccessor(attr.second, model, usedBufferViews, usedBuffers);
                usedAccessors.insert(attr.second);
            }
        }
        
        // Mark Draco compression extension bufferView
        if (prim.extensions.count("KHR_draco_mesh_compression") > 0) {
            const auto& dracoExt = prim.extensions.at("KHR_draco_mesh_compression");
            if (dracoExt.Has("bufferView") && dracoExt.Get("bufferView").IsInt()) {
                int bvIdx = dracoExt.Get("bufferView").Get<int>();
                if (bvIdx >= 0 && bvIdx < static_cast<int>(model.bufferViews.size())) {
                    usedBufferViews.insert(bvIdx);
                    const auto& bv = model.bufferViews[bvIdx];
                    if (bv.buffer >= 0) {
                        usedBuffers.insert(bv.buffer);
                    }
                }
            }
        }
    }
}

void GltfPrune::markMaterial(int materialIdx,
                              const tinygltf::Model& model,
                              std::unordered_set<int>& usedTextures,
                              std::unordered_set<int>& usedImages,
                              std::unordered_set<int>& usedSamplers) {
    if (materialIdx < 0 || materialIdx >= (int)model.materials.size()) return;
    
    const auto& material = model.materials[materialIdx];
    
    auto markTextureInfo = [&](int textureIdx) {
        if (textureIdx >= 0) {
            markTexture(textureIdx, model, usedImages, usedSamplers);
            usedTextures.insert(textureIdx);
        }
    };
    
    markTextureInfo(material.pbrMetallicRoughness.baseColorTexture.index);
    markTextureInfo(material.pbrMetallicRoughness.metallicRoughnessTexture.index);
    markTextureInfo(material.normalTexture.index);
    markTextureInfo(material.occlusionTexture.index);
    markTextureInfo(material.emissiveTexture.index);
}

void GltfPrune::markTexture(int textureIdx,
                             const tinygltf::Model& model,
                             std::unordered_set<int>& usedImages,
                             std::unordered_set<int>& usedSamplers) {
    if (textureIdx < 0 || textureIdx >= (int)model.textures.size()) return;
    
    const auto& texture = model.textures[textureIdx];
    if (texture.source >= 0) {
        usedImages.insert(texture.source);
    }
    if (texture.sampler >= 0) {
        usedSamplers.insert(texture.sampler);
    }
}

void GltfPrune::markAccessor(int accessorIdx,
                              const tinygltf::Model& model,
                              std::unordered_set<int>& usedBufferViews,
                              std::unordered_set<int>& usedBuffers) {
    if (accessorIdx < 0 || accessorIdx >= (int)model.accessors.size()) return;
    
    const auto& accessor = model.accessors[accessorIdx];
    if (accessor.bufferView >= 0) {
        usedBufferViews.insert(accessor.bufferView);
        
        if (accessor.bufferView < (int)model.bufferViews.size()) {
            const auto& bufferView = model.bufferViews[accessor.bufferView];
            if (bufferView.buffer >= 0) {
                usedBuffers.insert(bufferView.buffer);
            }
        }
    }
}

void GltfPrune::markSkin(int skinIdx,
                          const tinygltf::Model& model,
                          std::unordered_set<int>& usedNodes,
                          std::unordered_set<int>& usedAccessors,
                          std::unordered_set<int>& usedBufferViews,
                          std::unordered_set<int>& usedBuffers) {
    if (skinIdx < 0 || skinIdx >= (int)model.skins.size()) return;
    
    const auto& skin = model.skins[skinIdx];
    
    if (skin.inverseBindMatrices >= 0) {
        markAccessor(skin.inverseBindMatrices, model, usedBufferViews, usedBuffers);
        usedAccessors.insert(skin.inverseBindMatrices);
    }
    
    if (skin.skeleton >= 0) {
        usedNodes.insert(skin.skeleton);
    }
    
    for (int jointIdx : skin.joints) {
        usedNodes.insert(jointIdx);
    }
}

void GltfPrune::pruneEmptyLeafNodes(tinygltf::Model& model, const PruneOptions& options) {
    bool changed = true;
    while (changed) {
        changed = false;
        
        for (auto& node : model.nodes) {
            std::vector<int> newChildren;
            for (int childIdx : node.children) {
                if (childIdx < 0 || childIdx >= (int)model.nodes.size()) continue;
                
                const auto& child = model.nodes[childIdx];
                bool isEmpty = child.mesh < 0 && child.skin < 0 && child.camera < 0 &&
                              child.children.empty();
                bool hasExtras = !options.keepExtras || child.extras.Keys().empty();
                
                if (!isEmpty || !hasExtras) {
                    newChildren.push_back(childIdx);
                } else {
                    changed = true;
                }
            }
            node.children = newChildren;
        }
        
        // Remove from scene roots too
        for (auto& scene : model.scenes) {
            std::vector<int> newNodes;
            for (int nodeIdx : scene.nodes) {
                if (nodeIdx < 0 || nodeIdx >= (int)model.nodes.size()) continue;
                
                const auto& node = model.nodes[nodeIdx];
                bool isEmpty = node.mesh < 0 && node.skin < 0 && node.camera < 0 &&
                              node.children.empty();
                bool hasExtras = !options.keepExtras || node.extras.Keys().empty();
                
                if (!isEmpty || !hasExtras) {
                    newNodes.push_back(nodeIdx);
                } else {
                    changed = true;
                }
            }
            scene.nodes = newNodes;
        }
    }
}

void GltfPrune::pruneUnusedAttributes(tinygltf::Model& model) {
    for (auto& mesh : model.meshes) {
        for (auto& prim : mesh.primitives) {
            const tinygltf::Material* material = nullptr;
            if (prim.material >= 0 && prim.material < (int)model.materials.size()) {
                material = &model.materials[prim.material];
            }
            
            std::vector<std::string> toRemove;
            for (const auto& attr : prim.attributes) {
                if (!isSemanticRequired(attr.first, material, model)) {
                    toRemove.push_back(attr.first);
                }
            }
            
            for (const auto& semantic : toRemove) {
                prim.attributes.erase(semantic);
            }
        }
    }
}

bool GltfPrune::isSemanticRequired(const std::string& semantic,
                                    const tinygltf::Material* material,
                                    const tinygltf::Model& /* model */) const {
    // POSITION is always required
    if (semantic == "POSITION") return true;
    
    // NORMAL is required for lit materials
    if (semantic == "NORMAL") {
        if (!material) return true; // Keep if no material
        // Check if material is unlit
        bool isUnlit = material->extensions.count("KHR_materials_unlit") > 0;
        return !isUnlit;
    }
    
    // TANGENT is required if material has normal map
    if (semantic == "TANGENT") {
        if (!material) return false;
        return material->normalTexture.index >= 0;
    }
    
    // TEXCOORD_n is required if material uses textures
    if (semantic.find("TEXCOORD_") == 0) {
        if (!material) return false;
        
        // Extract tex coord index
        int texCoordIdx = -1;
        try {
            texCoordIdx = std::stoi(semantic.substr(9));
        } catch (...) {
            return false;
        }
        
        // Check if any texture uses this texCoord
        auto checkTexture = [texCoordIdx](int texCoord) {
            return texCoord == texCoordIdx;
        };
        
        if (checkTexture(material->pbrMetallicRoughness.baseColorTexture.texCoord)) return true;
        if (checkTexture(material->pbrMetallicRoughness.metallicRoughnessTexture.texCoord)) return true;
        if (checkTexture(material->normalTexture.texCoord)) return true;
        if (checkTexture(material->occlusionTexture.texCoord)) return true;
        if (checkTexture(material->emissiveTexture.texCoord)) return true;
        
        return false;
    }
    
    // COLOR_0 is always kept, others can be removed
    if (semantic == "COLOR_0") return true;
    if (semantic.find("COLOR_") == 0) return false;
    
    // JOINTS and WEIGHTS are required for skinning
    if (semantic.find("JOINTS_") == 0 || semantic.find("WEIGHTS_") == 0) {
        return true;
    }
    
    // Keep everything else by default
    return true;
}

template<typename T>
std::vector<T> GltfPrune::removeUnused(const std::vector<T>& items,
                                        const std::unordered_set<int>& usedIndices) {
    std::vector<T> result;
    for (size_t i = 0; i < items.size(); ++i) {
        if (usedIndices.count(i)) {
            result.push_back(items[i]);
        }
    }
    return result;
}

std::vector<int> GltfPrune::buildIndexMap(size_t originalSize,
                                          const std::unordered_set<int>& usedIndices) {
    std::vector<int> indexMap(originalSize, -1);
    int newIndex = 0;
    for (size_t i = 0; i < originalSize; ++i) {
        if (usedIndices.count(i)) {
            indexMap[i] = newIndex++;
        }
    }
    return indexMap;
}

} // namespace gltfu
