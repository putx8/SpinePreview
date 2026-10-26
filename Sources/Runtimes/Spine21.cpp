#include "SpinePreview.h"
#include "PreviewTexture.h"
#include <spine/spine.h>
#include <spine/extension.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {
template<class T, void (*Dispose)(T*)> struct Deleter { void operator()(T *p) const { if (p) Dispose(p); } };
template<class T, void (*Dispose)(T*)> using Owned = std::unique_ptr<T, Deleter<T, Dispose>>;
struct TextureContext { std::vector<TexturePage> pages; bool validate; std::string error; };
struct Geometry {
    std::vector<float> world;
    const float *uvs = nullptr;
    const int *indices = nullptr;
    int count = 0;
    spAtlasRegion *region = nullptr;
    float r = 1, g = 1, b = 1, a = 1;
};
Geometry geometry(spSlot *slot) {
    Geometry g;
    auto *attachment = slot->attachment;
    if (!attachment) return g;
    static const int quad[] = {0, 1, 2, 2, 3, 0};
    switch (attachment->type) {
    case SP_ATTACHMENT_REGION: {
        auto *item = reinterpret_cast<spRegionAttachment*>(attachment);
        g.world.resize(8); spRegionAttachment_computeWorldVertices(item, slot->bone, g.world.data());
        g.uvs = item->uvs; g.indices = quad; g.count = 6;
        g.region = static_cast<spAtlasRegion*>(item->rendererObject);
        g.r = item->r; g.g = item->g; g.b = item->b; g.a = item->a;
        break;
    }
    case SP_ATTACHMENT_MESH: {
        auto *item = reinterpret_cast<spMeshAttachment*>(attachment);
        if (item->verticesCount < 0 || item->verticesCount > 10000000) throw std::runtime_error("Invalid mesh size");
        g.world.resize(item->verticesCount); spMeshAttachment_computeWorldVertices(item, slot, g.world.data());
        g.uvs = item->uvs; g.indices = item->triangles; g.count = item->trianglesCount;
        g.region = static_cast<spAtlasRegion*>(item->rendererObject);
        g.r = item->r; g.g = item->g; g.b = item->b; g.a = item->a;
        break;
    }
    case SP_ATTACHMENT_SKINNED_MESH: {
        auto *item = reinterpret_cast<spSkinnedMeshAttachment*>(attachment);
        if (item->uvsCount < 0 || item->uvsCount > 10000000) throw std::runtime_error("Invalid weighted mesh size");
        g.world.resize(item->uvsCount); spSkinnedMeshAttachment_computeWorldVertices(item, slot, g.world.data());
        g.uvs = item->uvs; g.indices = item->triangles; g.count = item->trianglesCount;
        g.region = static_cast<spAtlasRegion*>(item->rendererObject);
        g.r = item->r; g.g = item->g; g.b = item->b; g.a = item->a;
        break;
    }
    default: return g;
    }
    if (g.count < 0 || g.count > 10000000 || g.count % 3 || (g.count && (!g.indices || !g.uvs)))
        throw std::runtime_error("Invalid mesh triangles");
    for (int i = 0; i < g.count; ++i)
        if (g.indices[i] < 0 || static_cast<size_t>(g.indices[i])*2+1 >= g.world.size())
            throw std::runtime_error("Mesh index exceeds vertex bounds");
    for (float value : g.world) if (!std::isfinite(value)) throw std::runtime_error("Invalid skeleton vertices");
    return g;
}
void setBlend(const ImDrawList *, const ImDrawCmd *command) {
    glBlendFuncSeparate(GL_SRC_ALPHA, command->UserCallbackData ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}
}

extern "C" void _spAtlasPage_createTexture(spAtlasPage *page, const char *path) {
    auto &context = *static_cast<TextureContext*>(page->atlas->rendererObject);
    if (context.validate) return;
    try {
        const auto &pages = context.pages;
        const auto found = std::find_if(pages.begin(), pages.end(), [&](const auto &p) { return p.name == page->name; });
        const auto texture = loadPreviewTexture(found == pages.end() ? path : found->path.string(),
            found != pages.end() && found->premultiplied, page->minFilter+1, page->magFilter+1,
            page->uWrap, page->vWrap, context.error);
        page->rendererObject = reinterpret_cast<void*>(static_cast<uintptr_t>(texture));
    } catch (const std::exception &e) { context.error = e.what(); }
}
extern "C" void _spAtlasPage_disposeTexture(spAtlasPage *page) {
    if (page->rendererObject) {
        GLuint texture = static_cast<GLuint>(reinterpret_cast<uintptr_t>(page->rendererObject));
        glDeleteTextures(1, &texture);
    }
}
extern "C" char *_spUtil_readFile(const char *path, int *length) { return _readFile(path, length); }

