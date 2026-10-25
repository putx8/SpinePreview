#import <Foundation/Foundation.h>
#include <ImageIO/ImageIO.h>
#include "ResourceFiles.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool ends(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() && s.compare(s.size()-suffix.size(), suffix.size(), suffix) == 0;
}
std::string trim(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
std::string readFile(const fs::path &path, size_t maximum) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size == 0 || size > maximum)
        throw std::runtime_error("File missing, empty, or too large: " + path.string());
    std::ifstream in(path, std::ios::binary);
    std::string data(static_cast<size_t>(size), '\0');
    if (!in.read(data.data(), static_cast<std::streamsize>(size)))
        throw std::runtime_error("Cannot read: " + path.string());
    return data;
}
bool isAtlas(const fs::path &p) {
    const auto s = lower(p.filename().string());
    return ends(s, ".atlas") || ends(s, ".atlas.txt");
}
bool isImage(const fs::path &p) {
    const auto s = lower(p.extension().string());
    return s == ".png" || s == ".jpg" || s == ".jpeg" || s == ".webp" || s == ".tga";
}
std::string stem(fs::path p) {
    std::string s = lower(p.filename().string());
    for (auto suffix : {".json.txt", ".skel.bytes", ".atlas.txt", ".json", ".skel", ".atlas"})
        if (ends(s, suffix)) { s.resize(s.size()-std::strlen(suffix)); break; }
    return s;
}
std::string baseStem(std::string s) {
    bool changed;
    do {
        changed = false;
        for (auto suffix : {"-pro", "-ess", "_pro", "_ess", "-pma", "_pma", "-skeleton", "_skeleton"})
            if (ends(s, suffix)) { s.resize(s.size()-std::strlen(suffix)); changed = true; break; }
    } while (changed);
    return s;
}
using Properties = std::map<std::string, std::string>;
struct Region { std::string name; Properties properties; };
struct Page { std::string name; Properties properties; std::vector<Region> regions; };
std::vector<Page> parseAtlas(const fs::path &path) {
    auto text = readFile(path, 8*1024*1024);
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0) text.erase(0, 3);
    if (text.find('\0') != std::string::npos) throw std::runtime_error("Atlas contains binary data");
    std::vector<Page> pages;
    std::istringstream stream(text);
    std::string line;
    bool newPage = true;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) { newPage = true; continue; }
        if (line.size() > 8192) throw std::runtime_error("Atlas line is too long");
        if (newPage) {
            if (pages.size() >= 64) throw std::runtime_error("Atlas has more than 64 texture pages");
            pages.push_back({line, {}, {}}); newPage = false; continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            pages.back().regions.push_back({line, {}});
        } else {
            auto &page = pages.back();
            auto &props = page.regions.empty() ? page.properties : page.regions.back().properties;
            const auto key = lower(trim(line.substr(0, colon)));
            if (!props.emplace(key, trim(line.substr(colon+1))).second)
                throw std::runtime_error("Duplicate atlas property: " + key);
        }
    }
    if (pages.empty()) throw std::runtime_error("Atlas has no pages: " + path.string());
    std::set<std::string> pageNames, regionNames;
    for (const auto &page : pages) {
        if (!pageNames.insert(page.name).second) throw std::runtime_error("Duplicate atlas page: " + page.name);
        for (const auto &region : page.regions)
            if (!regionNames.insert(region.name).second) throw std::runtime_error("Duplicate atlas region: " + region.name);
    }
    return pages;
}
std::vector<int> tuple(const Properties &props, const std::string &key, size_t count,
                       std::vector<int> fallback = {}) {
    const auto found = props.find(key);
    if (found == props.end()) {
        if (!fallback.empty()) return fallback;
        throw std::runtime_error("Atlas is missing property: " + key);
    }
    std::istringstream stream(found->second);
    std::string value;
    std::vector<int> numbers;
    while (std::getline(stream, value, ',')) {
        value = trim(value);
        size_t used = 0;
        int number;
        try { number = std::stoi(value, &used); }
        catch (...) { throw std::runtime_error("Invalid atlas value: " + key); }
        if (used != value.size() || number < -1000000 || number > 1000000)
            throw std::runtime_error("Invalid atlas value: " + key);
        numbers.push_back(number);
    }
    if (numbers.size() != count) throw std::runtime_error("Invalid atlas tuple: " + key);
    return numbers;
}
std::string value(const Properties &props, const std::string &key, const std::string &fallback) {
    const auto found = props.find(key);
    return found == props.end() ? fallback : found->second;
}
fs::path resolveTexture(const fs::path &path) {
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) return path;
    std::vector<fs::path> matches;
    for (fs::directory_iterator it(path.parent_path(), fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (lower(it->path().filename().string()) == lower(path.filename().string()) && it->is_regular_file())
            matches.push_back(it->path());
    }
    if (matches.size() == 1) return matches.front();
    return path;
}
fs::path texturePath(const fs::path &directory, std::string name) {
    // Windows-exported atlases may contain backslashes in relative page paths.
    std::replace(name.begin(), name.end(), '\\', '/');
    return resolveTexture(directory/fs::path(name));
}
std::pair<int,int> imageSize(const fs::path &path) {
    const auto name = path.string();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr,
        reinterpret_cast<const UInt8 *>(name.data()), name.size(), false);
    CGImageSourceRef source = url ? CGImageSourceCreateWithURL(url, nullptr) : nullptr;
    if (url) CFRelease(url);
    CFDictionaryRef props = source ? CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr) : nullptr;
    if (source) CFRelease(source);
    int width = 0, height = 0;
    if (props) {
        auto w = static_cast<CFNumberRef>(CFDictionaryGetValue(props, kCGImagePropertyPixelWidth));
        auto h = static_cast<CFNumberRef>(CFDictionaryGetValue(props, kCGImagePropertyPixelHeight));
        if (w && h) { CFNumberGetValue(w, kCFNumberIntType, &width); CFNumberGetValue(h, kCFNumberIntType, &height); }
        CFRelease(props);
    }
    if (width <= 0 || height <= 0) throw std::runtime_error("Missing or unreadable texture: " + name);
    if (width > 32768 || height > 32768 || static_cast<uint64_t>(width)*height*4 > 128*1024*1024)
        throw std::runtime_error("Texture exceeds 128 MiB decoded limit: " + name);
    return {width, height};
}
std::string binaryString(const std::string &bytes, size_t &offset) {
    uint32_t length = 0;
    for (unsigned shift = 0; shift <= 28; shift += 7) {
        if (offset >= bytes.size()) throw std::runtime_error("Truncated skeleton binary header");
        const unsigned byte = static_cast<unsigned char>(bytes[offset++]);
        if (shift == 28 && (byte & 0xf0)) throw std::runtime_error("Invalid skeleton binary header");
        length |= (byte & 127) << shift;
        if (!(byte & 128)) {
            if (!length) return "";
            --length;
            if (length > bytes.size()-offset || length > 4096) throw std::runtime_error("Invalid skeleton binary string");
            std::string result = bytes.substr(offset, length); offset += length; return result;
        }
    }
    throw std::runtime_error("Invalid skeleton binary header");
}
}

