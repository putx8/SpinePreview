#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "ResourceFiles.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <stdexcept>

namespace fs = std::filesystem;
static void require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
static void write(const fs::path &path, const std::string &text) { std::ofstream(path, std::ios::binary) << text; }
static std::string read(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}
static void rejects(const std::function<void()> &action, const std::string &contains) {
    try { action(); }
    catch (const std::exception &e) { require(std::string(e.what()).find(contains) != std::string::npos, e.what()); return; }
    throw std::runtime_error("Expected rejection: " + contains);
}
static bool validate(const std::string &executable, const ResourceFiles &files) {
    std::vector<std::string> values = {executable, "--validate-resource", files.skeleton.string(), files.atlas.string(),
                                     std::to_string(static_cast<int>(files.alpha))};
    for (const auto &page : files.pages) { values.push_back(page.name); values.push_back(page.path.string()); }
    std::vector<const char *> args;
    for (const auto &v : values) args.push_back(v.c_str());
    args.push_back(nullptr);
    auto props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    auto *process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    require(process, SDL_GetError());
    const auto started = SDL_GetTicks();
    int code = -1;
    while (!SDL_WaitProcess(process, false, &code)) {
        if (SDL_GetTicks()-started > 10000) {
            SDL_KillProcess(process, true); SDL_WaitProcess(process, true, nullptr); SDL_DestroyProcess(process);
            throw std::runtime_error("Worker timed out");
        }
        SDL_Delay(5);
    }
    SDL_DestroyProcess(process);
    return code == 0;
}
static std::string atlasPage(const std::string &image, const std::string &region) {
    return "\n" + image + "\nsize: 1024,256\nformat: RGBA8888\nfilter: Linear,Linear\nrepeat: none\n" +
           region + "\n rotate: false\n xy: 0,0\n size: 16,16\n orig: 16,16\n offset: 0,0\n index: -1\n";
}
static const std::string tiny = R"({"skeleton":{"spine":"3.8.99","width":32,"height":32},"bones":[{"name":"root"}],"slots":[{"name":"slot","bone":"root","attachment":"part"}],"skins":[{"name":"default","attachments":{"slot":{"part":{"width":16,"height":16}}}}],"animations":{"idle":{}}})";

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    const fs::path sample(argv[1]); const std::string executable(argv[2]);
    const auto root = fs::temp_directory_path() / ("SpinePreview-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    int passed = 0;
    auto test = [&](const char *name, const std::function<void()> &body) {
        body(); ++passed; std::printf("PASS %s\n", name);
    };
    auto copy = [&](const fs::path &from, const fs::path &to) { fs::copy_file(from, to, fs::copy_options::overwrite_existing); };
    try {
        SDL_SetMainReady(); require(SDL_Init(0), SDL_GetError());
        const auto coin = root/"coin"; fs::create_directories(coin);
        copy(sample/"spineboy-pro.skel", coin/"coin-pro.skel");
        copy(sample/"spineboy.png", coin/"coin.png");
        auto coinAtlas = read(sample/"spineboy.atlas");
        coinAtlas.replace(coinAtlas.find("spineboy.png"), 12, "coin.png");
        write(coin/"coin.atlas", coinAtlas);
        test("coin-pro.skel pairs with coin.atlas and coin.png", [&] {
            const auto candidates = discoverResources({(coin/"coin-pro.skel").string()});
            const auto matches = matchAtlases(candidates.skeletons.at(0), candidates.atlases);
            require(matches.size() == 1 && matches[0].filename() == "coin.atlas", "suffix match");
            require(validate(executable, prepareResources(candidates.skeletons[0], matches[0])), "binary validation");
        });
        test("dropping an image discovers its Atlas", [&] {
            const auto c = discoverResources({(coin/"coin.png").string()});
            require(c.atlases.size() == 1 && c.atlases[0].filename() == "coin.atlas", "image association");
        });
        const auto multi = root/"multiple pages"; fs::create_directories(multi);
        write(multi/"rig.json", tiny); copy(sample/"spineboy.png", multi/"page-a.png"); copy(sample/"spineboy.png", multi/"page-b.png");
        write(multi/"textures.atlas", atlasPage("page-a.png", "part") + atlasPage("page-b.png", "other"));
        test("multiple texture pages are loaded independently", [&] {
            const auto f = prepareResources(multi/"rig.json", multi/"textures.atlas");
            require(f.pages.size() == 2 && f.pages[0].path != f.pages[1].path, "page mapping");
            require(validate(executable, f), "multipage validation");
        });
        test("explicit mismatched skeleton and Atlas names", [&] {
            const auto c = discoverResources({(multi/"rig.json").string(), (multi/"textures.atlas").string()});
            require(c.skeletons.size() == 1 && c.atlases.size() == 1, "explicit selection");
        });
        test("ambiguous Atlas matches remain choices", [&] {
            require(matchAtlases("actor-pro.skel", {"a.atlas", "b.atlas"}).size() == 2, "ambiguous candidates");
        });
        test("missing texture reports filename; explicit override repairs it", [&] {
            write(multi/"missing.atlas", atlasPage("renamed.png", "part"));
            rejects([&] { prepareResources(multi/"rig.json", multi/"missing.atlas"); }, "renamed.png");
            const auto f = prepareResources(multi/"rig.json", multi/"missing.atlas", AlphaMode::Auto,
                                            {{"renamed.png", multi/"page-a.png", false}});
            require(validate(executable, f), "manual texture mapping");
        });
        test("uppercase extension and texture-name case", [&] {
            write(multi/"UPPER.JSON", tiny); write(multi/"UPPER.ATLAS", atlasPage("PAGE-A.PNG", "part"));
            require(isSkeletonFile(multi/"UPPER.JSON"), "uppercase skeleton extension");
            require(validate(executable, prepareResources(multi/"UPPER.JSON", multi/"UPPER.ATLAS")), "case fallback");
        });
        test("Unicode and spaces in paths", [&] {
            const auto unicode = root/fs::path(u8"角色 资源"); fs::create_directories(unicode);
            write(unicode/fs::path(u8"角色-pro.json"), tiny);
            copy(sample/"spineboy.png", unicode/fs::path(u8"纹理.png"));
            write(unicode/fs::path(u8"角色.atlas"), atlasPage(u8"纹理.png", "part"));
            require(validate(executable, prepareResources(unicode/fs::path(u8"角色-pro.json"), unicode/fs::path(u8"角色.atlas"))), "unicode validation");
        });
        test("folder with multiple skeletons retains all choices", [&] {
            copy(sample/"spineboy-pro.json", coin/"coin-pro.json");
            require(discoverResources({coin.string()}).skeletons.size() == 2, "multiple skeleton candidates");
        });
        test("wrapped .skel.bytes and .json.txt filenames", [&] {
            copy(sample/"spineboy-pro.skel", coin/"coin-pro.skel.bytes");
            copy(sample/"spineboy-pro.json", coin/"coin-pro.json.txt");
            require(validate(executable, prepareResources(coin/"coin-pro.skel.bytes", coin/"coin.atlas")), "wrapped binary");
            require(validate(executable, prepareResources(coin/"coin-pro.json.txt", coin/"coin.atlas")), "wrapped JSON");
        });
        test("incompatible runtime version rejected before parser", [&] {
            auto json = tiny; json.replace(json.find("3.8.99"), 6, "4.2.00"); write(multi/"wrong.json", json);
            rejects([&] { prepareResources(multi/"wrong.json", multi/"textures.atlas"); }, "incompatible");
        });
        test("malformed and unrelated JSON rejected", [&] {
            write(multi/"bad.json", "{invalid");
            rejects([&] { prepareResources(multi/"bad.json", multi/"textures.atlas"); }, "Invalid skeleton JSON");
            write(multi/"bad.json", "{\"settings\":true}");
            rejects([&] { prepareResources(multi/"bad.json", multi/"textures.atlas"); }, "not a versioned Spine");
        });
        test("truncated binary header rejected", [&] {
            write(multi/"bad.skel", "\x80");
            rejects([&] { prepareResources(multi/"bad.skel", multi/"textures.atlas"); }, "Truncated");
        });
        test("truncated binary body cannot crash the preview process", [&] {
            auto bytes = read(sample/"spineboy-pro.skel"); bytes.resize(64); write(multi/"truncated.skel", bytes);
            require(!validate(executable, prepareResources(multi/"truncated.skel", multi/"textures.atlas")), "truncated binary rejection");
        });
        test("wrong Atlas reports missing regions", [&] {
            require(!validate(executable, prepareResources(coin/"coin-pro.json", multi/"textures.atlas")), "wrong atlas rejection");
        });
        test("no animations supports setup pose", [&] {
            auto json = tiny; json.replace(json.find("\"idle\":{}"), 9, ""); write(multi/"static.json", json);
            require(validate(executable, prepareResources(multi/"static.json", multi/"textures.atlas")), "no animations");
        });
        test("PMA metadata and explicit alpha overrides", [&] {
            auto text = atlasPage("page-a.png", "part"); text.insert(text.find("part\n"), "pma: true\n"); write(multi/"pma.atlas", text);
            require(prepareResources(multi/"rig.json", multi/"pma.atlas").pages[0].premultiplied, "PMA metadata");
            require(!prepareResources(multi/"rig.json", multi/"pma.atlas", AlphaMode::Straight).pages[0].premultiplied, "alpha override");
        });
        test("compact Atlas bounds/offsets normalize to 3.8 format", [&] {
            write(multi/"compact.atlas", "\npage-a.png\nsize: 1024,256\npma: false\npart\n bounds: 0,0,16,16\n offsets: 0,0,16,16\n");
            require(validate(executable, prepareResources(multi/"rig.json", multi/"compact.atlas")), "compact Atlas");
        });
        test("texture dimension mismatch rejected", [&] {
            auto text = atlasPage("page-a.png", "part"); text.replace(text.find("1024,256"), 8, "512,256"); write(multi/"size.atlas", text);
            rejects([&] { prepareResources(multi/"rig.json", multi/"size.atlas"); }, "size does not match");
        });
        test("broken Atlas and out-of-bounds region rejected", [&] {
            write(multi/"bad.atlas", "page-a.png\nregion\nxy: 0,0\n");
            rejects([&] { prepareResources(multi/"rig.json", multi/"bad.atlas"); }, "missing property");
            auto text = atlasPage("page-a.png", "part"); text.replace(text.find("xy: 0,0"), 7, "xy: 1020,0"); write(multi/"bad.atlas", text);
            rejects([&] { prepareResources(multi/"rig.json", multi/"bad.atlas"); }, "exceeds texture bounds");
        });
        test("unsupported .spine project rejected", [&] {
            write(multi/"project.spine", "dummy");
            rejects([&] { discoverResources({(multi/"project.spine").string()}); }, "Unsupported");
        });
        SDL_Quit(); std::printf("%d resource regression tests passed\n", passed); return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL after %d tests: %s\n", passed, e.what()); SDL_Quit(); return 1;
    }
}
