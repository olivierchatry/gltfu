#ifndef GLTF_INFO_H
#define GLTF_INFO_H

#include "tiny_gltf.h"
#include <string>
#include <sstream>

namespace gltfu {

/**
 * @brief GLTF Info - Display detailed information about a GLTF file
 * 
 * Provides comprehensive information about:
 * - File size and format
 * - Asset metadata
 * - Scene structure
 * - Resource counts (meshes, materials, textures, etc.)
 * - Memory usage breakdown
 */
class GltfInfo {
public:
    struct Stats {
        // File info
        std::string filename;
        size_t fileSize;
        bool isBinary;
        
        // Asset info
        std::string generator;
        std::string version;
        std::string copyright;
        
        // Scene structure
        int sceneCount;
        int defaultScene;
        int nodeCount;
        
        // Mesh info
        int meshCount;
        int primitiveCount;
        int triangleCount;
        int vertexCount;
        
        // Material info
        int materialCount;
        int textureCount;
        int imageCount;
        int samplerCount;
        
        // Animation info
        int animationCount;
        int skinCount;
        
        // Accessor info
        int accessorCount;
        int bufferViewCount;
        int bufferCount;
        
        // Memory usage
        size_t bufferBytes;
        size_t imageBytes;
        size_t totalBytes;
        
        Stats() : fileSize(0), isBinary(false), sceneCount(0), defaultScene(-1),
                 nodeCount(0), meshCount(0), primitiveCount(0), triangleCount(0),
                 vertexCount(0), materialCount(0), textureCount(0), imageCount(0),
                 samplerCount(0), animationCount(0), skinCount(0), accessorCount(0),
                 bufferViewCount(0), bufferCount(0), bufferBytes(0), imageBytes(0),
                 totalBytes(0) {}
    };

    GltfInfo();
    ~GltfInfo();

    /**
     * @brief Analyze a GLTF file and gather statistics
     * @param filename Path to the GLTF file
     * @return true if successful, false otherwise
     */
    bool analyze(const std::string& filename);

    /**
     * @brief Get statistics from the last analysis
     */
    const Stats& getStats() const { return stats_; }

    /**
     * @brief Format statistics as a human-readable string
     * @param verbose Include detailed information
     */
    std::string format(bool verbose = false) const;

    /**
     * @brief Get the last error message
     */
    std::string getError() const { return errorMsg_; }

private:
    Stats stats_;
    tinygltf::Model model_;
    std::string errorMsg_;

    void analyzeModel();
    void analyzeMeshes();
    void analyzeMemory();
    
    std::string formatBytes(size_t bytes) const;
    std::string formatNumber(int number) const;
};

} // namespace gltfu

#endif // GLTF_INFO_H