struct SpinePreview::Impl {
    TextureContext textures;
    Owned<spAtlas, spAtlas_dispose> atlas;
    Owned<spSkeletonData, spSkeletonData_dispose> data;
    Owned<spSkeleton, spSkeleton_dispose> skeleton;
    Owned<spAnimationStateData, spAnimationStateData_dispose> stateData;
    Owned<spAnimationState, spAnimationState_dispose> state;
    std::vector<std::string> animations, skins;
    std::string version;
    float x = 0, y = 0, width = 100, height = 100;
    explicit Impl(const ResourceFiles &files, bool validate = false) : textures{files.pages, validate, {}} {
        if (files.binary) throw std::runtime_error("Spine 2.1.25 C runtime supports JSON only");
        spBone_setYDown(0);
        const auto directory = files.atlas.parent_path().string();
        atlas.reset(spAtlas_create(files.atlasText.data(), static_cast<int>(files.atlasText.size()), directory.c_str(), &textures));
        if (!textures.error.empty()) throw std::runtime_error(textures.error);
        if (!atlas || !atlas->pages) throw std::runtime_error("Cannot read Spine 2.1 atlas");
        Owned<spSkeletonJson, spSkeletonJson_dispose> reader(spSkeletonJson_create(atlas.get()));
        data.reset(spSkeletonJson_readSkeletonData(reader.get(), files.skeletonBytes.c_str()));
        if (!data) throw std::runtime_error(std::string("Cannot read Spine JSON: ") + (reader->error ? reader->error : "unknown error"));
        if (!data->bonesCount) throw std::runtime_error("Skeleton has no bones");
        version = files.version;
        for (int i = 0; i < data->animationsCount; ++i) animations.emplace_back(data->animations[i]->name);
        for (int i = 0; i < data->skinsCount; ++i) skins.emplace_back(data->skins[i]->name);
        skeleton.reset(spSkeleton_create(data.get()));
        if (!data->defaultSkin && data->skinsCount) spSkeleton_setSkin(skeleton.get(), data->skins[0]);
        spSkeleton_setToSetupPose(skeleton.get()); spSkeleton_updateWorldTransform(skeleton.get());
        float minX = std::numeric_limits<float>::infinity(), minY = minX, maxX = -minX, maxY = -minY;
        for (int i = 0; i < skeleton->slotsCount; ++i) {
            const auto g = geometry(skeleton->drawOrder[i]);
            for (size_t j = 0; j < g.world.size(); j += 2) {
                minX = std::min(minX, g.world[j]); maxX = std::max(maxX, g.world[j]);
                minY = std::min(minY, g.world[j+1]); maxY = std::max(maxY, g.world[j+1]);
            }
        }
        if (std::isfinite(minX) && maxX > minX && maxY > minY) { x = minX; y = minY; width = maxX-minX; height = maxY-minY; }
        stateData.reset(spAnimationStateData_create(data.get())); stateData->defaultMix = 0.2f;
        state.reset(spAnimationState_create(stateData.get()));
    }
};
SpinePreview::SpinePreview(const ResourceFiles &files) : impl(new Impl(files)) {
    const auto found = std::find(impl->animations.begin(), impl->animations.end(), "walk");
    setAnimation(found == impl->animations.end() ? 0 : static_cast<int>(found-impl->animations.begin()), true);
}
SpinePreview::~SpinePreview() = default;
void SpinePreview::validate(const ResourceFiles &files) {
    Impl s(files, true);
    for (int skin = 0; skin < std::max(1, s.data->skinsCount); ++skin) {
        if (s.data->skinsCount) spSkeleton_setSkin(s.skeleton.get(), s.data->skins[skin]);
        for (int i = 0; i < std::max(1, s.data->animationsCount); ++i) {
            spSkeleton_setToSetupPose(s.skeleton.get());
            if (s.data->animationsCount) spAnimationState_setAnimation(s.state.get(), 0, s.data->animations[i], 0);
            spAnimationState_apply(s.state.get(), s.skeleton.get()); spSkeleton_updateWorldTransform(s.skeleton.get());
            for (int slot = 0; slot < s.skeleton->slotsCount; ++slot) geometry(s.skeleton->drawOrder[slot]);
        }
    }
}
const std::vector<std::string> &SpinePreview::animations() const { return impl->animations; }
const std::vector<std::string> &SpinePreview::skins() const { return impl->skins; }
const std::string &SpinePreview::version() const { return impl->version; }
float SpinePreview::time() const { auto *t = spAnimationState_getCurrent(impl->state.get(), 0); return t ? t->time : 0; }
void SpinePreview::setTime(float time) {
    if (auto *t = spAnimationState_getCurrent(impl->state.get(), 0)) {
        auto *animation = t->animation;
        const int loop = t->loop;
        // The 2.1 mixer divides by mixDuration even when it is zero. Seeking
        // starts a fresh track so a previous animation cannot produce NaNs.
        spAnimationState_clearTrack(impl->state.get(), 0);
        t = spAnimationState_setAnimation(impl->state.get(), 0, animation, loop);
        t->time = std::max(0.0f, time); t->lastTime = t->time; t->mixDuration = 0;
        spAnimationState_apply(impl->state.get(), impl->skeleton.get());
        spSkeleton_updateWorldTransform(impl->skeleton.get());
    }
}
void SpinePreview::setAnimation(int index, bool loop) {
    if (index >= 0 && static_cast<size_t>(index) < impl->animations.size())
        spAnimationState_setAnimation(impl->state.get(), 0, impl->data->animations[index], loop);
}
void SpinePreview::setSkin(int index) {
    if (index >= 0 && static_cast<size_t>(index) < impl->skins.size()) {
        spSkeleton_setSkin(impl->skeleton.get(), impl->data->skins[index]);
        spSkeleton_setSlotsToSetupPose(impl->skeleton.get());
    }
}
void SpinePreview::update(float delta) {
    auto &s = *impl;
    spSkeleton_update(s.skeleton.get(), delta);
    auto *t = spAnimationState_getCurrent(s.state.get(), 0);
    // This old runtime clears finished tracks; hold the final pose for the viewer.
    if (!t || t->loop || t->time < t->endTime) {
        if (t && !t->loop) delta = std::min(delta, std::max(0.0f, t->endTime-t->time));
        spAnimationState_update(s.state.get(), delta);
    }
    spAnimationState_apply(s.state.get(), s.skeleton.get()); spSkeleton_updateWorldTransform(s.skeleton.get());
}
void SpinePreview::draw(ImDrawList *list, ImVec2 minimum, ImVec2 maximum, float userScale, ImVec2 offset) {
    auto &s = *impl;
    const float zoom = 0.8f*userScale*std::min((maximum.x-minimum.x)/s.width, (maximum.y-minimum.y)/s.height);
    list->PushClipRect(minimum, maximum, true);
    for (int i = 0; i < s.skeleton->slotsCount; ++i) {
        auto *slot = s.skeleton->drawOrder[i]; const auto g = geometry(slot);
        if (!g.region || !g.count) continue;
        const auto tint = ImGui::ColorConvertFloat4ToU32(ImVec4(s.skeleton->r*slot->r*g.r, s.skeleton->g*slot->g*g.g,
            s.skeleton->b*slot->b*g.b, s.skeleton->a*slot->a*g.a));
        list->AddCallback(setBlend, reinterpret_cast<void*>(static_cast<intptr_t>(slot->data->additiveBlending)));
        list->PushTexture(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(g.region->page->rendererObject))));
        list->PrimReserve(g.count, g.count);
        for (int j = 0; j < g.count; ++j) {
            const int v = g.indices[j]*2;
            list->PrimWriteIdx(static_cast<ImDrawIdx>(list->_VtxCurrentIdx));
            list->PrimWriteVtx(ImVec2((minimum.x+maximum.x)*0.5f+offset.x+(g.world[v]-s.x-s.width*0.5f)*zoom,
                (minimum.y+maximum.y)*0.5f+offset.y-(g.world[v+1]-s.y-s.height*0.5f)*zoom), ImVec2(g.uvs[v],g.uvs[v+1]), tint);
        }
        list->PopTexture();
    }
    list->AddCallback(ImDrawCallback_ResetRenderState, nullptr); list->PopClipRect();
}
