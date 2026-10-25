#pragma once
#include "ResourceFiles.h"
#include <memory>
#include <optional>
#include <SDL3/SDL.h>

class FileLoader {
public:
    FileLoader();
    ~FileLoader();
    void drop(const std::string &path);
    void update();
    void draw(SDL_Window *window);
    std::optional<ResourceFiles> takeValidated();
    void loaded(const ResourceFiles &files);
    void failed(const std::string &message);
    void cancel();
    void open(SDL_Window *window);
    void reload();
    bool wantsPanel();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Entry point for a short-lived, windowless validation worker. Keeps parser
// failures in old runtime code from terminating the preview application.
int validateResourceArguments(int argc, char *argv[]);
