#include "CLI11.hpp"
#include "gltf_merger.h"
#include "gltf_dedup.h"
#include "gltf_flatten.h"
#include "gltf_join.h"
#include "gltf_weld.h"
#include "gltf_prune.h"
#include "gltf_simplify.h"
#include "gltf_info.h"
#include "gltf_compress.h"
#include "gltf_bounds.h"
#include "progress_reporter.h"

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

// Helper function to check if filename ends with .glb
bool isGlbFile(const std::string& filename) {
    if (filename.size() < 4) return false;
    std::string ext = filename.substr(filename.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".glb";
}

int main(int argc, char** argv) {
    CLI::App app{"gltfu - Memory-efficient GLTF operations tool"};
    app.require_subcommand(1);
    
    // Global option for JSON progress output
    bool jsonProgress = false;
    app.add_flag("--json-progress", jsonProgress, 
                 "Output progress reports as JSON (one per line)")
        ->group("Global");
    
    // Merge subcommand
    auto* mergeCmd = app.add_subcommand("merge", "Merge multiple GLTF files or scenes");
    
    std::vector<std::string> inputFiles;
    std::string outputFile;
    bool keepScenesIndependent = false;
    bool defaultScenesOnly = false;
    bool embedImages = false;
    bool embedBuffers = false;
    bool prettyPrint = true;
    bool writeBinary = false;
    std::vector<int> sceneIndices;
    
    mergeCmd->add_option("inputs", inputFiles, "Input GLTF files")
        ->required()
        ->check(CLI::ExistingFile);
    
    mergeCmd->add_option("-o,--output", outputFile, "Output GLTF file")
        ->required();
    
    mergeCmd->add_flag("--keep-scenes", keepScenesIndependent, 
                       "Keep all scenes as separate scenes in output (default: merge into one scene)");
    
    mergeCmd->add_flag("--default-scene-only", defaultScenesOnly,
                       "Merge only default scenes from each file (default: merge all scenes)");
    
    mergeCmd->add_option("--scenes", sceneIndices, 
                        "Specific scene indices to merge (not yet implemented)");
    
    mergeCmd->add_flag("--embed-images", embedImages, 
                       "Embed images in output file");
    
    mergeCmd->add_flag("--embed-buffers", embedBuffers, 
                       "Embed buffers in output file");
    
    mergeCmd->add_flag("--no-pretty-print", 
                       [&prettyPrint](int count) { prettyPrint = !count; },
                       "Disable pretty-printing of JSON");
    
    mergeCmd->add_flag("-b,--binary", writeBinary, 
                       "Write binary GLTF (.glb) format (auto-detected from .glb extension)");
    
    mergeCmd->callback([&]() {
        gltfu::ProgressReporter progress(
            jsonProgress ? gltfu::ProgressReporter::Format::JSON : gltfu::ProgressReporter::Format::Text
        );
        
        // Auto-detect binary format from output file extension
        if (!writeBinary && isGlbFile(outputFile)) {
            writeBinary = true;
        }
        
        progress.report("merge", "Starting merge of " + std::to_string(inputFiles.size()) + " file(s)", 0.0);
        
        gltfu::GltfMerger merger;
        
        // Load and merge files one at a time (memory efficient)
        for (size_t i = 0; i < inputFiles.size(); ++i) {
            double loadProgress = static_cast<double>(i) / inputFiles.size(); 
            progress.report("merge", "Loading and merging file " + std::to_string(i + 1) + "/" + std::to_string(inputFiles.size()), 
                          loadProgress, inputFiles[i]);
            
            // Pass flags: keepScenesIndependent and defaultScenesOnly
            if (!merger.loadAndMergeFile(inputFiles[i], keepScenesIndependent, defaultScenesOnly)) {
                progress.error("merge", merger.getError());
                return 1;
            }
        }
        
        if (!sceneIndices.empty()) {
            progress.report("merge", "Warning: --scenes option not yet implemented", 0.9);
        }
        
        // Save
        progress.report("merge", "Saving output", 0.75, outputFile);
        if (!merger.save(outputFile, embedImages, embedBuffers, prettyPrint, writeBinary)) {
            progress.error("merge", merger.getError());
            return 1;
        }
        
        progress.success("merge", "Successfully merged to: " + outputFile);
        return 0;
    });
    
    // Dedupe subcommand
    auto* dedupeCmd = app.add_subcommand("dedupe", "Remove duplicate data to reduce file size");
    
    std::string dedupeInput;
    std::string dedupeOutput;
    bool dedupAccessors = true;
    bool dedupMeshes = true;
    bool dedupMaterials = true;
    bool dedupTextures = true;
    bool keepUniqueNames = false;
    bool verbose = false;
    bool dedupeEmbedImages = false;
    bool dedupeEmbedBuffers = false;
    bool dedupePrettyPrint = true;
    bool dedupeWriteBinary = false;
    
    dedupeCmd->add_option("input", dedupeInput, "Input GLTF file")
        ->required()
        ->check(CLI::ExistingFile);
    
    dedupeCmd->add_option("-o,--output", dedupeOutput, "Output GLTF file")
        ->required();
    
    dedupeCmd->add_flag("--accessors", dedupAccessors, 
                        "Remove duplicate accessors (default: true)")
        ->default_val(true);
    
    dedupeCmd->add_flag("--meshes", dedupMeshes, 
                        "Remove duplicate meshes (default: true)")
        ->default_val(true);
    
    dedupeCmd->add_flag("--materials", dedupMaterials, 
                        "Remove duplicate materials (default: true)")
        ->default_val(true);
    
    dedupeCmd->add_flag("--textures", dedupTextures, 
                        "Remove duplicate textures and images (default: true)")
        ->default_val(true);
    
    dedupeCmd->add_flag("--keep-unique-names", keepUniqueNames, 
                        "Keep resources with unique names even if they are duplicates");
    
    dedupeCmd->add_flag("-v,--verbose", verbose, 
                        "Print detailed statistics");
    
    dedupeCmd->add_flag("--embed-images", dedupeEmbedImages, 
                        "Embed images in output file");
    
    dedupeCmd->add_flag("--embed-buffers", dedupeEmbedBuffers, 
                        "Embed buffers in output file");
    
    dedupeCmd->add_flag("--no-pretty-print", 
                        [&dedupePrettyPrint](int count) { dedupePrettyPrint = !count; },
                        "Disable pretty-printing of JSON");
    
    dedupeCmd->add_flag("-b,--binary", dedupeWriteBinary, 
                        "Write binary GLTF (.glb) format (auto-detected from .glb extension)");
    
    dedupeCmd->callback([&]() {
        gltfu::ProgressReporter progress(
            jsonProgress ? gltfu::ProgressReporter::Format::JSON : gltfu::ProgressReporter::Format::Text
        );
        
        // Auto-detect binary format from output file extension
        if (!dedupeWriteBinary && isGlbFile(dedupeOutput)) {
            dedupeWriteBinary = true;
        }
        
        progress.report("dedupe", "Loading file", 0.0, dedupeInput);
        
        // Load the file
        tinygltf::TinyGLTF loader;
        tinygltf::Model model;
        std::string err, warn;
        
        bool ret = false;
        if (dedupeInput.substr(dedupeInput.find_last_of(".") + 1) == "glb") {
            ret = loader.LoadBinaryFromFile(&model, &err, &warn, dedupeInput);
        } else {
            ret = loader.LoadASCIIFromFile(&model, &err, &warn, dedupeInput);
        }
        
        if (!warn.empty() && !jsonProgress) {
            std::cerr << "Warning: " << warn << std::endl;
        }
        
        if (!err.empty()) {
            progress.error("dedupe", "Error loading file: " + err);
            return 1;
        }
        
        if (!ret) {
            progress.error("dedupe", "Failed to load: " + dedupeInput);
            return 1;
        }
        
        progress.report("dedupe", "File loaded, starting deduplication", 0.2);
        
        // Deduplicate
        gltfu::GltfDedup deduper;
        gltfu::GltfDedup::Options options;
        options.dedupAccessors = dedupAccessors;
        options.dedupMeshes = dedupMeshes;
        options.dedupMaterials = dedupMaterials;
        options.dedupTextures = dedupTextures;
        options.keepUniqueNames = keepUniqueNames;
        options.verbose = verbose;
        options.progressReporter = &progress;
        
        if (!deduper.process(model, options)) {
            progress.error("dedupe", deduper.getError());
            return 1;
        }
        
        // Print stats
        std::string stats = deduper.getStats();
        if (!stats.empty()) {
            progress.report("dedupe", "Deduplication complete", 0.9, stats);
        }
        
        // When writing to GLB, clear buffer URIs so data is embedded in binary chunk
        if (dedupeWriteBinary) {
            for (auto& buffer : model.buffers) {
                buffer.uri.clear();
            }
        }
        
        // Save
        progress.report("dedupe", "Saving output", 0.95, dedupeOutput);
        if (dedupeWriteBinary) {
            // GLB format must embed buffers in the binary chunk
            ret = loader.WriteGltfSceneToFile(&model, dedupeOutput,
                                             dedupeEmbedImages, true, 
                                             dedupePrettyPrint, true);
        } else {
            ret = loader.WriteGltfSceneToFile(&model, dedupeOutput,
                                             dedupeEmbedImages, dedupeEmbedBuffers, 
                                             dedupePrettyPrint, false);
        }
        
        if (!ret) {
            progress.error("dedupe", "Failed to write file: " + dedupeOutput);
            return 1;
        }
        
        progress.success("dedupe", "Successfully deduplicated to: " + dedupeOutput);
        return 0;
    });
    
    // Info subcommand
    auto* infoCmd = app.add_subcommand("info", "Display information about a GLTF file");
    
    std::string infoInputFile;
    bool infoVerbose = false;
    
    infoCmd->add_option("input", infoInputFile, "Input GLTF/GLB file")
        ->required()
        ->check(CLI::ExistingFile);
    
    infoCmd->add_flag("-v,--verbose", infoVerbose, 
                     "Show detailed information");
    
    infoCmd->callback([&]() {
        gltfu::ProgressReporter progress(
            jsonProgress ? gltfu::ProgressReporter::Format::JSON : gltfu::ProgressReporter::Format::Text
        );
        
        progress.report("info", "Analyzing file", 0.0, infoInputFile);
        
        gltfu::GltfInfo info;
        if (!info.analyze(infoInputFile)) {
            progress.error("info", info.getError());
            return 1;
        }
        
        progress.report("info", "Analysis complete", 1.0);
        
        // Print the formatted info (always print to stdout, even with JSON progress)
        if (!jsonProgress) {
            std::cout << "\n";
        }
        std::cout << info.format(infoVerbose);
        
        if (!jsonProgress) {
            std::cout << "\n";
        }
        
        return 0;
    });
    
    // Flatten subcommand
    auto* flattenCmd = app.add_subcommand("flatten", "Flatten scene graph hierarchy");
    
    std::string flattenInputFile;
    std::string flattenOutputFile;
    bool flattenCleanup = true;
    bool flattenEmbedImages = false;
    bool flattenEmbedBuffers = false;
    bool flattenPrettyPrint = true;
    bool flattenWriteBinary = false;
    
    flattenCmd->add_option("input", flattenInputFile, "Input GLTF file")
        ->required()
        ->check(CLI::ExistingFile);
    
    flattenCmd->add_option("-o,--output", flattenOutputFile, "Output GLTF file")
        ->required();
    
    flattenCmd->add_flag("--no-cleanup", 
                         [&flattenCleanup](int count) { flattenCleanup = !count; },
                         "Skip removal of empty leaf nodes");
    
    flattenCmd->add_flag("--embed-images", flattenEmbedImages, 
                         "Embed images in output file");
    
    flattenCmd->add_flag("--embed-buffers", flattenEmbedBuffers, 
                         "Embed buffers in output file");
    
    flattenCmd->add_flag("--no-pretty-print", 
                         [&flattenPrettyPrint](int count) { flattenPrettyPrint = !count; },
                         "Disable JSON pretty printing");
    
    flattenCmd->add_flag("--binary", flattenWriteBinary, 
                         "Write binary .glb output (auto-detected from .glb extension)");
    
    flattenCmd->callback([&]() {
        gltfu::ProgressReporter progress(
            jsonProgress ? gltfu::ProgressReporter::Format::JSON : gltfu::ProgressReporter::Format::Text
        );
        
        // Auto-detect binary format from output file extension
        if (!flattenWriteBinary && isGlbFile(flattenOutputFile)) {
            flattenWriteBinary = true;
        }
        
        progress.report("flatten", "Loading file", 0.0, flattenInputFile);
        
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;
        
        bool ret;
        if (flattenInputFile.size() >= 4 && flattenInputFile.substr(flattenInputFile.size()-4) == ".glb") {
            ret = loader.LoadBinaryFromFile(&model, &err, &warn, flattenInputFile);
        } else {
            ret = loader.LoadASCIIFromFile(&model, &err, &warn, flattenInputFile);
        }
        
        if (!warn.empty() && !jsonProgress) {
            std::cerr << "Warning: " << warn << std::endl;
        }
        
        if (!ret) {
            progress.error("flatten", "Failed to load: " + err);
            return 1;
        }
        
        progress.report("flatten", "Flattening scene graph", 0.3);
        
        // Flatten the scene graph
        int flattenedCount = gltfu::GltfFlatten::process(model, flattenCleanup);
        progress.report("flatten", "Flattened nodes", 0.7, std::to_string(flattenedCount) + " nodes");
        
        // When writing to GLB, clear buffer URIs so data is embedded in binary chunk
        if (flattenWriteBinary) {
            for (auto& buffer : model.buffers) {
                buffer.uri.clear();
            }
        }
        
        progress.report("flatten", "Writing output", 0.9, flattenOutputFile);
        
        // Write output
        bool writeRet;
        if (flattenWriteBinary) {
            // GLB format must embed buffers in the binary chunk
            writeRet = loader.WriteGltfSceneToFile(&model, flattenOutputFile, 
                                                   flattenEmbedImages, 
                                                   true, 
                                                   flattenPrettyPrint, 
                                                   true);
        } else {
            writeRet = loader.WriteGltfSceneToFile(&model, flattenOutputFile, 
                                                   flattenEmbedImages, 
                                                   flattenEmbedBuffers, 
                                                   flattenPrettyPrint, 
                                                   false);
        }
        
        if (!writeRet) {
            progress.error("flatten", "Failed to write output file: " + flattenOutputFile);
            return 1;
        }
        
        progress.success("flatten", "Written to: " + flattenOutputFile);
        return 0;
    });
    
    // Join subcommand
    auto* joinCmd = app.add_subcommand("join", "Join compatible primitives to reduce draw calls");
    
    std::string joinInputFile;
    std::string joinOutputFile;
    bool keepMeshes = false;
    bool keepNamed = false;
    bool joinEmbedImages = false;
    bool joinEmbedBuffers = false;
    bool joinPrettyPrint = true;
    bool joinWriteBinary = false;
    
    joinCmd->add_option("input", joinInputFile, "Input GLTF file")
        ->required()
        ->check(CLI::ExistingFile);
    
    joinCmd->add_option("-o,--output", joinOutputFile, "Output GLTF file")
        ->required();
    
    joinCmd->add_flag("--keep-meshes", keepMeshes,
                      "Keep meshes separate (only join primitives within same mesh)");
    
    joinCmd->add_flag("--keep-named", keepNamed,
                      "Keep named meshes and nodes separate");
    
    joinCmd->add_flag("--embed-images", joinEmbedImages, 
                      "Embed images in output file");
    
    joinCmd->add_flag("--embed-buffers", joinEmbedBuffers, 
                      "Embed buffers in output file");
    
    joinCmd->add_flag("--no-pretty-print", 
                      [&joinPrettyPrint](int count) { joinPrettyPrint = !count; },
                      "Disable JSON pretty printing");
    
    joinCmd->add_flag("--binary", joinWriteBinary, 
                      "Write binary .glb output (auto-detected from .glb extension)");
    
    joinCmd->callback([&]() {
        gltfu::ProgressReporter progress(
            jsonProgress ? gltfu::ProgressReporter::Format::JSON : gltfu::ProgressReporter::Format::Text
        );
        
        // Auto-detect binary format from output file extension
        if (!joinWriteBinary && isGlbFile(joinOutputFile)) {
            joinWriteBinary = true;
        }
        
        progress.report("join", "Loading file", 0.0, joinInputFile);
        
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;
        
        bool ret;
        if (joinInputFile.size() >= 4 && joinInputFile.substr(joinInputFile.size()-4) == ".glb") {
            ret = loader.LoadBinaryFromFile(&model, &err, &warn, joinInputFile);
        } else {
            ret = loader.LoadASCIIFromFile(&model, &err, &warn, joinInputFile);
        }
        
        if (!warn.empty() && !jsonProgress) {
            std::cerr << "Warning: " << warn << std::endl;
        }
        
        if (!ret) {
            progress.error("join", "Failed to load: " + err);
            return 1;
        }
        
        progress.report("join", "Joining primitives", 0.3);
        
        // Join primitives
        gltfu::GltfJoin joiner;
        gltfu::JoinOptions options;
        options.keepMeshes = keepMeshes;
        options.keepNamed = keepNamed;
        
        if (!joiner.process(model, options)) {
            progress.error("join", "Join operation failed");
            return 1;
        }
        
        // When writing to GLB, clear buffer URIs so data is embedded in binary chunk
        if (joinWriteBinary) {
            for (auto& buffer : model.buffers) {
                buffer.uri.clear();
            }
        }
        
        progress.report("join", "Writing output", 0.9, joinOutputFile);
        
        // Write output
        bool writeRet;
        if (joinWriteBinary) {
            // GLB format must embed buffers in the binary chunk
            writeRet = loader.WriteGltfSceneToFile(&model, joinOutputFile, 
                                                   joinEmbedImages, 
                                                   true, 
                                                   joinPrettyPrint, 
                                                   true);
        } else {
            writeRet = loader.WriteGltfSceneToFile(&model, joinOutputFile, 
                                                   joinEmbedImages, 
                                                   joinEmbedBuffers, 
                                                   joinPrettyPrint, 
                                                   false);
        }
        
        if (!writeRet) {
            progress.error("join", "Failed to write output file: " + joinOutputFile);
            return 1;
        }
        
        progress.success("join", "Written to: " + joinOutputFile);
        return 0;
    });
    
    // Weld subcommand
    auto* weldCmd = app.add_subcommand("weld", "Merge identical vertices to reduce geometry size");
    
    std::string weldInputFile;
    std::string weldOutputFile;
    bool weldOverwrite = false;
    bool weldEmbedImages = false;
    bool weldEmbedBuffers = false;
    bool weldPrettyPrint = true;
    bool weldWriteBinary = false;
    
    weldCmd->add_option("input", weldInputFile, "Input GLTF file")
        ->required()
        ->check(CLI::ExistingFile);
    
    weldCmd->add_option("-o,--output", weldOutputFile, "Output GLTF file")
        ->required();
    
    weldCmd->add_flag("--overwrite", weldOverwrite,
                      "Overwrite existing indices with optimized version");
    
    weldCmd->add_flag("--embed-images", weldEmbedImages, 
                      "Embed images in output file");
    
    weldCmd->add_flag("--embed-buffers", weldEmbedBuffers, 
                      "Embed buffers in output file");
    
    weldCmd->add_flag("--no-pretty-print", 
                      [&weldPrettyPrint](int count) { weldPrettyPrint = !count; },
                      "Disable JSON pretty printing");
    
    weldCmd->add_flag("--binary", weldWriteBinary, 
                      "Write binary .glb output (auto-detected from .glb extension)");
    
    weldCmd->callback([&]() {
        gltfu::ProgressReporter progress(
            jsonProgress ? gltfu::ProgressReporter::Format::JSON : gltfu::ProgressReporter::Format::Text
        );
        
        // Auto-detect binary format from output file extension
        if (!weldWriteBinary && isGlbFile(weldOutputFile)) {
            weldWriteBinary = true;
        }
        
        progress.report("weld", "Loading file", 0.0, weldInputFile);
        
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;
        
        bool ret;
        if (weldInputFile.size() >= 4 && weldInputFile.substr(weldInputFile.size()-4) == ".glb") {
            ret = loader.LoadBinaryFromFile(&model, &err, &warn, weldInputFile);
        } else {
            ret = loader.LoadASCIIFromFile(&model, &err, &warn, weldInputFile);
        }
        
        if (!warn.empty() && !jsonProgress) {
            std::cerr << "Warning: " << warn << std::endl;
        }
        
        if (!ret) {
            progress.error("weld", "Failed to load: " + err);
            return 1;
        }
        
        // Weld vertices
        progress.report("weld", "Welding vertices", 0.3);
        gltfu::GltfWeld welder;
        gltfu::WeldOptions options;
        options.overwrite = weldOverwrite;
        
        if (!welder.process(model, options)) {
            progress.error("weld", "Weld operation failed");
            return 1;
        }
        
        // When writing to GLB, clear buffer URIs so data is embedded in binary chunk
        if (weldWriteBinary) {
            for (auto& buffer : model.buffers) {
                buffer.uri.clear();
            }
        }
        
        // Write output
        progress.report("weld", "Writing output", 0.9, weldOutputFile);
        bool writeRet;
        if (weldWriteBinary) {
            // GLB format must embed buffers in the binary chunk
            writeRet = loader.WriteGltfSceneToFile(&model, weldOutputFile, 
                                                   weldEmbedImages, 
                                                   true, 
                                                   weldPrettyPrint, 
                                                   true);
        } else {
            writeRet = loader.WriteGltfSceneToFile(&model, weldOutputFile, 
                                                   weldEmbedImages, 
                                                   weldEmbedBuffers, 
                                                   weldPrettyPrint, 
                                                   false);
        }
        
        if (!writeRet) {
            progress.error("weld", "Failed to write output file: " + weldOutputFile);
            return 1;
        }
        
        progress.success("weld", "Written to: " + weldOutputFile);
        return 0;
    });
    
    // Prune subcommand
    auto* pruneCmd = app.add_subcommand("prune", "Remove unused resources not referenced by any scene");
    
    std::string pruneInputFile;
    std::string pruneOutputFile;
    bool keepLeaves = false;
    bool keepAttributes = false;
    bool keepExtras = false;
    bool pruneEmbedImages = false;
    bool pruneEmbedBuffers = false;
    bool prunePrettyPrint = true;
    bool pruneWriteBinary = false;
    
    pruneCmd->add_option("input", pruneInputFile, "Input GLTF file")
        ->required()
        ->check(CLI::ExistingFile);
    
    pruneCmd->add_option("-o,--output", pruneOutputFile, "Output GLTF file")
        ->required();
    
    pruneCmd->add_flag("--keep-leaves", keepLeaves,
                      "Keep empty leaf nodes");
    
    pruneCmd->add_flag("--keep-attributes", keepAttributes,
                      "Keep unused vertex attributes");
    
    pruneCmd->add_flag("--keep-extras", keepExtras,
                      "Prevent pruning properties with custom extras");
    
    pruneCmd->add_flag("--embed-images", pruneEmbedImages, 
                      "Embed images in output file");
    
    pruneCmd->add_flag("--embed-buffers", pruneEmbedBuffers, 
                      "Embed buffers in output file");
    
    pruneCmd->add_flag("--no-pretty-print", 
                      [&prunePrettyPrint](int count) { prunePrettyPrint = !count; },
                      "Disable JSON pretty printing");
    
    pruneCmd->add_flag("--binary", pruneWriteBinary, 
                       "Write binary .glb output (auto-detected from .glb extension)");    pruneCmd->callback([&]() {
        gltfu::ProgressReporter progress(
            jsonProgress ? gltfu::ProgressReporter::Format::JSON : gltfu::ProgressReporter::Format::Text
        );
        
        // Auto-detect binary format from output file extension
        if (!pruneWriteBinary && isGlbFile(pruneOutputFile)) {
            pruneWriteBinary = true;
        }
        
        progress.report("prune", "Loading file", 0.0, pruneInputFile);
        
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;
        
        bool ret;
        if (pruneInputFile.size() >= 4 && pruneInputFile.substr(pruneInputFile.size()-4) == ".glb") {
            ret = loader.LoadBinaryFromFile(&model, &err, &warn, pruneInputFile);
        } else {
            ret = loader.LoadASCIIFromFile(&model, &err, &warn, pruneInputFile);
        }
        
        if (!warn.empty() && !jsonProgress) {
            std::cerr << "Warning: " << warn << std::endl;
        }
        
        if (!ret) {
            progress.error("prune", "Failed to load: " + err);
            return 1;
        }
        
        // Prune unused resources
        progress.report("prune", "Pruning unused resources", 0.3);
        gltfu::GltfPrune pruner;
        gltfu::PruneOptions options;
        options.keepLeaves = keepLeaves;
        options.keepAttributes = keepAttributes;
        options.keepExtras = keepExtras;
        
        if (!pruner.process(model)) {
            progress.error("prune", "Prune operation failed");
            return 1;
        }
        
        // When writing to GLB, clear buffer URIs so data is embedded in binary chunk
        if (pruneWriteBinary) {
            for (auto& buffer : model.buffers) {
                buffer.uri.clear();
            }
        }
        
        // Write output
        progress.report("prune", "Writing output", 0.9, pruneOutputFile);
        bool writeRet;
        if (pruneWriteBinary) {
            // GLB format must embed buffers in the binary chunk
            writeRet = loader.WriteGltfSceneToFile(&model, pruneOutputFile, 
                                                   pruneEmbedImages, 
                                                   true, 
                                                   prunePrettyPrint, 
                                                   true);
        } else {
            writeRet = loader.WriteGltfSceneToFile(&model, pruneOutputFile, 
                                                   pruneEmbedImages, 
                                                   pruneEmbedBuffers, 
                                                   prunePrettyPrint, 
                                                   false);
        }
        
        if (!writeRet) {
            progress.error("prune", "Failed to write output file: " + pruneOutputFile);
            return 1;
        }
        
        progress.success("prune", "Written to: " + pruneOutputFile);
        return 0;
    });
    
    // Simplify subcommand
    auto* simplifyCmd = app.add_subcommand("simplify", "Reduce mesh complexity using meshoptimizer");
    
    std::string simplifyInputFile;
    std::string simplifyOutputFile;
    float simplifyRatio = 0.5f;
    float simplifyError = 0.01f;
    bool simplifyLockBorder = false;
    
    bool simplifyEmbedImages = false;
    bool simplifyEmbedBuffers = false;
    bool simplifyPrettyPrint = true;
    bool simplifyWriteBinary = false;
    
    simplifyCmd->add_option("input", simplifyInputFile, "Input GLTF file")
        ->required()
        ->check(CLI::ExistingFile);
    
    simplifyCmd->add_option("-o,--output", simplifyOutputFile, "Output GLTF file")
        ->required();
    
    simplifyCmd->add_option("-r,--ratio", simplifyRatio, 
        "Target ratio of triangles to keep (0.0-1.0, default 0.5)")
        ->check(CLI::Range(0.0, 1.0));
    
    simplifyCmd->add_option("-e,--error", simplifyError,
        "Maximum error threshold (default 0.01)")
        ->check(CLI::PositiveNumber);
    
    simplifyCmd->add_flag("-l,--lock-border", simplifyLockBorder,
        "Lock border vertices to prevent mesh from shrinking");
    
    simplifyCmd->add_flag("--embed-images", simplifyEmbedImages, 
        "Embed images as data URIs");
    
    simplifyCmd->add_flag("--embed-buffers", simplifyEmbedBuffers, 
        "Embed buffers as data URIs");
    
    simplifyCmd->add_flag("!--no-pretty-print,!--ugly", simplifyPrettyPrint, 
        "Disable pretty printing JSON");
    
    simplifyCmd->add_flag("-b,--binary", simplifyWriteBinary, 
        "Write output as GLB (binary) (auto-detected from .glb extension)");
    
    simplifyCmd->callback([&]() {
        gltfu::ProgressReporter progress(
            jsonProgress ? gltfu::ProgressReporter::Format::JSON : gltfu::ProgressReporter::Format::Text
        );
        
        if (simplifyOutputFile.empty()) {
            simplifyOutputFile = simplifyInputFile;
        }
        
        // Auto-detect binary format from output file extension
        if (!simplifyWriteBinary && isGlbFile(simplifyOutputFile)) {
            simplifyWriteBinary = true;
        }
        
        progress.report("simplify", "Loading file", 0.0, simplifyInputFile);
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;
        
        bool ret;
        if (simplifyInputFile.size() >= 4 && simplifyInputFile.substr(simplifyInputFile.size()-4) == ".glb") {
            ret = loader.LoadBinaryFromFile(&model, &err, &warn, simplifyInputFile);
        } else {
            ret = loader.LoadASCIIFromFile(&model, &err, &warn, simplifyInputFile);
        }
        
        if (!warn.empty() && !jsonProgress) {
            std::cerr << "Warning: " << warn << std::endl;
        }
        
        if (!ret) {
            progress.error("simplify", "Failed to load: " + err);
            return 1;
        }
        
        // Simplify meshes
        progress.report("simplify", "Simplifying meshes", 0.3);
        gltfu::GltfSimplify simplifier;
        gltfu::SimplifyOptions options;
        options.ratio = simplifyRatio;
        options.error = simplifyError;
        options.lockBorder = simplifyLockBorder;
        
        if (!simplifier.process(model, options)) {
            progress.error("simplify", "Simplify operation failed");
            return 1;
        }
        
        // When writing to GLB, clear buffer URIs so data is embedded in binary chunk
        if (simplifyWriteBinary) {
            for (auto& buffer : model.buffers) {
                buffer.uri.clear();
            }
        }
        
        // Write output
        progress.report("simplify", "Writing output", 0.9, simplifyOutputFile);
        bool writeRet;
        if (simplifyWriteBinary) {
            // GLB format must embed buffers in the binary chunk
            writeRet = loader.WriteGltfSceneToFile(&model, simplifyOutputFile, 
                                                   simplifyEmbedImages, 
                                                   true, 
                                                   simplifyPrettyPrint, 
                                                   true);
        } else {
            writeRet = loader.WriteGltfSceneToFile(&model, simplifyOutputFile, 
                                                   simplifyEmbedImages, 
                                                   simplifyEmbedBuffers, 
                                                   simplifyPrettyPrint, 
                                                   false);
        }
        
        if (!writeRet) {
            progress.error("simplify", "Failed to write output file: " + simplifyOutputFile);
            return 1;
        }
        
        progress.success("simplify", "Written to: " + simplifyOutputFile);
        return 0;
    });
    
    // Optim subcommand - Full optimization pipeline
    auto* optimCmd = app.add_subcommand("optim", "Optimize GLTF files (merge + dedupe + flatten + join + weld + prune)");
    
    std::vector<std::string> optimInputs;
    std::string optimOutput;
    bool optimSimplify = false;
    float optimSimplifyRatio = 0.75f;
    float optimSimplifyError = 0.01f;
    bool optimLockBorder = false;
    bool optimCompress = false;
    int optimCompressPositionBits = 14;
    int optimCompressNormalBits = 10;
    int optimCompressTexcoordBits = 12;
    int optimCompressColorBits = 8;
    bool optimSkipDedupe = false;
    bool optimSkipFlatten = false;
    bool optimSkipJoin = false;
    bool optimSkipWeld = false;
    bool optimSkipPrune = false;
    bool optimVerbose = false;
    bool optimEmbedImages = false;
    bool optimEmbedBuffers = false;
    bool optimPrettyPrint = true;
    bool optimWriteBinary = false;
    
    optimCmd->add_option("input", optimInputs, "Input GLTF file(s) to optimize")
        ->required()
        ->check(CLI::ExistingFile);
    
    optimCmd->add_option("-o,--output", optimOutput, "Output GLTF file")
        ->required();
    
    optimCmd->add_flag("--simplify", optimSimplify, 
                      "Apply mesh simplification");
    
    optimCmd->add_option("--simplify-ratio", optimSimplifyRatio, 
                        "Target ratio for simplification (default: 0.75)")
        ->check(CLI::Range(0.0, 1.0));
    
    optimCmd->add_option("--simplify-error", optimSimplifyError, 
                        "Error threshold for simplification (default: 0.01)")
        ->check(CLI::PositiveNumber);
    
    optimCmd->add_flag("--simplify-lock-border", optimLockBorder, 
                      "Lock border vertices during simplification");
    
#ifdef GLTFU_ENABLE_DRACO
    optimCmd->add_flag("--compress", optimCompress, 
                      "Apply Draco mesh compression");
    
    optimCmd->add_option("--compress-position-bits", optimCompressPositionBits, 
                        "Quantization bits for positions (default: 14)")
        ->check(CLI::Range(10, 16));
    
    optimCmd->add_option("--compress-normal-bits", optimCompressNormalBits, 
                        "Quantization bits for normals (default: 10)")
        ->check(CLI::Range(8, 12));
    
    optimCmd->add_option("--compress-texcoord-bits", optimCompressTexcoordBits, 
                        "Quantization bits for texture coordinates (default: 12)")
        ->check(CLI::Range(10, 14));
    
    optimCmd->add_option("--compress-color-bits", optimCompressColorBits, 
                        "Quantization bits for colors (default: 8)")
        ->check(CLI::Range(6, 10));
#endif
    
    optimCmd->add_flag("--skip-dedupe", optimSkipDedupe, 
                      "Skip deduplication pass");
    
    optimCmd->add_flag("--skip-flatten", optimSkipFlatten, 
                      "Skip scene flattening pass");
    
    optimCmd->add_flag("--skip-join", optimSkipJoin, 
                      "Skip primitive joining pass");
    
    optimCmd->add_flag("--skip-weld", optimSkipWeld, 
                      "Skip vertex welding pass");
    
    optimCmd->add_flag("--skip-prune", optimSkipPrune, 
                      "Skip unused resource pruning pass");
    
    optimCmd->add_flag("-v,--verbose", optimVerbose, 
                      "Show detailed optimization statistics");
    
    optimCmd->add_flag("--embed-images", optimEmbedImages, 
                       "Embed images in output file");
    
    optimCmd->add_flag("--embed-buffers", optimEmbedBuffers, 
                       "Embed buffers in output file");
    
    optimCmd->add_flag("--no-pretty-print", 
                       [&optimPrettyPrint](int count) { optimPrettyPrint = !count; },
                       "Disable pretty-printing of JSON");
    
    optimCmd->add_flag("-b,--binary", optimWriteBinary, 
                       "Write binary GLTF (.glb) format (auto-detected from .glb extension)");
    
    optimCmd->callback([&]() {
        gltfu::ProgressReporter progress(
            jsonProgress ? gltfu::ProgressReporter::Format::JSON : gltfu::ProgressReporter::Format::Text
        );
        
        // Auto-detect binary format from output file extension
        if (!optimWriteBinary && isGlbFile(optimOutput)) {
            optimWriteBinary = true;
        }
        
        progress.report("optim", "Starting optimization pipeline", 0.0);
        
        tinygltf::TinyGLTF loader;
        tinygltf::Model model;
        
        // Step 1: Load and merge input files
        if (optimInputs.size() > 1) {
            progress.report("optim", "Step 1: Merging " + std::to_string(optimInputs.size()) + " files", 0.05);
            
            gltfu::GltfMerger merger;
            for (size_t i = 0; i < optimInputs.size(); ++i) {
                double fileProgress = 0.05 + (0.05 * i / optimInputs.size());
                progress.report("optim", "Merging file " + std::to_string(i + 1) + "/" + std::to_string(optimInputs.size()), fileProgress);
                if (!merger.loadAndMergeFile(optimInputs[i], false, false)) {
                    progress.error("optim", "Merge failed: " + merger.getError());
                    return 1;
                }
            }
            
            progress.report("optim", "Extracting merged model", 0.10);
            model = merger.getMergedModel();
        } else {
            progress.report("optim", "Loading input file", 0.05);
            std::string err, warn;
            bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, optimInputs[0]);
            if (!ret) {
                progress.error("optim", "Failed to load file: " + err);
                return 1;
            }
        }
        
        // Step 2: Deduplicate (in-place)
        if (!optimSkipDedupe) {
            progress.report("optim", "Step 2: Deduplicating resources", 0.15);
            
            gltfu::GltfDedup deduper;
            gltfu::GltfDedup::Options dedupOpts;
            dedupOpts.dedupAccessors = true;
            dedupOpts.dedupMeshes = true;
            dedupOpts.dedupMaterials = true;
            dedupOpts.dedupTextures = true;
            dedupOpts.keepUniqueNames = false;
            dedupOpts.verbose = optimVerbose;
            dedupOpts.progressReporter = &progress;
            
            if (!deduper.process(model, dedupOpts)) {
                progress.error("optim", "Deduplication failed: " + deduper.getError());
                return 1;
            }
            
            if (optimVerbose && !deduper.getStats().empty()) {
                std::cout << "  " << deduper.getStats() << std::endl;
            }
        }
        
        // Step 3: Flatten scene graph (in-place)
        if (!optimSkipFlatten) {
            progress.report("optim", "Step 3: Flattening scene graph", 0.30);
            
            int flattenedCount = gltfu::GltfFlatten::process(model, true);
            
            if (optimVerbose) {
                std::cout << "  Flattened " << flattenedCount << " nodes" << std::endl;
            }
        }
        
        // Step 4: Join primitives (in-place)
        if (!optimSkipJoin) {
            progress.report("optim", "Step 4: Joining compatible primitives", 0.45);
            
            gltfu::GltfJoin joiner;
            gltfu::JoinOptions joinOpts;
            joinOpts.keepMeshes = false;
            joinOpts.keepNamed = false;
            joinOpts.verbose = optimVerbose;
            
            if (!joiner.process(model, joinOpts)) {
                progress.error("optim", "Join operation failed");
                return 1;
            }
        }
        
        // Step 5: Weld vertices (in-place)
        if (!optimSkipWeld) {
            progress.report("optim", "Step 5: Welding identical vertices", 0.60);
            
            gltfu::GltfWeld welder;
            gltfu::WeldOptions weldOpts;
            weldOpts.overwrite = true;
            weldOpts.verbose = optimVerbose;
            
            if (!welder.process(model, weldOpts)) {
                progress.error("optim", "Weld operation failed");
                return 1;
            }
        }
        
        // Step 6: Simplify (in-place, optional)
        if (optimSimplify) {
            progress.report("optim", "Step 6: Simplifying meshes", 0.75);
            
            gltfu::GltfSimplify simplifier;
            gltfu::SimplifyOptions simplifyOpts;
            simplifyOpts.ratio = optimSimplifyRatio;
            simplifyOpts.error = optimSimplifyError;
            simplifyOpts.lockBorder = optimLockBorder;
            
            if (!simplifier.process(model, simplifyOpts)) {
                progress.error("optim", "Simplify operation failed");
                return 1;
            }
        }
        
#ifdef GLTFU_ENABLE_DRACO
        // Step 6.5: Compress meshes with Draco (in-place)
        if (optimCompress) {
            progress.report("optim", "Step 6.5: Compressing meshes with Draco", 0.84);
            
            gltfu::GltfCompress compressor;
            gltfu::CompressOptions compressOpts;
            compressOpts.positionQuantizationBits = optimCompressPositionBits;
            compressOpts.normalQuantizationBits = optimCompressNormalBits;
            compressOpts.texCoordQuantizationBits = optimCompressTexcoordBits;
            compressOpts.colorQuantizationBits = optimCompressColorBits;
            compressOpts.verbose = optimVerbose;
            
            if (!compressor.process(model, compressOpts)) {
                progress.error("optim", "Compression operation failed: " + compressor.getError());
                return 1;
            }
            
            if (optimVerbose) {
                std::cout << compressor.getStats() << std::endl;
            }
        }
#endif
        
        // Step 7: Prune unused resources (in-place)
        if (!optimSkipPrune) {
            progress.report("optim", "Step 7: Pruning unused resources", 0.87);
            
            gltfu::GltfPrune pruner;
            gltfu::PruneOptions pruneOpts;
            pruneOpts.keepLeaves = false;
            pruneOpts.keepAttributes = false;
            
            if (!pruner.process(model, pruneOpts)) {
                progress.error("optim", "Prune operation failed");
                return 1;
            }
        }
        
        // Step 8: Compute bounds for all POSITION accessors (required by spec)
        progress.report("optim", "Computing accessor bounds", 0.93);
        int boundsComputed = gltfu::GltfBounds::computeAllBounds(model);
        if (optimVerbose && boundsComputed > 0) {
            std::cout << "  Computed bounds for " << boundsComputed << " accessors" << std::endl;
        }
        
        // Final step: Write output with proper settings
        progress.report("optim", "Writing optimized output", 0.95);
        
        bool writeRet;
        if (optimWriteBinary) {
            for (auto& buffer : model.buffers) {
                buffer.uri.clear();
            }
            writeRet = loader.WriteGltfSceneToFile(&model, optimOutput, 
                                                  optimEmbedImages, true, optimPrettyPrint, true);
        } else {
            writeRet = loader.WriteGltfSceneToFile(&model, optimOutput, 
                                                  optimEmbedImages, optimEmbedBuffers, optimPrettyPrint, false);
        }
        
        if (!writeRet) {
            progress.error("optim", "Failed to write final output");
            return 1;
        }
        
        progress.success("optim", "Optimization complete: " + optimOutput);
        return 0;
    });
    
    // Parse and run
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    
    return 0;
}
