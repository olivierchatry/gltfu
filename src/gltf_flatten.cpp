#include "gltf_flatten.h"
#include "math_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <vector>

namespace gltfu {
namespace {

Matrix4 getNodeMatrix(const tinygltf::Node& node) {
    Matrix4 matrix = kIdentityMatrix;

    if (node.matrix.size() == 16) {
        std::copy(node.matrix.begin(), node.matrix.end(), matrix.begin());
        return matrix;
    }

    const double tx = node.translation.size() == 3 ? node.translation[0] : 0.0;
    const double ty = node.translation.size() == 3 ? node.translation[1] : 0.0;
    const double tz = node.translation.size() == 3 ? node.translation[2] : 0.0;

    const double sx = node.scale.size() == 3 ? node.scale[0] : 1.0;
    const double sy = node.scale.size() == 3 ? node.scale[1] : 1.0;
    const double sz = node.scale.size() == 3 ? node.scale[2] : 1.0;

    const double x = node.rotation.size() == 4 ? node.rotation[0] : 0.0;
    const double y = node.rotation.size() == 4 ? node.rotation[1] : 0.0;
    const double z = node.rotation.size() == 4 ? node.rotation[2] : 0.0;
    const double w = node.rotation.size() == 4 ? node.rotation[3] : 1.0;

    const double x2 = x + x;
    const double y2 = y + y;
    const double z2 = z + z;
    const double xx = x * x2;
    const double xy = x * y2;
    const double xz = x * z2;
    const double yy = y * y2;
    const double yz = y * z2;
    const double zz = z * z2;
    const double wx = w * x2;
    const double wy = w * y2;
    const double wz = w * z2;

    matrix[0] = (1.0 - (yy + zz)) * sx;
    matrix[1] = (xy + wz) * sx;
    matrix[2] = (xz - wy) * sx;
    matrix[3] = 0.0;

    matrix[4] = (xy - wz) * sy;
    matrix[5] = (1.0 - (xx + zz)) * sy;
    matrix[6] = (yz + wx) * sy;
    matrix[7] = 0.0;

    matrix[8] = (xz + wy) * sz;
    matrix[9] = (yz - wx) * sz;
    matrix[10] = (1.0 - (xx + yy)) * sz;
    matrix[11] = 0.0;

    matrix[12] = tx;
    matrix[13] = ty;
    matrix[14] = tz;
    matrix[15] = 1.0;

    return matrix;
}

void setNodeMatrix(tinygltf::Node& node, const Matrix4& matrix) {
    node.matrix.assign(matrix.begin(), matrix.end());
    node.translation.clear();
    node.rotation.clear();
    node.scale.clear();
}

} // namespace

int GltfFlatten::process(tinygltf::Model& model, bool cleanup) {
    (void)cleanup; // Reserved for future pruning logic.

    const size_t totalNodes = model.nodes.size();
    if (totalNodes == 0) {
        return 0;
    }

    const bool debug = std::getenv("GLTFU_DEBUG_FLATTEN") != nullptr;

    // Build parent lookup once.
    std::vector<int> parentMap(totalNodes, -1);
    for (size_t parent = 0; parent < totalNodes; ++parent) {
        for (int child : model.nodes[parent].children) {
            if (child >= 0 && child < static_cast<int>(totalNodes)) {
                parentMap[child] = static_cast<int>(parent);
            }
        }
    }

    // Mark joints and animated nodes (and their descendants) as off-limits.
    std::vector<bool> skip(totalNodes, false);
    std::deque<int> queue;

    auto enqueue = [&](int idx) {
        if (idx >= 0 && idx < static_cast<int>(totalNodes) && !skip[idx]) {
            skip[idx] = true;
            queue.push_back(idx);
        }
    };

    for (const auto& skin : model.skins) {
        for (int joint : skin.joints) {
            enqueue(joint);
        }
    }

    for (const auto& animation : model.animations) {
        for (const auto& channel : animation.channels) {
            if ((channel.target_path == "translation" ||
                 channel.target_path == "rotation" ||
                 channel.target_path == "scale") &&
                channel.target_node >= 0) {
                enqueue(channel.target_node);
            }
        }
    }

    while (!queue.empty()) {
        int current = queue.front();
        queue.pop_front();
        const auto& node = model.nodes[current];
        for (int child : node.children) {
            enqueue(child);
        }
    }

    // Cache scenes that reference each root node.
    std::vector<std::vector<int>> scenesForRoot(totalNodes);
    for (size_t sceneIdx = 0; sceneIdx < model.scenes.size(); ++sceneIdx) {
        for (int rootNode : model.scenes[sceneIdx].nodes) {
            if (rootNode >= 0 && rootNode < static_cast<int>(totalNodes)) {
                scenesForRoot[rootNode].push_back(static_cast<int>(sceneIdx));
            }
        }
    }

    // Compute world matrices and depth for every node.
    std::vector<Matrix4> worldMatrix(totalNodes, kIdentityMatrix);
    std::vector<char> worldComputed(totalNodes, 0);
    std::vector<int> depth(totalNodes, 0);
    std::vector<int> rootNode(totalNodes, -1);

    std::function<void(int)> computeWorld = [&](int nodeIdx) {
        if (worldComputed[nodeIdx]) {
            return;
        }

        Matrix4 local = getNodeMatrix(model.nodes[nodeIdx]);
        const int parent = parentMap[nodeIdx];
        if (parent >= 0) {
            computeWorld(parent);
            worldMatrix[nodeIdx] = multiply(worldMatrix[parent], local);
            depth[nodeIdx] = depth[parent] + 1;
            rootNode[nodeIdx] = rootNode[parent];
        } else {
            worldMatrix[nodeIdx] = local;
            depth[nodeIdx] = 0;
            rootNode[nodeIdx] = nodeIdx;
        }

        worldComputed[nodeIdx] = 1;
    };

    for (int nodeIdx = 0; nodeIdx < static_cast<int>(totalNodes); ++nodeIdx) {
        computeWorld(nodeIdx);
    }

    // Collect flatten candidates (non-root, non-constrained).
    std::vector<int> candidates;
    candidates.reserve(totalNodes);
    for (int nodeIdx = 0; nodeIdx < static_cast<int>(totalNodes); ++nodeIdx) {
        if (parentMap[nodeIdx] >= 0 && !skip[nodeIdx]) {
            candidates.push_back(nodeIdx);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [&](int a, int b) {
        return depth[a] > depth[b];
    });

    int flattenedCount = 0;

    for (int nodeIdx : candidates) {
        int parentIdx = parentMap[nodeIdx];
        if (parentIdx < 0) {
            continue;
        }

        if (debug) {
            std::cout << "Flattening node " << nodeIdx
                      << " (parent " << parentIdx
                      << ", depth " << depth[nodeIdx] << ")" << std::endl;
        }

        setNodeMatrix(model.nodes[nodeIdx], worldMatrix[nodeIdx]);

        auto& siblings = model.nodes[parentIdx].children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), nodeIdx), siblings.end());

        const auto& scenes = scenesForRoot[rootNode[nodeIdx]];
        for (int sceneIdx : scenes) {
            auto& sceneNodes = model.scenes[sceneIdx].nodes;
            if (std::find(sceneNodes.begin(), sceneNodes.end(), nodeIdx) == sceneNodes.end()) {
                sceneNodes.push_back(nodeIdx);
            }
        }

        parentMap[nodeIdx] = -1;
        ++flattenedCount;
    }

    return flattenedCount;
}

} // namespace gltfu
