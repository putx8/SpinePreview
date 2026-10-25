#pragma once
#include "imconfig.h"
// Detailed skeletons can exceed 64K vertices in one frame. ANGLE Metal supports
// 32-bit element indices, so don't constrain asset complexity to ImGui's default.
#define ImDrawIdx unsigned int
