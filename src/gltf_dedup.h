#ifndef GLTF_DEDUP_H
#define GLTF_DEDUP_H

#include "tiny_gltf.h"

#include <string>

namespace gltfu {

class ProgressReporter;

struct DedupOptions {
    bool dedupAccessors = true;
    bool dedupMeshes = true;
    bool dedupMaterials = true;
    bool dedupTextures = true;
    bool keepUniqueNames = false;
    bool verbose = false;
    ProgressReporter* progressReporter = nullptr;
};

class GltfDedup {
public:
    GltfDedup();
    ~GltfDedup();

    bool process(tinygltf::Model& model, const DedupOptions& options = DedupOptions());
    std::string getStats() const { return stats_; }
    std::string getError() const { return error_; }

private:
    std::string error_;
    std::string stats_;
};

} // namespace gltfu

#endif // GLTF_DEDUP_H
