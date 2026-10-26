#pragma once
#include <memory>
#include <string>
#include <vector>
#include "imgui.h"
#include "ResourceFiles.h"

// Runtime adapter selected at build time. GL textures must be destroyed before the EGL context.
class SpinePreview {
public:
    explicit SpinePreview(const ResourceFiles &files);
    static void validate(const ResourceFiles &files);
    ~SpinePreview();
    SpinePreview(const SpinePreview &) = delete;
    SpinePreview &operator=(const SpinePreview &) = delete;
    void update(float delta);
    void draw(ImDrawList *list, ImVec2 minimum, ImVec2 maximum, float scale, ImVec2 offset = ImVec2(0, 0));
    void setAnimation(int index, bool loop);
    void setSkin(int index);
    const std::vector<std::string> &animations() const;
    const std::vector<std::string> &skins() const;
    const std::string &version() const;
    float time() const;
    void setTime(float time);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
