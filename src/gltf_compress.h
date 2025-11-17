#pragma once

#include "tiny_gltf.h"
#include <string>

namespace gltfu {

/**
 * Options for Draco compression
 */
struct CompressOptions {
    // Quantization bits for position attribute (10-14 recommended)
    int positionQuantizationBits = 14;
    
    // Quantization bits for normal attribute (8-10 recommended)
    int normalQuantizationBits = 10;
    
    // Quantization bits for texture coordinate attribute (10-12 recommended)
    int texCoordQuantizationBits = 12;
    
    // Quantization bits for color attribute (8-10 recommended)
    int colorQuantizationBits = 8;
    
    // Quantization bits for generic attributes
    int genericQuantizationBits = 8;
    
    // Encoding speed (0-10, 0=slowest/best compression, 10=fastest/worst compression)
    int encodingSpeed = 5;
    
    // Decoding speed (0-10, 0=slowest, 10=fastest)
    int decodingSpeed = 5;
    
    // Compression level (0-10, 0=fastest, 10=smallest)
    int compressionLevel = 7;
    
    // Use edgebreaker encoding for meshes (better compression for triangle meshes)
    bool useEdgebreaker = true;
    
    // Verbose output
    bool verbose = false;
    
    // Constructor with default values
    CompressOptions() = default;
};

/**
 * Class responsible for compressing glTF meshes using Draco compression
 * 
 * This class compresses mesh geometry using Google's Draco compression library,
 * significantly reducing file sizes while maintaining visual quality. It adds
 * the KHR_draco_mesh_compression extension to the glTF file.
 */
class GltfCompress {
public:
    GltfCompress() = default;
    ~GltfCompress() = default;

    /**
     * Compress all meshes in the model using Draco compression
     * @param model The glTF model to compress
     * @param options Compression options
     * @return true on success
     */
    bool process(tinygltf::Model& model, const CompressOptions& options = CompressOptions());

    /**
     * Get the last error message
     */
    const std::string& getError() const { return error_; }

    /**
     * Get compression statistics
     */
    const std::string& getStats() const { return stats_; }

private:
    std::string error_;
    std::string stats_;
};

} // namespace gltfu
