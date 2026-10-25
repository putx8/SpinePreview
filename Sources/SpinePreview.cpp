#include "SpinePreview.h"
#include <spine/spine.h>
#include <GLES2/gl2.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <stdexcept>

namespace spine {
SpineExtension *getDefaultExtension() { return new DefaultSpineExtension(); }
}

namespace {
// ImageIO is the only platform-specific part of the initial texture loader.
// It decodes into premultiplied RGBA, then converts to the straight alpha that
// the ImGui shader expects. Use the sample's standard (not -pma) atlas.
class TextureLoader final : public spine::TextureLoader {
public:
    std::vector<TexturePage> pages;
    std::string error;
    void load(spine::AtlasPage &page, const spine::String &path) override {
        const auto found = std::find_if(pages.begin(), pages.end(), [&](const auto &p) { return p.name == page.name.buffer(); });
        const auto filename = found == pages.end() ? std::string(path.buffer()) : found->path.string();
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr,
            reinterpret_cast<const UInt8 *>(filename.data()), filename.size(), false);
        CGImageSourceRef source = url ? CGImageSourceCreateWithURL(url, nullptr) : nullptr;
        if (url) CFRelease(url);
        CGImageRef image = source ? CGImageSourceCreateImageAtIndex(source, 0, nullptr) : nullptr;
        if (source) CFRelease(source);
        if (!image) { error = std::string("Cannot decode atlas image: ") + filename; return; }
        const size_t width = CGImageGetWidth(image), height = CGImageGetHeight(image);
        GLint limit = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
        if (!width || !height || width > static_cast<size_t>(limit) || height > static_cast<size_t>(limit)) {
            CGImageRelease(image);
            error = "Atlas texture dimensions exceed the GPU limit";
            return;
        }
        std::vector<unsigned char> pixels(width * height * 4);
        CGColorSpaceRef colors = CGColorSpaceCreateDeviceRGB();
        CGContextRef bitmap = CGBitmapContextCreate(pixels.data(), width, height, 8, width * 4, colors,
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(colors);
        if (!bitmap) { CGImageRelease(image); error = "Cannot allocate atlas bitmap"; return; }
        CGContextDrawImage(bitmap, CGRectMake(0, 0, width, height), image);
        CGContextRelease(bitmap);
        CGImageRelease(image);
        const int passes = found != pages.end() && found->premultiplied ? 2 : 1;
        for (int pass = 0; pass < passes; ++pass) for (size_t i = 0; i < pixels.size(); i += 4) {
            const unsigned alpha = pixels[i + 3];
            if (alpha) for (size_t c = 0; c < 3; ++c)
                pixels[i + c] = static_cast<unsigned char>(std::min(255u, (pixels[i + c] * 255u + alpha / 2) / alpha));
        }
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        const bool mipmap = page.minFilter >= spine::TextureFilter_MipMap;
        const GLenum minFilters[] = {GL_LINEAR, GL_NEAREST, GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR,
            GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_LINEAR};
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilters[page.minFilter]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, page.magFilter == spine::TextureFilter_Nearest ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, page.uWrap == spine::TextureWrap_Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, page.vWrap == spine::TextureWrap_Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (mipmap) glGenerateMipmap(GL_TEXTURE_2D);
        if (glGetError() != GL_NO_ERROR) {
            glDeleteTextures(1, &texture);
            error = "Cannot upload atlas texture to ANGLE";
            return;
        }
        page.width = static_cast<int>(width);
        page.height = static_cast<int>(height);
        page.setRendererObject(reinterpret_cast<void *>(static_cast<uintptr_t>(texture)));
    }
    void unload(void *object) override {
        GLuint texture = static_cast<GLuint>(reinterpret_cast<uintptr_t>(object));
        glDeleteTextures(1, &texture);
    }
};

class AttachmentLoader final : public spine::AtlasAttachmentLoader {
public:
    std::string missing;
    explicit AttachmentLoader(spine::Atlas *atlas) : spine::AtlasAttachmentLoader(atlas) {}
    spine::RegionAttachment *newRegionAttachment(spine::Skin &skin, const spine::String &name, const spine::String &path) override {
        auto *result = spine::AtlasAttachmentLoader::newRegionAttachment(skin, name, path);
        if (!result) missing = path.buffer();
        return result;
    }
    spine::MeshAttachment *newMeshAttachment(spine::Skin &skin, const spine::String &name, const spine::String &path) override {
        auto *result = spine::AtlasAttachmentLoader::newMeshAttachment(skin, name, path);
        if (!result) missing = path.buffer();
        return result;
    }
};

std::unique_ptr<spine::SkeletonData> readSkeleton(const ResourceFiles &files, spine::Atlas *atlas) {
    AttachmentLoader attachments(atlas);
    std::unique_ptr<spine::SkeletonData> data;
    if (files.binary) {
        spine::SkeletonBinary reader(&attachments, false);
        data.reset(reader.readSkeletonData(reinterpret_cast<const unsigned char *>(files.skeletonBytes.data()),
                                          static_cast<int>(files.skeletonBytes.size())));
        if (!data) throw std::runtime_error(std::string("Cannot read Spine binary: ") + reader.getError().buffer());
    } else {
        spine::SkeletonJson reader(&attachments, false);
        data.reset(reader.readSkeletonData(files.skeletonBytes.c_str()));
        if (!data) throw std::runtime_error(std::string("Cannot read Spine JSON: ") + reader.getError().buffer());
    }
    if (!attachments.missing.empty()) throw std::runtime_error("Atlas is missing attachment region: " + attachments.missing);
    if (!data->getBones().size()) throw std::runtime_error("Skeleton has no bones");
    return data;
}

void setBlend(const ImDrawList *, const ImDrawCmd *command) {
    const auto mode = static_cast<spine::BlendMode>(reinterpret_cast<intptr_t>(command->UserCallbackData));
    GLenum source = GL_SRC_ALPHA, destination = GL_ONE_MINUS_SRC_ALPHA;
    if (mode == spine::BlendMode_Additive) destination = GL_ONE;
    if (mode == spine::BlendMode_Multiply) source = GL_DST_COLOR;
    if (mode == spine::BlendMode_Screen) { source = GL_ONE; destination = GL_ONE_MINUS_SRC_COLOR; }
    glBlendFuncSeparate(source, destination, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}
}

struct SpinePreview::Impl {
    TextureLoader textures;
    std::unique_ptr<spine::Atlas> atlas;
    std::unique_ptr<spine::SkeletonData> data;
    std::unique_ptr<spine::Skeleton> skeleton;
    std::unique_ptr<spine::AnimationStateData> stateData;
    std::unique_ptr<spine::AnimationState> state;
    spine::SkeletonClipping clipper;
    spine::Vector<float> world;
    spine::Vector<unsigned short> quad;
    std::vector<std::string> animations, skins;
    std::string version;
    float x = 0, y = 0, width = 0, height = 0;

