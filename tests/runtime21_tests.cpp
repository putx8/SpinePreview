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
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_INHERITED);
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
    return "\n"+image+"\nformat: RGBA8888\nfilter: Linear,Linear\nrepeat: none\n"+region+
        "\n rotate: true\n xy: 0,0\n size: 16,16\n orig: 16,16\n offset: 0,0\n index: -1\n";
}
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    const fs::path sample(argv[1]); const std::string executable(argv[2]);
    const auto root = fs::temp_directory_path() / ("SpinePreview21-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    try {
        auto official = prepareResources(sample/"spineboy.json", sample/"spineboy.atlas");
        require(official.version == "unknown (legacy)", "legacy version disclosure");
        require(validate(executable, official), "official legacy sample");
        const fs::path raptor(argv[3]);
        auto dev = prepareResources(raptor/"raptor.json", raptor/"raptor.atlas");
        require(dev.version == "Dev", "retain the actual Dev export marker");
        require(dev.atlasText.find("size: 1024,1024") != std::string::npos, "legacy Atlas uses actual PNG size");
        require(validate(executable, dev), "official Dev Raptor sample");
        fs::copy_file(sample/"spineboy.png", root/"coin.png");
        fs::copy_file(sample/"spineboy.png", root/"coin2.png");
        const std::string rig = R"({"skeleton":{"spine":"2.1.25"},"bones":[{"name":"root"}],"slots":[{"name":"slot","bone":"root","attachment":"part"}],"skins":{"default":{"slot":{"part":{"width":16,"height":16}}}},"animations":{}})";
        write(root/"coin-pro.json", rig); write(root/"coin.atlas", atlasPage("coin.png", "part"));
        const auto candidates = discoverResources({(root/"coin-pro.json").string()});
        require(matchAtlases(candidates.skeletons[0], candidates.atlases)[0] == fs::canonical(root/"coin.atlas"), "renamed matching");
        auto prepared = prepareResources(root/"coin-pro.json", root/"coin.atlas");
        require(prepared.atlasText.find("rotate: true") != std::string::npos, "2.1 atlas rotation");
        require(validate(executable, prepared), "versioned setup pose");
        write(root/"coin.atlas", atlasPage("coin.png", "part")+atlasPage("coin2.png", "other"));
        prepared = prepareResources(root/"coin-pro.json", root/"coin.atlas", AlphaMode::Premultiplied);
        require(prepared.pages.size() == 2 && prepared.pages[0].premultiplied && prepared.pages[1].premultiplied, "multipage PMA");
        require(validate(executable, prepared), "multipage validation");
        auto patch15 = rig; patch15.replace(patch15.find("2.1.25"), 6, "2.1.15");
        write(root/"patch15.json", patch15);
        require(validate(executable, prepareResources(root/"patch15.json", root/"coin.atlas")), "explicit 2.1.15 version");
        auto unknown = rig; unknown.replace(unknown.find("2.1.25"), 6, "Other"); write(root/"unknown.json", unknown);
        auto unknownFiles = prepareResources(root/"unknown.json", root/"coin.atlas");
        require(!unknownFiles.versionNotice.empty() && validate(executable, unknownFiles), "unknown version warns and parses");
        auto wrong = rig; wrong.replace(wrong.find("2.1.25"), 6, "3.8.55"); write(root/"wrong.json", wrong);
        auto mismatch = prepareResources(root/"wrong.json", root/"coin.atlas");
        require(!mismatch.versionNotice.empty() && validate(executable, mismatch), "mismatched version warns and parses");
        write(root/"wrong.skel", "broken");
        rejects([&] { prepareResources(root/"wrong.skel", root/"coin.atlas"); }, "JSON only");
        write(root/"coin.atlas", atlasPage("coin.png", "missing"));
        require(!validate(executable, prepareResources(root/"coin-pro.json", root/"coin.atlas")), "missing region rejection");
        auto outside = atlasPage("coin.png", "part"); outside.replace(outside.find("xy: 0,0"), 7, "xy: 40000,0");
        write(root/"outside.atlas", outside);
        rejects([&] { prepareResources(root/"coin-pro.json", root/"outside.atlas"); }, "exceeds texture bounds");
        write(root/"coin.atlas", atlasPage("coin.png", "part"));
        for (const auto &type : {std::string("mesh"), std::string("skinnedmesh")}) {
            auto mesh = rig;
            const auto start = mesh.find("{\"width\":16,\"height\":16}");
            const std::string vertices = type == "mesh" ? "0,0,16,0,0,16" : "1,0,0,0,1,1,0,16,0,1,1,0,0,16,1";
            const auto attachment = "{\"type\":\""+type+"\",\"uvs\":[0,0,1,0,0,1],\"triangles\":[0,1,2],\"vertices\":["+vertices+"]}";
            mesh.replace(start, std::string("{\"width\":16,\"height\":16}").size(), attachment);
            write(root/"coin-pro.json", mesh);
            require(validate(executable, prepareResources(root/"coin-pro.json", root/"coin.atlas")), type+" validation");
            mesh.replace(mesh.find("[0,1,2]"), 7, "[0,1,99]"); write(root/"coin-pro.json", mesh);
            require(!validate(executable, prepareResources(root/"coin-pro.json", root/"coin.atlas")), "bad mesh index rejection");
        }
        // Syntactically valid JSON with an invalid bone graph must fail in the isolated worker.
        write(root/"coin-pro.json", R"({"skeleton":{"spine":"2.1.25"},"bones":[{"name":"root","parent":"missing"}]})");
        require(!validate(executable, prepareResources(root/"coin-pro.json", root/"coin.atlas")), "invalid bone graph isolation");
        std::puts("PASS: 2.1 legacy/versioned JSON, Dev Raptor, 2.1.15, setup pose, renamed resources, rotation, multipage/PMA, version warnings/binary rejection, missing regions, meshes and worker isolation");
    } catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
