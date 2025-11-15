#include "gltf_dedup.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <algorithm>

namespace gltfu {

GltfDedup::GltfDedup() {}

GltfDedup::~GltfDedup() = default;

bool GltfDedup::process(tinygltf::Model& model, const Options& options) {
    errorMsg_.clear();
    stats_.clear();
    
    try {
        if (options.dedupAccessors) {
            dedupAccessors(model, options);
        }
        
        if (options.dedupTextures) {
            dedupTextures(model, options);
        }
        
        if (options.dedupMaterials) {
            dedupMaterials(model, options);
        }
        
        if (options.dedupMeshes) {
            dedupMeshes(model, options);
        }
        
        return true;
    } catch (const std::exception& e) {
        errorMsg_ = std::string("Deduplication failed: ") + e.what();
        return false;
    }
}

std::string GltfDedup::getStats() const {
    return stats_;
}

bool GltfDedup::buffersEqual(const std::vector<unsigned char>& a, 
                             const std::vector<unsigned char>& b) const {
    if (a.size() != b.size()) return false;
    return std::memcmp(a.data(), b.data(), a.size()) == 0;
}

std::string GltfDedup::createAccessorHash(const tinygltf::Accessor& accessor) const {
    std::stringstream ss;
    ss << accessor.count << ":"
       << accessor.type << ":"
       << accessor.componentType << ":"
       << accessor.normalized << ":"
       << accessor.sparse.isSparse;
    return ss.str();
}

std::string GltfDedup::createMaterialHash(const tinygltf::Material& material) const {
    std::stringstream ss;
    
    // Hash PBR metallic roughness
    const auto& pbr = material.pbrMetallicRoughness;
    for (double v : pbr.baseColorFactor) ss << v << ";";
    ss << pbr.baseColorTexture.index << ";";
    ss << pbr.metallicFactor << ";";
    ss << pbr.roughnessFactor << ";";
    ss << pbr.metallicRoughnessTexture.index << ";";
    
    // Hash other properties
    ss << material.normalTexture.index << ";";
    ss << material.occlusionTexture.index << ";";
    ss << material.emissiveTexture.index << ";";
    for (double v : material.emissiveFactor) ss << v << ";";
    ss << material.alphaMode << ";";
    ss << material.alphaCutoff << ";";
    ss << material.doubleSided << ";";
    
    return ss.str();
}

void GltfDedup::dedupAccessors(tinygltf::Model& model, const Options& options) {
    const size_t originalCount = model.accessors.size();
    
    auto reportProgress = [&](const std::string& message, double progress = -1.0, const std::string& details = "") {
        if (options.progressReporter) {
            options.progressReporter->report("dedupe-accessors", message, progress, details);
        } else if (options.verbose) {
            std::cout << message;
            if (!details.empty()) std::cout << " - " << details;
            std::cout << std::endl;
        }
    };
    
    reportProgress("Deduplicating accessors", 0.0, std::to_string(originalCount) + " total");
    
    // Group accessors by metadata hash AND compute content hash for each
    std::unordered_map<std::string, std::vector<int>> hashGroups;
    std::unordered_map<int, uint64_t> contentHashes; // accessor index -> content hash
    
    reportProgress("Computing content hashes", 0.1);
    
    for (size_t i = 0; i < model.accessors.size(); ++i) {
        if (i % 10000 == 0 && i > 0) {
            double hashProgress = 0.1 + 0.3 * static_cast<double>(i) / model.accessors.size();
            reportProgress("Hashed " + std::to_string(i) + "/" + std::to_string(model.accessors.size()) + " accessors", hashProgress);
        }
        
        const auto& accessor = model.accessors[i];
        
        // Create metadata hash
        std::string metaHash = createAccessorHash(accessor);
        hashGroups[metaHash].push_back(i);
        
        // Compute content hash
        if (accessor.bufferView >= 0 && accessor.bufferView < (int)model.bufferViews.size()) {
            const auto& bufferView = model.bufferViews[accessor.bufferView];
            if (bufferView.buffer >= 0 && bufferView.buffer < (int)model.buffers.size()) {
                const auto& buffer = model.buffers[bufferView.buffer];
                
                size_t offset = bufferView.byteOffset + accessor.byteOffset;
                size_t stride = bufferView.byteStride > 0 ? bufferView.byteStride : 
                               tinygltf::GetComponentSizeInBytes(accessor.componentType) * 
                               tinygltf::GetNumComponentsInType(accessor.type);
                
                // Hash the actual data
                if (bufferView.byteStride == 0 || bufferView.byteStride == stride) {
                    // Contiguous data - hash directly
                    size_t size = accessor.count * stride;
                    if (offset + size <= buffer.data.size()) {
                        contentHashes[i] = XXH64(&buffer.data[offset], size, 0);
                    }
                } else {
                    // Non-contiguous - need to hash element by element
                    XXH64_state_t* state = XXH64_createState();
                    XXH64_reset(state, 0);
                    
                    size_t elemSize = tinygltf::GetComponentSizeInBytes(accessor.componentType) *
                                     tinygltf::GetNumComponentsInType(accessor.type);
                    
                    for (size_t k = 0; k < static_cast<size_t>(accessor.count); ++k) {
                        size_t elemOffset = offset + k * stride;
                        if (elemOffset + elemSize <= buffer.data.size()) {
                            XXH64_update(state, &buffer.data[elemOffset], elemSize);
                        }
                    }
                    
                    contentHashes[i] = XXH64_digest(state);
                    XXH64_freeState(state);
                }
            }
        }
    }
    
    reportProgress("Grouped into " + std::to_string(hashGroups.size()) + " metadata buckets", 0.4);
    reportProgress("Finding duplicates using content hashes", 0.5);
    
    // Map of duplicate accessor index -> original accessor index
    std::unordered_map<int, int> duplicates;
    
    // For each metadata group, use content hashes to find duplicates
    size_t groupsProcessed = 0;
    
    for (const auto& [metaHash, indices] : hashGroups) {
        if (indices.size() < 2) continue;
        
        groupsProcessed++;
        if (groupsProcessed % 100 == 0 || indices.size() > 1000) {
            double groupProgress = 0.5 + 0.3 * static_cast<double>(groupsProcessed) / hashGroups.size();
            reportProgress("Group " + std::to_string(groupsProcessed) + "/" + std::to_string(hashGroups.size()), 
                         groupProgress, std::to_string(indices.size()) + " accessors");
        }
        
        // Group by content hash within this metadata group
        std::unordered_map<uint64_t, int> contentHashToFirst;
        
        for (int idx : indices) {
            if (duplicates.count(idx)) continue;
            
            uint64_t hash = contentHashes[idx];
            
            if (contentHashToFirst.count(hash)) {
                // Found a duplicate!
                duplicates[idx] = contentHashToFirst[hash];
            } else {
                // First occurrence with this content hash
                contentHashToFirst[hash] = idx;
            }
        }
    }
    
    reportProgress("Found " + std::to_string(duplicates.size()) + " duplicates", 0.8);
    
    if (duplicates.empty()) {
        if (options.verbose) {
            reportProgress("Accessors: No duplicates found", 1.0, std::to_string(originalCount) + " total");
        }
        return;
    }
    
    // Update all references to accessors
    // Update mesh primitives
    for (auto& mesh : model.meshes) {
        for (auto& prim : mesh.primitives) {
            // Update attributes
            for (auto& [name, accessorIdx] : prim.attributes) {
                if (duplicates.count(accessorIdx)) {
                    accessorIdx = duplicates[accessorIdx];
                }
            }
            
            // Update indices
            if (prim.indices >= 0 && duplicates.count(prim.indices)) {
                prim.indices = duplicates[prim.indices];
            }
            
            // Update morph targets
            for (auto& target : prim.targets) {
                for (auto& [name, accessorIdx] : target) {
                    if (duplicates.count(accessorIdx)) {
                        accessorIdx = duplicates[accessorIdx];
                    }
                }
            }
        }
    }
    
    // Update animations
    for (auto& animation : model.animations) {
        for (auto& sampler : animation.samplers) {
            if (duplicates.count(sampler.input)) {
                sampler.input = duplicates[sampler.input];
            }
            if (duplicates.count(sampler.output)) {
                sampler.output = duplicates[sampler.output];
            }
        }
    }
    
    // Update skins
    for (auto& skin : model.skins) {
        if (skin.inverseBindMatrices >= 0 && duplicates.count(skin.inverseBindMatrices)) {
            skin.inverseBindMatrices = duplicates[skin.inverseBindMatrices];
        }
    }
    
    // Create index remapping (account for removed accessors)
    std::vector<int> indexRemap(model.accessors.size());
    int newIndex = 0;
    for (size_t i = 0; i < model.accessors.size(); ++i) {
        if (duplicates.count(i)) {
            indexRemap[i] = -1;  // Will be removed
        } else {
            indexRemap[i] = newIndex++;
        }
    }
    
    // Remove duplicate accessors
    std::vector<tinygltf::Accessor> newAccessors;
    newAccessors.reserve(model.accessors.size() - duplicates.size());
    for (size_t i = 0; i < model.accessors.size(); ++i) {
        if (!duplicates.count(i)) {
            newAccessors.push_back(model.accessors[i]);
        }
    }
    model.accessors = std::move(newAccessors);
    
    // Update all references with new indices
    auto updateIndex = [&indexRemap, &duplicates](int& idx) {
        if (idx < 0) return;
        if (duplicates.count(idx)) {
            idx = indexRemap[duplicates[idx]];
        } else {
            idx = indexRemap[idx];
        }
    };
    
    for (auto& mesh : model.meshes) {
        for (auto& prim : mesh.primitives) {
            for (auto& [name, accessorIdx] : prim.attributes) {
                updateIndex(accessorIdx);
            }
            updateIndex(prim.indices);
            for (auto& target : prim.targets) {
                for (auto& [name, accessorIdx] : target) {
                    updateIndex(accessorIdx);
                }
            }
        }
    }
    
    for (auto& animation : model.animations) {
        for (auto& sampler : animation.samplers) {
            updateIndex(sampler.input);
            updateIndex(sampler.output);
        }
    }
    
    for (auto& skin : model.skins) {
        updateIndex(skin.inverseBindMatrices);
    }
    
    std::stringstream ss;
    ss << "Accessors: Merged " << duplicates.size() << " of " << originalCount 
       << " (" << model.accessors.size() << " remaining)";
    stats_ += ss.str() + "\n";
    
    if (options.verbose) {
        std::cout << ss.str() << std::endl;
    }
}

void GltfDedup::dedupMeshes(tinygltf::Model& model, const Options& options) {
    const size_t originalCount = model.meshes.size();
    
    auto reportProgress = [&](const std::string& message, double progress = -1.0, const std::string& details = "") {
        if (options.progressReporter) {
            options.progressReporter->report("dedupe-meshes", message, progress, details);
        } else if (options.verbose) {
            std::cout << message;
            if (!details.empty()) std::cout << " - " << details;
            std::cout << std::endl;
        }
    };
    
    reportProgress("Deduplicating meshes", 0.0, std::to_string(originalCount) + " total");
    
    // Create hash for each mesh based on primitives
    std::unordered_map<std::string, int> uniqueMeshes;
    std::unordered_map<int, int> duplicates;  // duplicate index -> original index
    
    for (size_t i = 0; i < model.meshes.size(); ++i) {
        const auto& mesh = model.meshes[i];
        
        std::stringstream meshKey;
        
        if (options.keepUniqueNames && !mesh.name.empty()) {
            meshKey << mesh.name << ";";
        }
        
        // Create key from primitives
        for (const auto& prim : mesh.primitives) {
            meshKey << "mode:" << prim.mode << ";";
            meshKey << "material:" << prim.material << ";";
            meshKey << "indices:" << prim.indices << ";";
            
            // Sort attributes for consistent hashing
            std::vector<std::pair<std::string, int>> sortedAttrs(prim.attributes.begin(), prim.attributes.end());
            std::sort(sortedAttrs.begin(), sortedAttrs.end());
            
            for (const auto& [name, idx] : sortedAttrs) {
                meshKey << name << ":" << idx << ";";
            }
            
            // Morph targets
            for (const auto& target : prim.targets) {
                std::vector<std::pair<std::string, int>> sortedTargetAttrs(target.begin(), target.end());
                std::sort(sortedTargetAttrs.begin(), sortedTargetAttrs.end());
                
                meshKey << "target:[";
                for (const auto& [name, idx] : sortedTargetAttrs) {
                    meshKey << name << ":" << idx << ";";
                }
                meshKey << "]";
            }
            
            meshKey << "|";
        }
        
        std::string key = meshKey.str();
        
        if (uniqueMeshes.count(key)) {
            duplicates[i] = uniqueMeshes[key];
        } else {
            uniqueMeshes[key] = i;
        }
    }
    
    if (duplicates.empty()) {
        if (options.verbose) {
            reportProgress("Meshes: No duplicates found", 1.0, std::to_string(originalCount) + " total");
        }
        return;
    }
    
    // Update all references to meshes (in nodes)
    for (auto& node : model.nodes) {
        if (node.mesh >= 0 && duplicates.count(node.mesh)) {
            node.mesh = duplicates[node.mesh];
        }
    }
    
    // Create index remapping
    std::vector<int> indexRemap(model.meshes.size());
    int newIndex = 0;
    for (size_t i = 0; i < model.meshes.size(); ++i) {
        if (duplicates.count(i)) {
            indexRemap[i] = -1;
        } else {
            indexRemap[i] = newIndex++;
        }
    }
    
    // Remove duplicate meshes
    std::vector<tinygltf::Mesh> newMeshes;
    newMeshes.reserve(model.meshes.size() - duplicates.size());
    for (size_t i = 0; i < model.meshes.size(); ++i) {
        if (!duplicates.count(i)) {
            newMeshes.push_back(model.meshes[i]);
        }
    }
    model.meshes = std::move(newMeshes);
    
    // Update mesh indices in nodes
    for (auto& node : model.nodes) {
        if (node.mesh >= 0) {
            if (duplicates.count(node.mesh)) {
                node.mesh = indexRemap[duplicates[node.mesh]];
            } else {
                node.mesh = indexRemap[node.mesh];
            }
        }
    }
    
    std::stringstream ss;
    ss << "Meshes: Merged " << duplicates.size() << " of " << originalCount
       << " (" << model.meshes.size() << " remaining)";
    stats_ += ss.str() + "\n";
    
    if (options.verbose) {
        std::cout << ss.str() << std::endl;
    }
}

void GltfDedup::dedupMaterials(tinygltf::Model& model, const Options& options) {
    const size_t originalCount = model.materials.size();
    
    auto reportProgress = [&](const std::string& message, double progress = -1.0, const std::string& details = "") {
        if (options.progressReporter) {
            options.progressReporter->report("dedupe-materials", message, progress, details);
        } else if (options.verbose) {
            std::cout << message;
            if (!details.empty()) std::cout << " - " << details;
            std::cout << std::endl;
        }
    };
    
    reportProgress("Deduplicating materials", 0.0, std::to_string(originalCount) + " total");
    
    std::unordered_map<std::string, int> uniqueMaterials;
    std::unordered_map<int, int> duplicates;
    
    for (size_t i = 0; i < model.materials.size(); ++i) {
        const auto& mat = model.materials[i];
        
        std::string key;
        if (options.keepUniqueNames && !mat.name.empty()) {
            key = mat.name + ";" + createMaterialHash(mat);
        } else {
            key = createMaterialHash(mat);
        }
        
        if (uniqueMaterials.count(key)) {
            duplicates[i] = uniqueMaterials[key];
        } else {
            uniqueMaterials[key] = i;
        }
    }
    
    if (duplicates.empty()) {
        if (options.verbose) {
            reportProgress("Materials: No duplicates found", 1.0, std::to_string(originalCount) + " total");
        }
        return;
    }
    
    // Update references in meshes
    for (auto& mesh : model.meshes) {
        for (auto& prim : mesh.primitives) {
            if (prim.material >= 0 && duplicates.count(prim.material)) {
                prim.material = duplicates[prim.material];
            }
        }
    }
    
    // Create index remapping
    std::vector<int> indexRemap(model.materials.size());
    int newIndex = 0;
    for (size_t i = 0; i < model.materials.size(); ++i) {
        if (duplicates.count(i)) {
            indexRemap[i] = -1;
        } else {
            indexRemap[i] = newIndex++;
        }
    }
    
    // Remove duplicates
    std::vector<tinygltf::Material> newMaterials;
    newMaterials.reserve(model.materials.size() - duplicates.size());
    for (size_t i = 0; i < model.materials.size(); ++i) {
        if (!duplicates.count(i)) {
            newMaterials.push_back(model.materials[i]);
        }
    }
    model.materials = std::move(newMaterials);
    
    // Update indices
    for (auto& mesh : model.meshes) {
        for (auto& prim : mesh.primitives) {
            if (prim.material >= 0) {
                if (duplicates.count(prim.material)) {
                    prim.material = indexRemap[duplicates[prim.material]];
                } else {
                    prim.material = indexRemap[prim.material];
                }
            }
        }
    }
    
    std::stringstream ss;
    ss << "Materials: Merged " << duplicates.size() << " of " << originalCount
       << " (" << model.materials.size() << " remaining)";
    stats_ += ss.str() + "\n";
    
    if (options.verbose) {
        std::cout << ss.str() << std::endl;
    }
}

void GltfDedup::dedupTextures(tinygltf::Model& model, const Options& options) {
    const size_t originalImageCount = model.images.size();
    const size_t originalTextureCount = model.textures.size();
    
    auto reportProgress = [&](const std::string& message, double progress = -1.0, const std::string& details = "") {
        if (options.progressReporter) {
            options.progressReporter->report("dedupe-textures", message, progress, details);
        } else if (options.verbose) {
            std::cout << message;
            if (!details.empty()) std::cout << " - " << details;
            std::cout << std::endl;
        }
    };
    
    reportProgress("Deduplicating images", 0.0, std::to_string(originalImageCount) + " total");
    
    // Deduplicate images first
    std::unordered_map<int, int> imageDuplicates;
    
    for (size_t i = 0; i < model.images.size(); ++i) {
        if (imageDuplicates.count(i)) continue;
        
        if (i % 100 == 0 && i > 0) {
            double imgProgress = 0.3 * static_cast<double>(i) / model.images.size();
            reportProgress("Processed " + std::to_string(i) + "/" + std::to_string(model.images.size()) + " images", imgProgress);
        }
        
        const auto& imgA = model.images[i];
        
        for (size_t j = i + 1; j < model.images.size(); ++j) {
            if (imageDuplicates.count(j)) continue;
            
            const auto& imgB = model.images[j];
            
            if (imgA.mimeType != imgB.mimeType) continue;
            if (options.keepUniqueNames && imgA.name != imgB.name) continue;
            if (imgA.width != imgB.width || imgA.height != imgB.height) continue;
            
            if (buffersEqual(imgA.image, imgB.image)) {
                imageDuplicates[j] = i;
            }
        }
    }
    
    // Update texture references to images
    for (auto& texture : model.textures) {
        if (texture.source >= 0 && imageDuplicates.count(texture.source)) {
            texture.source = imageDuplicates[texture.source];
        }
    }
    
    // Create image index remapping
    std::vector<int> imageIndexRemap(model.images.size());
    int newImageIndex = 0;
    for (size_t i = 0; i < model.images.size(); ++i) {
        if (imageDuplicates.count(i)) {
            imageIndexRemap[i] = -1;
        } else {
            imageIndexRemap[i] = newImageIndex++;
        }
    }
    
    // Remove duplicate images
    std::vector<tinygltf::Image> newImages;
    newImages.reserve(model.images.size() - imageDuplicates.size());
    for (size_t i = 0; i < model.images.size(); ++i) {
        if (!imageDuplicates.count(i)) {
            newImages.push_back(model.images[i]);
        }
    }
    model.images = std::move(newImages);
    
    // Update image indices in textures
    for (auto& texture : model.textures) {
        if (texture.source >= 0) {
            if (imageDuplicates.count(texture.source)) {
                texture.source = imageIndexRemap[imageDuplicates[texture.source]];
            } else {
                texture.source = imageIndexRemap[texture.source];
            }
        }
    }
    
    // Now deduplicate textures
    std::unordered_map<int, int> textureDuplicates;
    
    for (size_t i = 0; i < model.textures.size(); ++i) {
        if (textureDuplicates.count(i)) continue;
        
        const auto& texA = model.textures[i];
        
        for (size_t j = i + 1; j < model.textures.size(); ++j) {
            if (textureDuplicates.count(j)) continue;
            
            const auto& texB = model.textures[j];
            
            if (options.keepUniqueNames && texA.name != texB.name) continue;
            if (texA.source != texB.source) continue;
            if (texA.sampler != texB.sampler) continue;
            
            textureDuplicates[j] = i;
        }
    }
    
    // Update texture references in materials
    auto updateTextureInfo = [&textureDuplicates](tinygltf::TextureInfo& info) {
        if (info.index >= 0 && textureDuplicates.count(info.index)) {
            info.index = textureDuplicates[info.index];
        }
    };
    
    auto updateNormalTextureInfo = [&textureDuplicates](tinygltf::NormalTextureInfo& info) {
        if (info.index >= 0 && textureDuplicates.count(info.index)) {
            info.index = textureDuplicates[info.index];
        }
    };
    
    auto updateOcclusionTextureInfo = [&textureDuplicates](tinygltf::OcclusionTextureInfo& info) {
        if (info.index >= 0 && textureDuplicates.count(info.index)) {
            info.index = textureDuplicates[info.index];
        }
    };
    
    for (auto& material : model.materials) {
        updateTextureInfo(material.pbrMetallicRoughness.baseColorTexture);
        updateTextureInfo(material.pbrMetallicRoughness.metallicRoughnessTexture);
        updateNormalTextureInfo(material.normalTexture);
        updateOcclusionTextureInfo(material.occlusionTexture);
        updateTextureInfo(material.emissiveTexture);
    }
    
    // Create texture index remapping
    std::vector<int> textureIndexRemap(model.textures.size());
    int newTextureIndex = 0;
    for (size_t i = 0; i < model.textures.size(); ++i) {
        if (textureDuplicates.count(i)) {
            textureIndexRemap[i] = -1;
        } else {
            textureIndexRemap[i] = newTextureIndex++;
        }
    }
    
    // Remove duplicate textures
    std::vector<tinygltf::Texture> newTextures;
    newTextures.reserve(model.textures.size() - textureDuplicates.size());
    for (size_t i = 0; i < model.textures.size(); ++i) {
        if (!textureDuplicates.count(i)) {
            newTextures.push_back(model.textures[i]);
        }
    }
    model.textures = std::move(newTextures);
    
    // Update texture indices in materials
    auto updateTextureInfoFinal = [&textureIndexRemap, &textureDuplicates](tinygltf::TextureInfo& info) {
        if (info.index >= 0) {
            if (textureDuplicates.count(info.index)) {
                info.index = textureIndexRemap[textureDuplicates[info.index]];
            } else {
                info.index = textureIndexRemap[info.index];
            }
        }
    };
    
    auto updateNormalTextureInfoFinal = [&textureIndexRemap, &textureDuplicates](tinygltf::NormalTextureInfo& info) {
        if (info.index >= 0) {
            if (textureDuplicates.count(info.index)) {
                info.index = textureIndexRemap[textureDuplicates[info.index]];
            } else {
                info.index = textureIndexRemap[info.index];
            }
        }
    };
    
    auto updateOcclusionTextureInfoFinal = [&textureIndexRemap, &textureDuplicates](tinygltf::OcclusionTextureInfo& info) {
        if (info.index >= 0) {
            if (textureDuplicates.count(info.index)) {
                info.index = textureIndexRemap[textureDuplicates[info.index]];
            } else {
                info.index = textureIndexRemap[info.index];
            }
        }
    };
    
    for (auto& material : model.materials) {
        updateTextureInfoFinal(material.pbrMetallicRoughness.baseColorTexture);
        updateTextureInfoFinal(material.pbrMetallicRoughness.metallicRoughnessTexture);
        updateNormalTextureInfoFinal(material.normalTexture);
        updateOcclusionTextureInfoFinal(material.occlusionTexture);
        updateTextureInfoFinal(material.emissiveTexture);
    }
    
    std::stringstream ss;
    ss << "Images: Merged " << imageDuplicates.size() << " of " << originalImageCount
       << " (" << model.images.size() << " remaining)\n";
    ss << "Textures: Merged " << textureDuplicates.size() << " of " << originalTextureCount
       << " (" << model.textures.size() << " remaining)";
    stats_ += ss.str() + "\n";
    
    if (options.verbose) {
        std::cout << ss.str() << std::endl;
    }
}

} // namespace gltfu
