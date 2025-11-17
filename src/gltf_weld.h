#pragma once

#include "tiny_gltf.h"

namespace gltfu {

/**
 * Options for the weld operation
 */
struct WeldOptions {
    // Whether to overwrite existing indices
    bool overwrite = true;
    
    // Verbose output
    bool verbose = false;
    
    // Constructor with default values
    WeldOptions() = default;
};

class GltfWeld {
public:
    bool process(tinygltf::Model& model, const WeldOptions& options = WeldOptions());
};

} // namespace gltfu
