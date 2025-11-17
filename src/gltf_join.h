#ifndef GLTF_JOIN_H
#define GLTF_JOIN_H

#include "tiny_gltf.h"

#include <string>

namespace gltfu {

struct JoinOptions {
    bool keepMeshes = false;
    bool keepNamed = false;
    bool verbose = false;
};

class GltfJoin {
public:
    GltfJoin();
    ~GltfJoin();

    bool process(tinygltf::Model& model, const JoinOptions& options = JoinOptions());
    std::string getStats() const { return stats_; }
    std::string getError() const { return error_; }

private:
    std::string error_;
    std::string stats_;
};

} // namespace gltfu

#endif // GLTF_JOIN_H
