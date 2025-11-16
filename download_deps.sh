#!/bin/bash

# Script to download header-only dependencies with specific release versions

THIRD_PARTY_DIR="$(dirname "$0")/third_party"
mkdir -p "$THIRD_PARTY_DIR"

# Version numbers
TINYGLTF_VERSION="v2.9.3"
CLI11_VERSION="v2.6.1"
JSON_VERSION="v3.11.3"
MESHOPT_VERSION="v0.21"
XXHASH_VERSION="v0.8.2"
DRACO_VERSION="1.5.7"

echo "Downloading tinygltf ${TINYGLTF_VERSION}..."
curl -L https://raw.githubusercontent.com/syoyo/tinygltf/${TINYGLTF_VERSION}/tiny_gltf.h -o "$THIRD_PARTY_DIR/tiny_gltf.h"

echo "Downloading stb_image.h..."
curl -L https://raw.githubusercontent.com/nothings/stb/master/stb_image.h -o "$THIRD_PARTY_DIR/stb_image.h"

echo "Downloading stb_image_write.h..."
curl -L https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h -o "$THIRD_PARTY_DIR/stb_image_write.h"

echo "Downloading CLI11 ${CLI11_VERSION}..."
curl -L https://github.com/CLIUtils/CLI11/releases/download/${CLI11_VERSION}/CLI11.hpp -o "$THIRD_PARTY_DIR/CLI11.hpp"

echo "Downloading json.hpp (nlohmann/json) ${JSON_VERSION}..."
curl -L https://github.com/nlohmann/json/releases/download/${JSON_VERSION}/json.hpp -o "$THIRD_PARTY_DIR/json.hpp"

echo "Downloading meshoptimizer ${MESHOPT_VERSION}..."
curl -L https://raw.githubusercontent.com/zeux/meshoptimizer/${MESHOPT_VERSION}/src/meshoptimizer.h -o "$THIRD_PARTY_DIR/meshoptimizer.h"
curl -L https://raw.githubusercontent.com/zeux/meshoptimizer/${MESHOPT_VERSION}/src/simplifier.cpp -o "$THIRD_PARTY_DIR/meshoptimizer_simplifier.cpp"
curl -L https://raw.githubusercontent.com/zeux/meshoptimizer/${MESHOPT_VERSION}/src/allocator.cpp -o "$THIRD_PARTY_DIR/meshoptimizer_allocator.cpp"

echo "Downloading xxHash ${XXHASH_VERSION}..."
curl -L https://raw.githubusercontent.com/Cyan4973/xxHash/${XXHASH_VERSION}/xxhash.h -o "$THIRD_PARTY_DIR/xxhash.h"

echo "Downloading Draco ${DRACO_VERSION}..."
if [ ! -d "$THIRD_PARTY_DIR/draco" ]; then
    TEMP_DIR=$(mktemp -d)
    curl -L https://github.com/google/draco/archive/refs/tags/${DRACO_VERSION}.tar.gz -o "$TEMP_DIR/draco.tar.gz"
    tar -xzf "$TEMP_DIR/draco.tar.gz" -C "$TEMP_DIR"
    mkdir -p "$THIRD_PARTY_DIR/draco"
    # Copy the entire draco source tree preserving structure
    cp -r "$TEMP_DIR/draco-${DRACO_VERSION}/src" "$THIRD_PARTY_DIR/draco/"
    cp -r "$TEMP_DIR/draco-${DRACO_VERSION}/cmake" "$THIRD_PARTY_DIR/draco/"
    cp "$TEMP_DIR/draco-${DRACO_VERSION}/CMakeLists.txt" "$THIRD_PARTY_DIR/draco/"
    rm -rf "$TEMP_DIR"
    echo "Draco ${DRACO_VERSION} extracted and installed"
else
    echo "Draco already exists, skipping download"
fi

echo "Dependencies downloaded successfully!"
