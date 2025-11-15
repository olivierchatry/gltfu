#include "gltf_info.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

namespace gltfu {

GltfInfo::GltfInfo() {}

GltfInfo::~GltfInfo() = default;

bool GltfInfo::analyze(const std::string& filename) {
    errorMsg_.clear();
    stats_ = Stats();
    stats_.filename = filename;
    
    // Get file size
    struct stat st;
    if (stat(filename.c_str(), &st) == 0) {
        stats_.fileSize = st.st_size;
    }
    
    // Determine if binary
    stats_.isBinary = filename.size() >= 4 && 
                      filename.substr(filename.size() - 4) == ".glb";
    
    // Load the file
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    
    bool ret = false;
    if (stats_.isBinary) {
        ret = loader.LoadBinaryFromFile(&model_, &err, &warn, filename);
    } else {
        ret = loader.LoadASCIIFromFile(&model_, &err, &warn, filename);
    }
    
    if (!err.empty()) {
        errorMsg_ = err;
        return false;
    }
    
    if (!ret) {
        errorMsg_ = "Failed to load file";
        return false;
    }
    
    // Analyze the model
    analyzeModel();
    analyzeMeshes();
    analyzeMemory();
    
    return true;
}

void GltfInfo::analyzeModel() {
    // Asset info
    stats_.generator = model_.asset.generator;
    stats_.version = model_.asset.version;
    stats_.copyright = model_.asset.copyright;
    
    // Scene structure
    stats_.sceneCount = model_.scenes.size();
    stats_.defaultScene = model_.defaultScene;
    stats_.nodeCount = model_.nodes.size();
    
    // Resource counts
    stats_.meshCount = model_.meshes.size();
    stats_.materialCount = model_.materials.size();
    stats_.textureCount = model_.textures.size();
    stats_.imageCount = model_.images.size();
    stats_.samplerCount = model_.samplers.size();
    stats_.animationCount = model_.animations.size();
    stats_.skinCount = model_.skins.size();
    stats_.accessorCount = model_.accessors.size();
    stats_.bufferViewCount = model_.bufferViews.size();
    stats_.bufferCount = model_.buffers.size();
}

void GltfInfo::analyzeMeshes() {
    stats_.primitiveCount = 0;
    stats_.triangleCount = 0;
    stats_.vertexCount = 0;
    
    for (const auto& mesh : model_.meshes) {
        stats_.primitiveCount += mesh.primitives.size();
        
        for (const auto& primitive : mesh.primitives) {
            // Count vertices
            auto posIt = primitive.attributes.find("POSITION");
            if (posIt != primitive.attributes.end() && posIt->second >= 0) {
                const auto& accessor = model_.accessors[posIt->second];
                stats_.vertexCount += accessor.count;
            }
            
            // Count triangles
            if (primitive.indices >= 0) {
                const auto& accessor = model_.accessors[primitive.indices];
                int mode = primitive.mode;
                
                // TRIANGLES = 4 (default)
                if (mode == 4 || mode == -1) {
                    stats_.triangleCount += accessor.count / 3;
                }
                // TRIANGLE_STRIP = 5
                else if (mode == 5) {
                    stats_.triangleCount += accessor.count - 2;
                }
                // TRIANGLE_FAN = 6
                else if (mode == 6) {
                    stats_.triangleCount += accessor.count - 2;
                }
            } else if (posIt != primitive.attributes.end() && posIt->second >= 0) {
                // No indices, assume TRIANGLES mode
                const auto& accessor = model_.accessors[posIt->second];
                stats_.triangleCount += accessor.count / 3;
            }
        }
    }
}

void GltfInfo::analyzeMemory() {
    // Buffer memory
    stats_.bufferBytes = 0;
    for (const auto& buffer : model_.buffers) {
        stats_.bufferBytes += buffer.data.size();
    }
    
    // Image memory
    stats_.imageBytes = 0;
    for (const auto& image : model_.images) {
        stats_.imageBytes += image.image.size();
    }
    
    stats_.totalBytes = stats_.bufferBytes + stats_.imageBytes;
}

std::string GltfInfo::formatBytes(size_t bytes) const {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unit = 0;
    double size = bytes;
    
    while (size >= 1024.0 && unit < 3) {
        size /= 1024.0;
        unit++;
    }
    
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return ss.str();
}

std::string GltfInfo::formatNumber(int number) const {
    std::string str = std::to_string(number);
    int pos = str.length() - 3;
    while (pos > 0) {
        str.insert(pos, ",");
        pos -= 3;
    }
    return str;
}

std::string GltfInfo::format(bool verbose) const {
    std::ostringstream ss;
    
    // File info
    ss << "┌─────────────────────────────────────────────────────────────────\n";
    ss << "│ FILE\n";
    ss << "├─────────────────────────────────────────────────────────────────\n";
    ss << "│ " << stats_.filename << "\n";
    ss << "│ " << formatBytes(stats_.fileSize) 
       << " (" << (stats_.isBinary ? "GLB" : "GLTF") << ")\n";
    
    // Asset info
    if (!stats_.generator.empty() || !stats_.version.empty()) {
        ss << "├─────────────────────────────────────────────────────────────────\n";
        ss << "│ ASSET\n";
        ss << "├─────────────────────────────────────────────────────────────────\n";
        if (!stats_.generator.empty()) {
            ss << "│ Generator:  " << stats_.generator << "\n";
        }
        if (!stats_.version.empty()) {
            ss << "│ Version:    " << stats_.version << "\n";
        }
        if (!stats_.copyright.empty() && verbose) {
            ss << "│ Copyright:  " << stats_.copyright << "\n";
        }
    }
    
    // Scene structure
    ss << "├─────────────────────────────────────────────────────────────────\n";
    ss << "│ SCENE\n";
    ss << "├─────────────────────────────────────────────────────────────────\n";
    ss << "│ Scenes:     " << stats_.sceneCount;
    if (stats_.defaultScene >= 0) {
        ss << " (default: " << stats_.defaultScene << ")";
    }
    ss << "\n";
    ss << "│ Nodes:      " << formatNumber(stats_.nodeCount) << "\n";
    
    // Mesh info
    ss << "├─────────────────────────────────────────────────────────────────\n";
    ss << "│ MESH\n";
    ss << "├─────────────────────────────────────────────────────────────────\n";
    ss << "│ Meshes:     " << formatNumber(stats_.meshCount) << "\n";
    ss << "│ Primitives: " << formatNumber(stats_.primitiveCount) << "\n";
    ss << "│ Triangles:  " << formatNumber(stats_.triangleCount) << "\n";
    ss << "│ Vertices:   " << formatNumber(stats_.vertexCount) << "\n";
    
    // Material info
    ss << "├─────────────────────────────────────────────────────────────────\n";
    ss << "│ MATERIAL\n";
    ss << "├─────────────────────────────────────────────────────────────────\n";
    ss << "│ Materials:  " << formatNumber(stats_.materialCount) << "\n";
    ss << "│ Textures:   " << formatNumber(stats_.textureCount) << "\n";
    ss << "│ Images:     " << formatNumber(stats_.imageCount) << "\n";
    if (verbose && stats_.samplerCount > 0) {
        ss << "│ Samplers:   " << formatNumber(stats_.samplerCount) << "\n";
    }
    
    // Animation info (if present)
    if (stats_.animationCount > 0 || stats_.skinCount > 0) {
        ss << "├─────────────────────────────────────────────────────────────────\n";
        ss << "│ ANIMATION\n";
        ss << "├─────────────────────────────────────────────────────────────────\n";
        if (stats_.animationCount > 0) {
            ss << "│ Animations: " << formatNumber(stats_.animationCount) << "\n";
        }
        if (stats_.skinCount > 0) {
            ss << "│ Skins:      " << formatNumber(stats_.skinCount) << "\n";
        }
    }
    
    // Data info
    if (verbose) {
        ss << "├─────────────────────────────────────────────────────────────────\n";
        ss << "│ DATA\n";
        ss << "├─────────────────────────────────────────────────────────────────\n";
        ss << "│ Accessors:    " << formatNumber(stats_.accessorCount) << "\n";
        ss << "│ Buffer Views: " << formatNumber(stats_.bufferViewCount) << "\n";
        ss << "│ Buffers:      " << formatNumber(stats_.bufferCount) << "\n";
    }
    
    // Memory usage
    ss << "├─────────────────────────────────────────────────────────────────\n";
    ss << "│ MEMORY\n";
    ss << "├─────────────────────────────────────────────────────────────────\n";
    ss << "│ Buffers:    " << formatBytes(stats_.bufferBytes) << "\n";
    ss << "│ Images:     " << formatBytes(stats_.imageBytes) << "\n";
    ss << "│ Total:      " << formatBytes(stats_.totalBytes) << "\n";
    ss << "└─────────────────────────────────────────────────────────────────\n";
    
    return ss.str();
}

} // namespace gltfu