bool isSkeletonFile(const fs::path &path) {
    const auto s = lower(path.filename().string());
    return ends(s, ".json") || ends(s, ".skel") || ends(s, ".json.txt") || ends(s, ".skel.bytes");
}

ResourceCandidates discoverResources(const std::vector<std::string> &paths) {
    ResourceCandidates result;
    std::set<fs::path> files, directories, explicitSkeletons, explicitAtlases;
    auto addFile = [&](const fs::path &p, bool explicitFile) {
        files.insert(p);
        if (explicitFile && isSkeletonFile(p)) explicitSkeletons.insert(p);
        if (explicitFile && isAtlas(p)) explicitAtlases.insert(p);
    };
    for (const auto &name : paths) {
        std::error_code ec;
        const auto p = fs::weakly_canonical(fs::absolute(fs::path(name)), ec);
        if (ec || !fs::exists(p)) throw std::runtime_error("Dropped path does not exist: " + name);
        if (fs::is_directory(p)) directories.insert(p);
        else if (fs::is_regular_file(p)) {
            if (!isSkeletonFile(p) && !isAtlas(p) && !isImage(p))
                throw std::runtime_error("Drop exported .json/.skel, .atlas, texture images, or a folder. Unsupported: " + name);
            addFile(p, true); directories.insert(p.parent_path());
            const auto folder = lower(p.parent_path().filename().string());
            if (isImage(p) && (folder == "images" || folder == "textures" || folder == "pages"))
                directories.insert(p.parent_path().parent_path());
        } else throw std::runtime_error("Unsupported file: " + name);
    }
    size_t scanned = 0;
    for (const auto &dir : directories) {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (++scanned > 10000) throw std::runtime_error("Too many files. Drop the skeleton and atlas directly.");
            if (it->is_directory() && (it.depth() >= 2 || it->path().filename().string().front() == '.'))
                it.disable_recursion_pending();
            if (it->is_regular_file() && (isSkeletonFile(it->path()) || isAtlas(it->path()) || isImage(it->path())))
                addFile(it->path(), false);
        }
    }
    for (const auto &p : files) {
        if (isSkeletonFile(p) && (explicitSkeletons.empty() || explicitSkeletons.count(p))) result.skeletons.push_back(p);
        else if (isAtlas(p) && (explicitAtlases.empty() || explicitAtlases.count(p))) result.atlases.push_back(p);
        else if (isImage(p)) result.images.push_back(p);
    }
    // A dropped image can identify its atlas by a page reference, independently
    // of either the skeleton's name or the image's filename suffix.
    if (explicitAtlases.empty()) {
        std::vector<fs::path> referenced;
        for (const auto &p : result.atlases) {
            try {
                for (const auto &page : atlasPageNames(p)) {
                    const auto texture = texturePath(p.parent_path(), page);
                    if (std::any_of(paths.begin(), paths.end(), [&](const auto &input) {
                        std::error_code ec;
                        return fs::equivalent(texture, fs::path(input), ec) && !ec;
                    })) { referenced.push_back(p); break; }
                }
            } catch (const std::exception &) { /* invalid candidates are reported if selected */ }
        }
        if (!referenced.empty()) result.atlases = referenced;
    }
    if (result.skeletons.empty()) throw std::runtime_error("No .json or .skel skeleton found. .spine projects must be exported first.");
    return result;
}

