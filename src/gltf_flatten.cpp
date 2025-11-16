#include "gltf_flatten.h"
#include <iostream>
#include <set>
#include <map>
#include <cmath>
#include <algorithm>

namespace gltfu {

// Helper: Create identity matrix
static std::vector<double> identityMatrix() {
    return {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
}

// Helper: Matrix decomposition (extract translation from matrix)
static void decomposeMatrix(const std::vector<double>& mat, 
                           std::vector<double>& translation,
                           std::vector<double>& rotation,
                           std::vector<double>& scale) {
    // Extract translation (rightmost column)
    translation = {mat[12], mat[13], mat[14]};
    
    // Extract scale with proper sign handling
    double sx = std::sqrt(mat[0]*mat[0] + mat[1]*mat[1] + mat[2]*mat[2]);
    double sy = std::sqrt(mat[4]*mat[4] + mat[5]*mat[5] + mat[6]*mat[6]);
    double sz = std::sqrt(mat[8]*mat[8] + mat[9]*mat[9] + mat[10]*mat[10]);
    
    // Check for negative scale using determinant
    // det(M) = det(R) * sx * sy * sz, and det(R) = 1 for pure rotation
    // So if det(M) < 0, one of the scales must be negative
    double det = mat[0] * (mat[5]*mat[10] - mat[6]*mat[9])
               - mat[1] * (mat[4]*mat[10] - mat[6]*mat[8])
               + mat[2] * (mat[4]*mat[9] - mat[5]*mat[8]);
    
    if (det < 0) {
        sz = -sz; // By convention, negate Z scale for negative determinant
    }
    
    scale = {sx, sy, sz};
    
    // Extract rotation (normalized rotation matrix)
    // Initialize to identity to handle zero scale cases
    std::vector<double> rotMat = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    
    if (std::abs(sx) > 1e-10) { 
        rotMat[0] = mat[0]/sx; rotMat[1] = mat[1]/sx; rotMat[2] = mat[2]/sx; 
    }
    if (std::abs(sy) > 1e-10) { 
        rotMat[3] = mat[4]/sy; rotMat[4] = mat[5]/sy; rotMat[5] = mat[6]/sy; 
    }
    if (std::abs(sz) > 1e-10) { 
        rotMat[6] = mat[8]/sz; rotMat[7] = mat[9]/sz; rotMat[8] = mat[10]/sz; 
    }
    
    // Convert rotation matrix to quaternion
    double trace = rotMat[0] + rotMat[4] + rotMat[8];
    if (trace > 0) {
        double s = 0.5 / std::sqrt(trace + 1.0);
        rotation = {
            (rotMat[7] - rotMat[5]) * s,
            (rotMat[2] - rotMat[6]) * s,
            (rotMat[3] - rotMat[1]) * s,
            0.25 / s
        };
    } else if (rotMat[0] > rotMat[4] && rotMat[0] > rotMat[8]) {
        double s = 2.0 * std::sqrt(1.0 + rotMat[0] - rotMat[4] - rotMat[8]);
        rotation = {
            0.25 * s,
            (rotMat[1] + rotMat[3]) / s,
            (rotMat[2] + rotMat[6]) / s,
            (rotMat[7] - rotMat[5]) / s
        };
    } else if (rotMat[4] > rotMat[8]) {
        double s = 2.0 * std::sqrt(1.0 + rotMat[4] - rotMat[0] - rotMat[8]);
        rotation = {
            (rotMat[1] + rotMat[3]) / s,
            0.25 * s,
            (rotMat[5] + rotMat[7]) / s,
            (rotMat[2] - rotMat[6]) / s
        };
    } else {
        double s = 2.0 * std::sqrt(1.0 + rotMat[8] - rotMat[0] - rotMat[4]);
        rotation = {
            (rotMat[2] + rotMat[6]) / s,
            (rotMat[5] + rotMat[7]) / s,
            0.25 * s,
            (rotMat[3] - rotMat[1]) / s
        };
    }
}

std::vector<double> GltfFlatten::getNodeMatrix(const tinygltf::Node& node) {
    // If node has a matrix property, use it directly
    if (!node.matrix.empty() && node.matrix.size() == 16) {
        return node.matrix;
    }
    
    // Otherwise, compose from TRS
    std::vector<double> translation = node.translation.empty() ? 
        std::vector<double>{0, 0, 0} : node.translation;
    std::vector<double> rotation = node.rotation.empty() ? 
        std::vector<double>{0, 0, 0, 1} : node.rotation;
    std::vector<double> scale = node.scale.empty() ? 
        std::vector<double>{1, 1, 1} : node.scale;
    
    // Create rotation matrix from quaternion
    double x = rotation[0], y = rotation[1], z = rotation[2], w = rotation[3];
    double x2 = x + x, y2 = y + y, z2 = z + z;
    double xx = x * x2, xy = x * y2, xz = x * z2;
    double yy = y * y2, yz = y * z2, zz = z * z2;
    double wx = w * x2, wy = w * y2, wz = w * z2;
    
    std::vector<double> mat(16);
    mat[0] = (1 - (yy + zz)) * scale[0];
    mat[1] = (xy + wz) * scale[0];
    mat[2] = (xz - wy) * scale[0];
    mat[3] = 0;
    
    mat[4] = (xy - wz) * scale[1];
    mat[5] = (1 - (xx + zz)) * scale[1];
    mat[6] = (yz + wx) * scale[1];
    mat[7] = 0;
    
    mat[8] = (xz + wy) * scale[2];
    mat[9] = (yz - wx) * scale[2];
    mat[10] = (1 - (xx + yy)) * scale[2];
    mat[11] = 0;
    
    mat[12] = translation[0];
    mat[13] = translation[1];
    mat[14] = translation[2];
    mat[15] = 1;
    
    return mat;
}

void GltfFlatten::setNodeMatrix(tinygltf::Node& node, const std::vector<double>& matrix) {
    std::vector<double> translation, rotation, scale;
    decomposeMatrix(matrix, translation, rotation, scale);
    
    node.translation = translation;
    node.rotation = rotation;
    node.scale = scale;
    node.matrix.clear(); // Clear matrix property, use TRS instead
}

std::vector<double> GltfFlatten::multiplyMatrices(const std::vector<double>& a, const std::vector<double>& b) {
    std::vector<double> result(16);
    
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            double sum = 0;
            for (int k = 0; k < 4; ++k) {
                sum += a[row + k * 4] * b[k + col * 4];
            }
            result[row + col * 4] = sum;
        }
    }
    
