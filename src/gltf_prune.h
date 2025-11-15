#pragma once

#include "tiny_gltf.h"
#include <string>
#include <vector>
#include <unordered_set>

namespace gltfu {

/**
 * Options for the prune operation.
 */
struct PruneOptions {
    bool keepLeaves = false;        // Keep empty leaf nodes
    bool keepAttributes = false;     // Keep unused vertex attributes
    bool keepExtras = false;         // Prevent pruning properties with custom extras
};

/**
 * Prune removes properties from the file if they are not referenced by a Scene.
 * This includes unused nodes, meshes, materials, textures, accessors, and buffers.
 * 
 * Unlike deduplicate which removes duplicates, prune removes anything completely
 * unused (not reachable from any scene).
 */
class GltfPrune {
public:
    GltfPrune() = default;
    
    /**
     * Process the model and remove all unused properties.
     * @param model The GLTF model to prune
     * @param options Pruning options
     * @return true if successful
     */
    bool process(tinygltf::Model& model, const PruneOptions& options = PruneOptions());
    
private:
    // Mark all resources reachable from scenes
    void markReachableFromScenes(const tinygltf::Model& model,
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
                                   std::unordered_set<int>& usedCameras);
    
    // Mark all resources used by animations
    void markAnimationResources(const tinygltf::Model& model,
                                 std::unordered_set<int>& usedNodes,
                                 std::unordered_set<int>& usedAccessors,
                                 std::unordered_set<int>& usedBufferViews,
                                 std::unordered_set<int>& usedBuffers);
    
    // Recursively mark node hierarchy
    void markNode(int nodeIdx,
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
                  std::unordered_set<int>& usedCameras);
    
    // Mark mesh and all its dependencies
    void markMesh(int meshIdx,
                  const tinygltf::Model& model,
                  std::unordered_set<int>& usedMaterials,
                  std::unordered_set<int>& usedAccessors,
                  std::unordered_set<int>& usedTextures,
                  std::unordered_set<int>& usedImages,
                  std::unordered_set<int>& usedSamplers,
                  std::unordered_set<int>& usedBufferViews,
                  std::unordered_set<int>& usedBuffers);
    
    // Mark material and all its dependencies
    void markMaterial(int materialIdx,
                      const tinygltf::Model& model,
                      std::unordered_set<int>& usedTextures,
                      std::unordered_set<int>& usedImages,
                      std::unordered_set<int>& usedSamplers);
    
    // Mark texture and its dependencies
    void markTexture(int textureIdx,
                     const tinygltf::Model& model,
                     std::unordered_set<int>& usedImages,
                     std::unordered_set<int>& usedSamplers);
    
    // Mark accessor and its dependencies
    void markAccessor(int accessorIdx,
                      const tinygltf::Model& model,
                      std::unordered_set<int>& usedBufferViews,
                      std::unordered_set<int>& usedBuffers);
    
    // Mark skin and its dependencies
    void markSkin(int skinIdx,
                  const tinygltf::Model& model,
                  std::unordered_set<int>& usedNodes,
                  std::unordered_set<int>& usedAccessors,
                  std::unordered_set<int>& usedBufferViews,
                  std::unordered_set<int>& usedBuffers);
    
    // Prune empty leaf nodes
    void pruneEmptyLeafNodes(tinygltf::Model& model, const PruneOptions& options);
    
    // Prune unused vertex attributes
    void pruneUnusedAttributes(tinygltf::Model& model);
    
    // Check if semantic is required by material
    bool isSemanticRequired(const std::string& semantic,
                            const tinygltf::Material* material,
                            const tinygltf::Model& model) const;
    
    // Remove items by building new arrays
    template<typename T>
    std::vector<T> removeUnused(const std::vector<T>& items,
                                const std::unordered_set<int>& usedIndices);
    
    // Update indices after removal
    std::vector<int> buildIndexMap(size_t originalSize,
                                   const std::unordered_set<int>& usedIndices);
};

} // namespace gltfu
