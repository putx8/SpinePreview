#pragma once
#include <GLES2/gl2.h>
#include <string>
// Filter codes match Spine 3.8: nearest=1, linear=2, mipmap variants=3..7.
GLuint loadPreviewTexture(const std::string &filename, bool premultiplied,
                          int minFilter, int magFilter, int wrapS, int wrapT, std::string &error);
