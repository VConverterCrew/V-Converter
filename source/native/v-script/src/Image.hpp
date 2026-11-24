#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vscript {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

bool decodePng(const uint8_t* data, size_t size, Image& out, std::string& error);

std::vector<uint8_t> encodePng(const Image& image, int level);

Image resizeImage(const Image& image, int width, int height, bool smooth);

void bakeColors(Image& image, uint32_t r, uint32_t g, uint32_t b);

}
