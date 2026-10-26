#pragma once
#include <filesystem>
#include <string>
#include <vector>

enum class AlphaMode { Auto, Straight, Premultiplied };
struct TexturePage {
    std::string name;
    std::filesystem::path path;
    bool premultiplied = false;
};
struct ResourceFiles {
    std::filesystem::path skeleton, atlas;
    std::string skeletonBytes, atlasText, version, versionNotice;
    std::vector<TexturePage> pages;
    AlphaMode alpha = AlphaMode::Auto;
    bool binary = false;
};
struct ResourceCandidates {
    std::vector<std::filesystem::path> skeletons, atlases, images;
};

bool isSkeletonFile(const std::filesystem::path &path);
ResourceCandidates discoverResources(const std::vector<std::string> &paths);
// Ranked matches only; equal candidates remain explicit user choices.
std::vector<std::filesystem::path> matchAtlases(const std::filesystem::path &skeleton,
                                               const std::vector<std::filesystem::path> &atlases);
std::vector<std::string> atlasPageNames(const std::filesystem::path &path);
ResourceFiles prepareResources(const std::filesystem::path &skeleton, const std::filesystem::path &atlas,
                              AlphaMode alpha = AlphaMode::Auto,
                              const std::vector<TexturePage> &overrides = {});
