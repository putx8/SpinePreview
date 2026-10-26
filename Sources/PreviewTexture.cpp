#include "PreviewTexture.h"
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <algorithm>
#include <vector>
GLuint loadPreviewTexture(const std::string &filename, bool premultiplied,
                          int minFilter, int magFilter, int wrapS, int wrapT, std::string &error) {
    // ImageIO decodes PMA RGBA. Recover straight alpha for the shared ImGui shader.
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr,
        reinterpret_cast<const UInt8 *>(filename.data()), filename.size(), false);
    CGImageSourceRef source = url ? CGImageSourceCreateWithURL(url, nullptr) : nullptr;
    if (url) CFRelease(url);
    CGImageRef image = source ? CGImageSourceCreateImageAtIndex(source, 0, nullptr) : nullptr;
    if (source) CFRelease(source);
    if (!image) { error = std::string("Cannot decode atlas image: ") + filename; return 0; }
    const size_t width = CGImageGetWidth(image), height = CGImageGetHeight(image);
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (!width || !height || width > static_cast<size_t>(limit) || height > static_cast<size_t>(limit)) {
        CGImageRelease(image);
        error = "Atlas texture dimensions exceed the GPU limit";
        return 0;
    }
    std::vector<unsigned char> pixels(width * height * 4);
    CGColorSpaceRef colors = CGColorSpaceCreateDeviceRGB();
    CGContextRef bitmap = CGBitmapContextCreate(pixels.data(), width, height, 8, width * 4, colors,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colors);
    if (!bitmap) { CGImageRelease(image); error = "Cannot allocate atlas bitmap"; return 0; }
    CGContextDrawImage(bitmap, CGRectMake(0, 0, width, height), image);
    CGContextRelease(bitmap);
    CGImageRelease(image);
    const int passes = premultiplied ? 2 : 1;
    for (int pass = 0; pass < passes; ++pass) for (size_t i = 0; i < pixels.size(); i += 4) {
        const unsigned alpha = pixels[i + 3];
        if (alpha) for (size_t c = 0; c < 3; ++c)
            pixels[i + c] = static_cast<unsigned char>(std::min(255u, (pixels[i + c] * 255u + alpha / 2) / alpha));
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    const bool mipmap = minFilter >= 3;
    const GLenum minFilters[] = {GL_LINEAR, GL_NEAREST, GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR,
        GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_LINEAR};
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilters[minFilter]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter == 1 ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS == 2 ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT == 2 ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    if (mipmap) glGenerateMipmap(GL_TEXTURE_2D);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        error = "Cannot upload atlas texture to ANGLE";
        return 0;
    }
    return texture;
}
