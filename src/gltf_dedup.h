#ifndef GLTF_DEDUP_H
#define GLTF_DEDUP_H

#include "tiny_gltf.h"
#include "progress_reporter.h"
#include <string>

namespace gltfu {

/**
 * @brief GLTF Deduplicator - Removes duplicate resources to optimize file size
 * 
 * This class provides memory-efficient deduplication by:
 * - Removing duplicate accessors (vertex data, indices)
 * - Removing duplicate meshes
 * - Removing duplicate materials
 * - Removing duplicate textures/images
 * 
 * Based on the implementation from gltf-transform:
 * https://github.com/donmccurdy/glTF-Transform/blob/main/packages/functions/src/dedup.ts
 */
class GltfDedup {
public:
    struct Options {
        bool dedupAccessors;
        bool dedupMeshes;
        bool dedupMaterials;
        bool dedupTextures;
        bool keepUniqueNames;
        bool verbose;
        ProgressReporter* progressReporter;
        
        // Default constructor with default values
        Options() 
            : dedupAccessors(true)
            , dedupMeshes(true)
            , dedupMaterials(true)
            , dedupTextures(true)
            , keepUniqueNames(false)
            , verbose(false)
            , progressReporter(nullptr)
        {}
    };

    GltfDedup();
    ~GltfDedup();

    /**
     * @brief Process a GLTF model and remove duplicates
     * @param model The GLTF model to deduplicate (modified in-place)
     * @param options Deduplication options
     * @return true if successful, false otherwise
     */
    bool process(tinygltf::Model& model, const Options& options = Options());

    /**
     * @brief Get statistics about the last deduplication operation
     * @return Statistics string
     */
    std::string getStats() const;

    /**
     * @brief Get the last error message
     * @return Error message string
     */
    std::string getError() const { return errorMsg_; }

private:
    std::string errorMsg_;
    std::string stats_;

    // Deduplication methods for different resource types
    void dedupAccessors(tinygltf::Model& model, const Options& options);
    void dedupMeshes(tinygltf::Model& model, const Options& options);
    void dedupMaterials(tinygltf::Model& model, const Options& options);
    void dedupTextures(tinygltf::Model& model, const Options& options);

    // Helper methods
    bool buffersEqual(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) const;
    std::string createAccessorHash(const tinygltf::Accessor& accessor) const;
    std::string createMaterialHash(const tinygltf::Material& material) const;
};

} // namespace gltfu

#endif // GLTF_DEDUP_H