    return result;
}

int GltfFlatten::getParentNode(const tinygltf::Model& model, int nodeIndex) {
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const auto& node = model.nodes[i];
        for (int childIdx : node.children) {
            if (childIdx == nodeIndex) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

std::vector<double> GltfFlatten::getWorldMatrix(const tinygltf::Model& model, int nodeIndex) {
    // Build ancestor chain
    std::vector<int> ancestors;
    int current = nodeIndex;
    while (current >= 0) {
        ancestors.push_back(current);
        current = getParentNode(model, current);
    }
    
    // Compute world matrix from root to node
    std::vector<double> worldMatrix = identityMatrix();
    for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
        std::vector<double> localMatrix = getNodeMatrix(model.nodes[*it]);
        worldMatrix = multiplyMatrices(worldMatrix, localMatrix);
    }
    
    return worldMatrix;
}

std::vector<int> GltfFlatten::listNodeScenes(const tinygltf::Model& model, int nodeIndex) {
    std::vector<int> sceneIndices;
    
    // Find the root node by traversing up
    int rootNode = nodeIndex;
    int parent = getParentNode(model, rootNode);
    while (parent >= 0) {
        rootNode = parent;
        parent = getParentNode(model, rootNode);
    }
    
    // Find which scenes contain this root node
    for (size_t sceneIdx = 0; sceneIdx < model.scenes.size(); ++sceneIdx) {
        const auto& scene = model.scenes[sceneIdx];
        for (int sceneNodeIdx : scene.nodes) {
            if (sceneNodeIdx == rootNode) {
                sceneIndices.push_back(static_cast<int>(sceneIdx));
                break;
            }
        }
    }
    
    return sceneIndices;
}

bool GltfFlatten::isJoint(const tinygltf::Model& model, int nodeIndex) {
    for (const auto& skin : model.skins) {
        for (int jointIdx : skin.joints) {
            if (jointIdx == nodeIndex) {
                return true;
            }
        }
    }
    return false;
}

bool GltfFlatten::isAnimated(const tinygltf::Model& model, int nodeIndex) {
    for (const auto& animation : model.animations) {
        for (const auto& channel : animation.channels) {
            if (channel.target_node == nodeIndex) {
                // Check if it's a TRS animation (translation, rotation, scale)
                // const auto& sampler = animation.samplers[channel.sampler];
                if (channel.target_path == "translation" ||
                    channel.target_path == "rotation" ||
                    channel.target_path == "scale") {
                    return true;
                }
            }
        }
    }
    return false;
}

bool GltfFlatten::hasConstrainedParent(const tinygltf::Model& model, int nodeIndex,
                                       const std::set<int>& joints,
                                       const std::set<int>& animated) {
    int parent = getParentNode(model, nodeIndex);
    while (parent >= 0) {
        if (joints.find(parent) != joints.end() || animated.find(parent) != animated.end()) {
            return true;
        }
        parent = getParentNode(model, parent);
    }
    return false;
}

void GltfFlatten::clearNodeParent(tinygltf::Model& model, int nodeIndex) {
    std::vector<int> scenes = listNodeScenes(model, nodeIndex);
    int parentIdx = getParentNode(model, nodeIndex);
    
    if (parentIdx < 0) return; // Already at root
    
    // Apply inherited transforms to local matrix
    std::vector<double> worldMatrix = getWorldMatrix(model, nodeIndex);
    setNodeMatrix(model.nodes[nodeIndex], worldMatrix);
    
    // Remove from parent
    auto& parent = model.nodes[parentIdx];
    parent.children.erase(
        std::remove(parent.children.begin(), parent.children.end(), nodeIndex),
        parent.children.end()
    );
    
    // Add to scene roots
    for (int sceneIdx : scenes) {
        auto& scene = model.scenes[sceneIdx];
        if (std::find(scene.nodes.begin(), scene.nodes.end(), nodeIndex) == scene.nodes.end()) {
            scene.nodes.push_back(nodeIndex);
        }
    }
}

void GltfFlatten::pruneEmptyNodes(tinygltf::Model& model) {
    bool changed = true;
    while (changed) {
        changed = false;
        
        for (int i = static_cast<int>(model.nodes.size()) - 1; i >= 0; --i) {
            const auto& node = model.nodes[i];
            
            // Check if node is empty (no attachments, no children)
            bool isEmpty = node.children.empty() &&
                          node.mesh < 0 &&
                          node.camera < 0 &&
                          node.skin < 0 &&
                          node.extensions.empty() &&
                          node.extras.Keys().empty();
            
            if (!isEmpty) continue;
            
            // Check if node is referenced by anything
            bool isReferenced = false;
            
            // Check if it's a scene root
            for (const auto& scene : model.scenes) {
                if (std::find(scene.nodes.begin(), scene.nodes.end(), i) != scene.nodes.end()) {
                    isReferenced = true;
                    break;
                }
            }
            
            // Check if it's a child of another node
            for (const auto& otherNode : model.nodes) {
                if (std::find(otherNode.children.begin(), otherNode.children.end(), i) != otherNode.children.end()) {
                    isReferenced = true;
                    break;
                }
            }
            
            // Check if it's a joint
            if (isJoint(model, i)) {
                isReferenced = true;
            }
            
            if (isReferenced) continue;
            
            // Mark for removal - but this is complex, so we'll just skip pruning
            // for now to avoid index remapping complexity
            // In a production implementation, you'd want to remap all indices
        }
    }
}

int GltfFlatten::process(tinygltf::Model& model, bool cleanup) {
    int flattenedCount = 0;
    size_t totalNodes = model.nodes.size();
    
    if (totalNodes == 0) return 0;
    
    // Check for debug mode
    bool debug = std::getenv("GLTFU_DEBUG_FLATTEN") != nullptr;
    
    // Build parent map once (O(n) instead of O(n²))
    std::vector<int> parentMap(totalNodes, -1);
    for (size_t i = 0; i < totalNodes; ++i) {
        const auto& node = model.nodes[i];
        for (int childIdx : node.children) {
            if (childIdx >= 0 && childIdx < static_cast<int>(totalNodes)) {
                parentMap[childIdx] = static_cast<int>(i);
            }
        }
    }
    
    // (1) Mark joints from skins
    std::set<int> joints;
    for (const auto& skin : model.skins) {
        for (int jointIdx : skin.joints) {
            joints.insert(jointIdx);
        }
    }
    
    // (2) Mark nodes with TRS animation
    std::set<int> animated;
    for (const auto& animation : model.animations) {
        for (const auto& channel : animation.channels) {
            if (channel.target_path == "translation" ||
                channel.target_path == "rotation" ||
                channel.target_path == "scale") {
                animated.insert(channel.target_node);
            }
        }
    }
    
    // (3) Mark descendants of joints/animated nodes in single pass
    std::vector<bool> hasConstrainedAncestor(totalNodes, false);
    
    for (size_t i = 0; i < totalNodes; ++i) {
        // Walk up parent chain (not including current node)
        int current = parentMap[i];
        bool constrained = false;
        
        // Walk up to find constrained ancestor
        while (current >= 0 && !constrained) {
            if (joints.count(current) || animated.count(current)) {
                constrained = true;
            }
            current = parentMap[current];
        }
        
        if (constrained) {
            hasConstrainedAncestor[i] = true;
        }
    }
    
    // (4) Collect nodes to flatten (nodes with parents that aren't constrained)
    // IMPORTANT: Only flatten leaf nodes (nodes without children) to avoid transform corruption
    std::vector<std::pair<int, int>> nodesToFlatten; // pair of (depth, nodeIdx)
    nodesToFlatten.reserve(totalNodes / 2); // Estimate
    
    // Compute depth for each node
    auto getDepth = [&](int nodeIdx) {
        int depth = 0;
        int current = parentMap[nodeIdx];
        while (current >= 0) {
            depth++;
            current = parentMap[current];
        }
        return depth;
    };
    
    for (size_t i = 0; i < totalNodes; ++i) {
        // Skip if constrained
        if (animated.count(i) || joints.count(i) || hasConstrainedAncestor[i]) {
            continue;
        }
        
        // Skip if has children (only flatten leaf nodes)
        if (!model.nodes[i].children.empty()) {
            continue;
        }
        
        // Only flatten if has parent
        if (parentMap[i] >= 0) {
            int depth = getDepth(static_cast<int>(i));
            nodesToFlatten.push_back({depth, static_cast<int>(i)});
        }
    }
    
    // Sort by depth (deepest first) so children are flattened before parents
    std::sort(nodesToFlatten.begin(), nodesToFlatten.end(), 
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    // (5) Flatten collected nodes
    for (const auto& pair : nodesToFlatten) {
        int nodeIdx = pair.second;
        int depth = pair.first;
        
        // Find which scenes contain this node's root
        int rootNode = nodeIdx;
        while (parentMap[rootNode] >= 0) {
            rootNode = parentMap[rootNode];
        }
        
        std::vector<int> sceneIndices;
        for (size_t sceneIdx = 0; sceneIdx < model.scenes.size(); ++sceneIdx) {
            const auto& scene = model.scenes[sceneIdx];
            if (std::find(scene.nodes.begin(), scene.nodes.end(), rootNode) != scene.nodes.end()) {
                sceneIndices.push_back(static_cast<int>(sceneIdx));
            }
        }
        
        int parentIdx = parentMap[nodeIdx];
        if (parentIdx < 0) continue; // Already at root
        
        // Check if we can skip matrix math when parent has identity transform
        bool parentIsIdentity = true;
        int current = parentIdx;
        std::vector<int> ancestors;
        while (current >= 0) {
            ancestors.push_back(current);
            auto& ancestorNode = model.nodes[current];
            
            // Check if this ancestor has a non-identity transform
            if (!ancestorNode.matrix.empty()) {
                parentIsIdentity = false;
            } else {
                bool hasTranslation = !ancestorNode.translation.empty() && 
                    (std::abs(ancestorNode.translation[0]) > 1e-10 ||
                     std::abs(ancestorNode.translation[1]) > 1e-10 ||
                     std::abs(ancestorNode.translation[2]) > 1e-10);
                bool hasRotation = !ancestorNode.rotation.empty() &&
                    (std::abs(ancestorNode.rotation[0]) > 1e-10 ||
                     std::abs(ancestorNode.rotation[1]) > 1e-10 ||
                     std::abs(ancestorNode.rotation[2]) > 1e-10 ||
                     std::abs(ancestorNode.rotation[3] - 1.0) > 1e-10);
                bool hasScale = !ancestorNode.scale.empty() &&
                    (std::abs(ancestorNode.scale[0] - 1.0) > 1e-10 ||
                     std::abs(ancestorNode.scale[1] - 1.0) > 1e-10 ||
                     std::abs(ancestorNode.scale[2] - 1.0) > 1e-10);
                
                if (hasTranslation || hasRotation || hasScale) {
                    parentIsIdentity = false;
                }
            }
            
            current = parentMap[current];
        }
        
        // If all ancestors are identity, skip the expensive matrix operations
        if (parentIsIdentity) {
            if (debug) {
                std::cout << "Flattening node " << nodeIdx << " (depth " << depth 
                          << ", root " << rootNode << ") - ancestors are identity, skipping\n\n";
            }
            
            // Just remove from parent and add to scene root
            auto& parent = model.nodes[parentIdx];
            parent.children.erase(
                std::remove(parent.children.begin(), parent.children.end(), nodeIdx),
                parent.children.end()
            );
            
            for (int sceneIdx : sceneIndices) {
                auto& scene = model.scenes[sceneIdx];
                if (std::find(scene.nodes.begin(), scene.nodes.end(), nodeIdx) == scene.nodes.end()) {
                    scene.nodes.push_back(nodeIdx);
                }
            }
            
            parentMap[nodeIdx] = -1;
            flattenedCount++;
            continue;
        }
        
        // Compute world matrix by walking up parent chain (EXCLUDING the node itself)
        std::vector<double> worldMatrix = identityMatrix();
        
        if (debug) {
            std::cout << "Flattening node " << nodeIdx << " (depth " << depth 
                      << ", root " << rootNode << ")\n";
            std::cout << "  Ancestors: [";
            for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
                std::cout << *it << " ";
            }
            std::cout << "]\n";
        }
        
        // Multiply parent transforms to get parent's world matrix
        for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
            std::vector<double> localMatrix = getNodeMatrix(model.nodes[*it]);
            worldMatrix = multiplyMatrices(worldMatrix, localMatrix);
        }
        
        // Now multiply by node's local transform to get node's world position
        std::vector<double> nodeLocalMatrix = getNodeMatrix(model.nodes[nodeIdx]);
        worldMatrix = multiplyMatrices(worldMatrix, nodeLocalMatrix);
        
        if (debug) {
            std::cout << "  World position: [" << worldMatrix[12] << ", " 
                      << worldMatrix[13] << ", " << worldMatrix[14] << "]\n";
        }
        
        // Apply world transform to node
        setNodeMatrix(model.nodes[nodeIdx], worldMatrix);
        
        // Remove from parent
        auto& parent = model.nodes[parentIdx];
        parent.children.erase(
            std::remove(parent.children.begin(), parent.children.end(), nodeIdx),
            parent.children.end()
        );
        
        // Add to scene roots
        for (int sceneIdx : sceneIndices) {
            auto& scene = model.scenes[sceneIdx];
            if (std::find(scene.nodes.begin(), scene.nodes.end(), nodeIdx) == scene.nodes.end()) {
                scene.nodes.push_back(nodeIdx);
            }
        }
        
        // Update parent map
        parentMap[nodeIdx] = -1;
        
        flattenedCount++;
    }
    
    return flattenedCount;
}

} // namespace gltfu