    explicit Impl(const ResourceFiles &files) {
        spine::Bone::setYDown(false);
        textures.pages = files.pages;
        const auto directory = files.atlas.parent_path().string();
        atlas.reset(new spine::Atlas(files.atlasText.data(), static_cast<int>(files.atlasText.size()), directory.c_str(), &textures));
        if (!textures.error.empty()) throw std::runtime_error(textures.error);
        if (!atlas->getPages().size()) throw std::runtime_error("Atlas is empty");
        data = readSkeleton(files, atlas.get());
        version = data->getVersion().buffer();
        for (size_t i = 0; i < data->getAnimations().size(); ++i)
            animations.emplace_back(data->getAnimations()[i]->getName().buffer());
        for (size_t i = 0; i < data->getSkins().size(); ++i)
            skins.emplace_back(data->getSkins()[i]->getName().buffer());
        skeleton.reset(new spine::Skeleton(data.get()));
        if (!data->getDefaultSkin() && !skins.empty()) skeleton->setSkin(data->getSkins()[0]);
        skeleton->setToSetupPose();
        skeleton->updateWorldTransform();
        skeleton->getBounds(x, y, width, height, world);
        if (!std::isfinite(width) || !std::isfinite(height)) throw std::runtime_error("Skeleton has invalid bounds");
        if (width <= 0 || height <= 0) {
            x = data->getX(); y = data->getY();
            width = std::max(100.0f, data->getWidth()); height = std::max(100.0f, data->getHeight());
        }
        stateData.reset(new spine::AnimationStateData(data.get()));
        stateData->setDefaultMix(0.2f);
        state.reset(new spine::AnimationState(stateData.get()));
        quad.add(0); quad.add(1); quad.add(2); quad.add(2); quad.add(3); quad.add(0);
    }
};

SpinePreview::SpinePreview(const ResourceFiles &files) : impl(new Impl(files)) {
    const auto &names = impl->animations;
    const auto found = std::find(names.begin(), names.end(), "walk");
    setAnimation(found == names.end() ? 0 : static_cast<int>(found - names.begin()), true);
}
void SpinePreview::validate(const ResourceFiles &files) {
    const auto directory = files.atlas.parent_path().string();
    spine::Atlas atlas(files.atlasText.data(), static_cast<int>(files.atlasText.size()), directory.c_str(), nullptr, false);
    auto data = readSkeleton(files, &atlas);
    spine::Skeleton skeleton(data.get());
    skeleton.setToSetupPose(); skeleton.updateWorldTransform();
    // Exercise animation construction and evaluation in the isolated worker too.
    spine::AnimationStateData stateData(data.get());
    spine::AnimationState state(&stateData);
    for (size_t i = 0; i < data->getAnimations().size(); ++i) {
        state.setAnimation(0, data->getAnimations()[i], false);
        state.update(0); state.apply(skeleton); skeleton.updateWorldTransform();
    }
}
SpinePreview::~SpinePreview() = default;
const std::vector<std::string> &SpinePreview::animations() const { return impl->animations; }
const std::vector<std::string> &SpinePreview::skins() const { return impl->skins; }
const std::string &SpinePreview::version() const { return impl->version; }
float SpinePreview::time() const { auto *track = impl->state->getCurrent(0); return track ? track->getTrackTime() : 0; }
void SpinePreview::setAnimation(int index, bool loop) {
    if (index < 0 || static_cast<size_t>(index) >= impl->animations.size()) return;
    impl->state->setAnimation(0, impl->data->getAnimations()[static_cast<size_t>(index)], loop);
}
void SpinePreview::setSkin(int index) {
    if (index < 0 || static_cast<size_t>(index) >= impl->skins.size()) return;
    impl->skeleton->setSkin(impl->data->getSkins()[static_cast<size_t>(index)]);
    impl->skeleton->setSlotsToSetupPose();
}
void SpinePreview::update(float delta) {
    impl->skeleton->update(delta);
    impl->state->update(delta);
    impl->state->apply(*impl->skeleton);
    impl->skeleton->updateWorldTransform();
}

void SpinePreview::draw(ImDrawList *list, ImVec2 minimum, ImVec2 maximum, float userScale) {
    auto &s = *impl;
    const float zoom = 0.8f * userScale * std::min((maximum.x - minimum.x) / s.width,
                                                 (maximum.y - minimum.y) / s.height);
    const float centerX = (minimum.x + maximum.x) * 0.5f, centerY = (minimum.y + maximum.y) * 0.5f;
    list->PushClipRect(minimum, maximum, true);
    s.clipper.clipEnd();
    for (size_t slotIndex = 0; slotIndex < s.skeleton->getDrawOrder().size(); ++slotIndex) {
        spine::Slot &slot = *s.skeleton->getDrawOrder()[slotIndex];
        spine::Attachment *attachment = slot.getAttachment();
        if (!attachment || !slot.getBone().isActive()) { s.clipper.clipEnd(slot); continue; }
        spine::Vector<float> *uvs = nullptr, *positions = &s.world;
        spine::Vector<unsigned short> *indices = nullptr;
        spine::Color *color = nullptr;
        spine::AtlasRegion *region = nullptr;
        if (attachment->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
            auto *item = static_cast<spine::RegionAttachment *>(attachment);
            s.world.setSize(8, 0);
            item->computeWorldVertices(slot.getBone(), s.world, 0, 2);
            uvs = &item->getUVs(); indices = &s.quad; color = &item->getColor();
            region = static_cast<spine::AtlasRegion *>(item->getRendererObject());
        } else if (attachment->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
            auto *item = static_cast<spine::MeshAttachment *>(attachment);
            s.world.setSize(item->getWorldVerticesLength(), 0);
            item->computeWorldVertices(slot, 0, item->getWorldVerticesLength(), s.world, 0, 2);
            uvs = &item->getUVs(); indices = &item->getTriangles(); color = &item->getColor();
            region = static_cast<spine::AtlasRegion *>(item->getRendererObject());
        } else if (attachment->getRTTI().isExactly(spine::ClippingAttachment::rtti)) {
            s.clipper.clipStart(slot, static_cast<spine::ClippingAttachment *>(attachment));
            continue;
        } else { s.clipper.clipEnd(slot); continue; }
        if (s.clipper.isClipping()) {
            s.clipper.clipTriangles(s.world, *indices, *uvs, 2);
            positions = &s.clipper.getClippedVertices();
            indices = &s.clipper.getClippedTriangles();
            uvs = &s.clipper.getClippedUVs();
        }
        const auto &a = s.skeleton->getColor();
        const auto &b = slot.getColor();
        const ImU32 tint = ImGui::ColorConvertFloat4ToU32(ImVec4(a.r*b.r*color->r, a.g*b.g*color->g,
                                                              a.b*b.b*color->b, a.a*b.a*color->a));
        if (region && indices->size()) {
            list->AddCallback(setBlend, reinterpret_cast<void *>(static_cast<intptr_t>(slot.getData().getBlendMode())));
            list->PushTexture(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(region->page->getRendererObject()))));
            // Expand indexed triangles: keeps each reservation small on the ES2 backend.
            list->PrimReserve(static_cast<int>(indices->size()), static_cast<int>(indices->size()));
            for (size_t i = 0; i < indices->size(); ++i) {
                const size_t v = (*indices)[i] * 2;
                list->PrimWriteIdx(static_cast<ImDrawIdx>(list->_VtxCurrentIdx));
                list->PrimWriteVtx(ImVec2(centerX + ((*positions)[v] - s.x - s.width*0.5f)*zoom,
                                        centerY - ((*positions)[v+1] - s.y - s.height*0.5f)*zoom),
                                   ImVec2((*uvs)[v], (*uvs)[v+1]), tint);
            }
            list->PopTexture();
        }
        s.clipper.clipEnd(slot);
    }
    s.clipper.clipEnd();
    list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    list->PopClipRect();
}
