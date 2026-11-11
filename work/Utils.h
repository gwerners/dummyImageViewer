#ifndef UTILS_H
#define UTILS_H

#include "raylib.h"

Image LoadAVIF(const char* filename);
Image LoadWEBP(const char* filename);
Image LoadJXL(const char* filename);  // JPEG XL (libjxl)
#endif                                // UTILS_H