std::vector<fs::path> matchAtlases(const fs::path &skeleton, const std::vector<fs::path> &atlases) {
    std::vector<fs::path> matches;
    int best = 0;
    for (const auto &atlas : atlases) {
        const auto a = stem(atlas), s = stem(skeleton);
        int score = a == s ? 100 : (a == baseStem(s) ? 90 : (baseStem(a) == baseStem(s) ? 80 : 0));
        if (score && atlas.parent_path() == skeleton.parent_path()) score += 5;
        if (score > best) { matches.clear(); best = score; }
        if (score == best) matches.push_back(atlas);
    }
    return matches;
}

std::vector<std::string> atlasPageNames(const fs::path &path) {
    std::vector<std::string> names;
    for (const auto &page : parseAtlas(path)) names.push_back(page.name);
    return names;
}

ResourceFiles prepareResources(const fs::path &skeleton, const fs::path &atlas, AlphaMode alpha,
                              const std::vector<TexturePage> &overrides) {
    ResourceFiles result;
    result.skeleton = skeleton; result.atlas = atlas; result.alpha = alpha;
    result.skeletonBytes = readFile(skeleton, 64*1024*1024);
    const auto filename = lower(skeleton.filename().string());
    result.binary = ends(filename, ".skel") || ends(filename, ".skel.bytes");
    if (result.binary) {
        size_t offset = 0;
        binaryString(result.skeletonBytes, offset);
        result.version = binaryString(result.skeletonBytes, offset);
        if (result.skeletonBytes.size()-offset < 17) throw std::runtime_error("Truncated skeleton binary");
    } else {
        @autoreleasepool {
            NSData *data = [NSData dataWithBytes:result.skeletonBytes.data() length:result.skeletonBytes.size()];
            NSError *error = nil;
            id root = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
            if (![root isKindOfClass:[NSDictionary class]]) throw std::runtime_error("Invalid skeleton JSON");
            id bones = root[@"bones"];
            id metadata = root[@"skeleton"];
            if (![bones isKindOfClass:[NSArray class]] || [bones count] == 0 ||
                ![metadata isKindOfClass:[NSDictionary class]] || ![metadata[@"spine"] isKindOfClass:[NSString class]])
                throw std::runtime_error("JSON is not a versioned Spine skeleton");
            result.version = [metadata[@"spine"] UTF8String];
            // Re-encode valid JSON to remove BOM and embedded NUL ambiguities.
            NSData *normalized = [NSJSONSerialization dataWithJSONObject:root options:0 error:&error];
            result.skeletonBytes.assign(static_cast<const char *>(normalized.bytes), normalized.length);
        }
    }
    if (result.version != "3.8" && result.version.compare(0, 4, "3.8.") != 0)
        throw std::runtime_error("Resource version " + result.version + " is incompatible. This app supports Spine 3.8.");
    if (result.version == "3.8.75") throw std::runtime_error("Spine runtime rejects 3.8.75. Re-export with a supported 3.8 patch version.");
    std::ostringstream normalized;
    uint64_t textureBytes = 0;
    for (const auto &page : parseAtlas(atlas)) {
        auto path = texturePath(atlas.parent_path(), page.name);
        for (const auto &overridePage : overrides) if (overridePage.name == page.name && !overridePage.path.empty()) path = overridePage.path;
        const auto [width, height] = imageSize(path);
        textureBytes += static_cast<uint64_t>(width)*height*4;
        if (textureBytes > 512*1024*1024) throw std::runtime_error("Atlas textures exceed 512 MiB decoded limit");
        if (page.properties.count("size")) {
            const auto size = tuple(page.properties, "size", 2);
            if (size[0] != width || size[1] != height) throw std::runtime_error("Atlas size does not match texture: " + page.name);
        }
        bool pma = lower(value(page.properties, "pma", "false")) == "true" || ends(stem(atlas), "-pma") || ends(stem(atlas), "_pma");
        if (alpha != AlphaMode::Auto) pma = alpha == AlphaMode::Premultiplied;
        result.pages.push_back({page.name, path, pma});
        const auto filter = value(page.properties, "filter", "Linear,Linear");
        const auto repeat = value(page.properties, "repeat", "none");
        const std::set<std::string> filters = {"Nearest", "Linear", "MipMap", "MipMapNearestNearest", "MipMapLinearNearest", "MipMapNearestLinear", "MipMapLinearLinear"};
        const auto comma = filter.find(',');
        if (comma == std::string::npos || !filters.count(trim(filter.substr(0, comma))) ||
            !filters.count(trim(filter.substr(comma+1)))) throw std::runtime_error("Invalid atlas filter");
        if (repeat != "none" && repeat != "x" && repeat != "y" && repeat != "xy") throw std::runtime_error("Invalid atlas repeat");
        normalized << '\n' << page.name << "\nsize: " << width << ',' << height
                   << "\nformat: RGBA8888\nfilter: " << filter << "\nrepeat: " << repeat << '\n';
        for (const auto &region : page.regions) {
            const auto &p = region.properties;
            std::vector<int> xy, size, original, offset;
            if (p.count("bounds")) {
                const auto bounds = tuple(p, "bounds", 4); xy = {bounds[0], bounds[1]}; size = {bounds[2], bounds[3]};
            } else { xy = tuple(p, "xy", 2); size = tuple(p, "size", 2); }
            if (p.count("offsets")) {
                const auto offsets = tuple(p, "offsets", 4); offset = {offsets[0], offsets[1]}; original = {offsets[2], offsets[3]};
            } else { offset = tuple(p, "offset", 2, {0, 0}); original = tuple(p, "orig", 2, size); }
            auto rotation = lower(value(p, "rotate", "false"));
            if (rotation == "true") rotation = "90";
            if (rotation == "false") rotation = "0";
            if (rotation != "0" && rotation != "90") throw std::runtime_error("Spine 3.8 preview supports atlas rotations 0 and 90 only");
            if (xy[0] < 0 || xy[1] < 0 || size[0] <= 0 || size[1] <= 0 || original[0] <= 0 || original[1] <= 0 ||
                xy[0] + size[rotation == "90" ? 1 : 0] > width || xy[1] + size[rotation == "90" ? 0 : 1] > height)
                throw std::runtime_error("Atlas region exceeds texture bounds: " + region.name);
            const auto index = tuple(p, "index", 1, {-1});
            normalized << region.name << "\n  rotate: " << rotation << "\n  xy: " << xy[0] << ',' << xy[1]
                       << "\n  size: " << size[0] << ',' << size[1];
            for (auto key : {"split", "pad"}) if (p.count(key)) {
                const auto values = tuple(p, key, 4);
                normalized << "\n  " << key << ": " << values[0] << ',' << values[1] << ',' << values[2] << ',' << values[3];
            }
            normalized << "\n  orig: " << original[0] << ',' << original[1] << "\n  offset: " << offset[0] << ',' << offset[1]
                       << "\n  index: " << index[0] << '\n';
        }
    }
    result.atlasText = normalized.str();
    return result;
}
