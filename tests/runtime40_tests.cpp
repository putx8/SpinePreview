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
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    const fs::path sample(argv[1]); const std::string executable(argv[2]);
    const auto root = fs::temp_directory_path() / ("SpinePreview40-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    try {
        for (const auto *file : {"spineboy-pro.json", "spineboy-pro.skel", "spineboy-ess.json", "spineboy-ess.skel"}) {
            const auto resource = prepareResources(sample/file, sample/"spineboy.atlas");
            require(resource.version.find("4.0.") == 0, "4.0 export version");
            require(validate(executable, resource), std::string("official sample: ")+file);
        }
        fs::copy_file(sample/"spineboy-pro.skel", root/"coin-pro.skel.bytes");
        fs::copy_file(sample/"spineboy.atlas", root/"coin.atlas");
        fs::copy_file(sample/"spineboy.png", root/"spineboy.png");
        auto candidates=discoverResources({(root/"coin-pro.skel.bytes").string()});
        require(matchAtlases(candidates.skeletons[0], candidates.atlases).size()==1, "wrapped renamed matching");
        require(validate(executable, prepareResources(root/"coin-pro.skel.bytes",root/"coin.atlas")), "wrapped binary validation");
        const auto pma=prepareResources(sample/"spineboy-pro.json",sample/"spineboy-pma.atlas");
        require(pma.pages[0].premultiplied && validate(executable,pma), "PMA sample");
        fs::remove(root/"spineboy.png");
        rejects([&] { prepareResources(root/"coin-pro.skel.bytes",root/"coin.atlas"); }, "texture");
        write(root/"broken.skel",read(sample/"spineboy-pro.skel").substr(0,32));
        bool rejected=false;
        try { rejected=!validate(executable,prepareResources(root/"broken.skel",sample/"spineboy.atlas")); }
        catch (const std::exception &) { rejected=true; }
        require(rejected,"truncated binary isolation");
        std::puts("PASS: 4.0 essential/pro JSON and binary, wrapped names, PMA, missing textures and corrupt binary isolation");
    } catch (const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
