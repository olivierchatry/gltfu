#ifndef GLTF_MERGER_H
#define GLTF_MERGER_H

#include "tiny_gltf.h"
#include <string>
#include <vector>
#include <memory>

namespace gltfu {

/**
 * @brief GLTF Merger - Combines multiple GLTF files and scenes into one
 * 
 * This class provides memory-efficient merging of GLTF files by:
 * - Processing files one at a time (no need to load all in memory)
 * - Using move semantics to avoid copying large buffers
 * - Immediately freeing source model memory after merging
 * - Reserving space to avoid reallocations
 */
class GltfMerger {
public:
    GltfMerger();
    ~GltfMerger();

    /**
     * @brief Load and immediately merge a GLTF file (memory efficient)
     * @param filename Path to the GLTF file
     * @param keepScenesIndependent If true, keep scenes separate; if false, merge all into one scene
     * @param defaultScenesOnly If true, only merge default scene from each file; if false, merge all scenes
     * @return true if successful, false otherwise
     */
    bool loadAndMergeFile(const std::string& filename, bool keepScenesIndependent = false, bool defaultScenesOnly = false);

    /**
     * @brief Save the merged model to a file
     * @param filename Output filename
     * @param embedImages Whether to embed images in the output
     * @param embedBuffers Whether to embed buffers in the output
     * @param prettyPrint Whether to pretty-print JSON
     * @param writeBinary Whether to write in binary format (.glb)
     * @return true if successful, false otherwise
     */
    bool save(const std::string& filename, 
              bool embedImages = false, 
              bool embedBuffers = false,
              bool prettyPrint = true,
              bool writeBinary = false);

    /**
     * @brief Get the last error message
     * @return Error message string
     */
    std::string getError() const { return errorMsg_; }

    /**
     * @brief Get a copy of the merged model for further processing
     * @return Copy of the merged model
     */
    tinygltf::Model getMergedModel() const;

    /**
     * @brief Clear all loaded data
     */
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string errorMsg_;

    // Helper methods
    bool mergeModel(tinygltf::Model&& model, bool mergeScenes);
    bool mergeModelStreaming(tinygltf::Model&& model, bool keepScenesIndependent, bool defaultScenesOnly);
    void updateIndices(tinygltf::Model& target, const tinygltf::Model& source,
                      int nodeOffset, int meshOffset, int materialOffset,
                      int textureOffset, int imageOffset, int samplerOffset,
                      int accessorOffset, int bufferViewOffset, int bufferOffset);
};

} // namespace gltfu

#endif // GLTF_MERGER_H
