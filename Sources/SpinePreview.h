#pragma once
#include <memory>
#include <string>
#include <vector>
#include "imgui.h"
#include "ResourceFiles.h"

// A 3.8 runtime instance. GL textures must be destroyed before the EGL context.
class SpinePreview {
public:
    explicit SpinePreview(const ResourceFiles &files);
    static void validate(const ResourceFiles &files);
    ~SpinePreview();
    SpinePreview(const SpinePreview &) = delete;
    SpinePreview &operator=(const SpinePreview &) = delete;
    void update(float delta);
    void draw(ImDrawList *list, ImVec2 minimum, ImVec2 maximum, float scale);
    void setAnimation(int index, bool loop);
    void setSkin(int index);
    const std::vector<std::string> &animations() const;
    const std::vector<std::string> &skins() const;
    const std::string &version() const;
    float time() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
