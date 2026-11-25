#include "Image.hpp"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#include "../../v-archive/third_party/miniz/miniz.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <thread>

namespace vscript {

namespace {

uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); }

void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v >> 24));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

bool unfilter(uint8_t* rows, size_t stride, int height, int bpp, std::string& error) {
    std::vector<uint8_t> zero(stride, 0);
    for (int y = 0; y < height; y++) {
        uint8_t* line = rows + size_t(y) * (stride + 1);
        uint8_t filter = line[0];
        uint8_t* cur = line + 1;
        const uint8_t* prev = y > 0 ? rows + size_t(y - 1) * (stride + 1) + 1 : zero.data();
        for (size_t x = 0; x < stride; x++) {
            int a = x >= size_t(bpp) ? cur[x - bpp] : 0;
            int b = prev[x];
            int c = x >= size_t(bpp) ? prev[x - bpp] : 0;
            int v = cur[x];
            switch (filter) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: v += paeth(a, b, c); break;
                default: error = "unknown PNG row filter"; return false;
            }
            cur[x] = uint8_t(v);
        }
    }
    return true;
}

struct Format {
    int depth = 8;
    int type = 6;
    int channels = 4;
    std::vector<uint8_t> palette;
    std::vector<uint8_t> trns;
};

int sample(const uint8_t* row, int index, int depth) {
    if (depth == 8) return row[index];
    if (depth == 16) return (row[index * 2] << 8) | row[index * 2 + 1];
    int perByte = 8 / depth;
    int byte = row[index / perByte];
    int shift = 8 - depth * (index % perByte + 1);
    return (byte >> shift) & ((1 << depth) - 1);
}

uint8_t to8(int value, int depth) {
    if (depth == 8) return uint8_t(value);
    if (depth == 16) return uint8_t(value >> 8);
    return uint8_t(value * 255 / ((1 << depth) - 1));
}

void writePixel(const Format& f, const uint8_t* row, int x, uint8_t* px) {
    int c = f.channels;
    auto s = [&](int i) { return sample(row, x * c + i, f.depth); };
    switch (f.type) {
        case 0: {
            int g = s(0);
            px[0] = px[1] = px[2] = to8(g, f.depth);
            int key = f.trns.size() >= 2 ? (f.trns[0] << 8) | f.trns[1] : -1;
            px[3] = g == key ? 0 : 255;
            break;
        }
        case 2: {
            int r = s(0), g = s(1), b = s(2);
            px[0] = to8(r, f.depth);
            px[1] = to8(g, f.depth);
            px[2] = to8(b, f.depth);
            bool key = f.trns.size() >= 6 && r == ((f.trns[0] << 8) | f.trns[1]) && g == ((f.trns[2] << 8) | f.trns[3]) && b == ((f.trns[4] << 8) | f.trns[5]);
            px[3] = key ? 0 : 255;
            break;
        }
        case 3: {
            int i = s(0);
            if (size_t(i) * 3 + 2 < f.palette.size()) {
                px[0] = f.palette[i * 3];
                px[1] = f.palette[i * 3 + 1];
                px[2] = f.palette[i * 3 + 2];
            } else {
                px[0] = px[1] = px[2] = 0;
            }
            px[3] = size_t(i) < f.trns.size() ? f.trns[i] : 255;
            break;
        }
        case 4: {
            px[0] = px[1] = px[2] = to8(s(0), f.depth);
            px[3] = to8(s(1), f.depth);
            break;
        }
        default: {
            px[0] = to8(s(0), f.depth);
            px[1] = to8(s(1), f.depth);
            px[2] = to8(s(2), f.depth);
            px[3] = to8(s(3), f.depth);
        }
    }
}

}

bool decodePng(const uint8_t* data, size_t size, Image& out, std::string& error) {
    static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (size < 33 || std::memcmp(data, signature, 8) != 0) {
        error = "not a PNG file";
        return false;
    }
    Format f;
    uint32_t width = 0, height = 0;
    int interlace = 0;
    std::vector<uint8_t> idat;
    size_t i = 8;
    while (i + 8 <= size) {
        uint32_t length = be32(data + i);
        if (length > size - i - 8) {
            error = "truncated PNG chunk";
            return false;
        }
        const uint8_t* body = data + i + 8;
        std::string kind(reinterpret_cast<const char*>(data + i + 4), 4);
        if (kind == "IHDR" && length >= 13) {
            width = be32(body);
            height = be32(body + 4);
            f.depth = body[8];
            f.type = body[9];
            interlace = body[12];
        } else if (kind == "PLTE") {
            f.palette.assign(body, body + length);
        } else if (kind == "tRNS") {
            f.trns.assign(body, body + length);
        } else if (kind == "IDAT") {
            idat.insert(idat.end(), body, body + length);
        } else if (kind == "IEND") {
            break;
        }
        i += size_t(length) + 12;
    }
    switch (f.type) {
        case 0: f.channels = 1; break;
        case 2: f.channels = 3; break;
        case 3: f.channels = 1; break;
        case 4: f.channels = 2; break;
        case 6: f.channels = 4; break;
        default: error = "unsupported PNG color type"; return false;
    }
    bool depthOk = f.depth == 8 || f.depth == 16 || ((f.type == 0 || f.type == 3) && (f.depth == 1 || f.depth == 2 || f.depth == 4));
    if (!depthOk || width == 0 || height == 0 || width > 32768 || height > 32768 || uint64_t(width) * height > (1ull << 28)) {
        error = "unsupported PNG size or bit depth";
        return false;
    }
    size_t rawSize = 0;
    void* raw = tinfl_decompress_mem_to_heap(idat.data(), idat.size(), &rawSize, TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (!raw) {
        error = "the PNG image data is damaged";
        return false;
    }
    uint8_t* bytes = static_cast<uint8_t*>(raw);
    int bitsPerPixel = f.channels * f.depth;
    int bpp = std::max(1, bitsPerPixel / 8);
    out.width = int(width);
    out.height = int(height);
    out.rgba.assign(size_t(width) * height * 4, 0);
    struct Pass { int x, y, dx, dy; };
    static const Pass adam7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
    static const Pass single[1] = {{0, 0, 1, 1}};
    const Pass* passes = interlace ? adam7 : single;
    int passCount = interlace ? 7 : 1;
    size_t offset = 0;
    for (int p = 0; p < passCount; p++) {
        const Pass& pass = passes[p];
        int pw = (int(width) - pass.x + pass.dx - 1) / pass.dx;
        int ph = (int(height) - pass.y + pass.dy - 1) / pass.dy;
        if (pw <= 0 || ph <= 0) continue;
        size_t stride = (size_t(pw) * bitsPerPixel + 7) / 8;
        size_t need = (stride + 1) * size_t(ph);
        if (offset + need > rawSize) {
            mz_free(raw);
            error = "the PNG image data is shorter than its size";
            return false;
        }
        uint8_t* rows = bytes + offset;
        if (!unfilter(rows, stride, ph, bpp, error)) {
            mz_free(raw);
            return false;
        }
        for (int y = 0; y < ph; y++) {
            const uint8_t* row = rows + size_t(y) * (stride + 1) + 1;
            int oy = pass.y + y * pass.dy;
            for (int x = 0; x < pw; x++) {
                int ox = pass.x + x * pass.dx;
                writePixel(f, row, x, &out.rgba[(size_t(oy) * width + ox) * 4]);
            }
        }
        offset += need;
    }
    mz_free(raw);
    return true;
}

namespace {

void rowPixels(const Image& image, int y, int channels, uint8_t* out) {
    const uint8_t* src = &image.rgba[size_t(y) * image.width * 4];
    if (channels == 4) {
        std::memcpy(out, src, size_t(image.width) * 4);
        return;
    }
    for (int x = 0; x < image.width; x++) {
        out[size_t(x) * 3] = src[x * 4];
        out[size_t(x) * 3 + 1] = src[x * 4 + 1];
        out[size_t(x) * 3 + 2] = src[x * 4 + 2];
    }
}

void filterRows(const Image& image, int channels, int from, int to, uint8_t* out) {
    size_t stride = size_t(image.width) * channels;
    std::vector<uint8_t> line(stride), prev(stride, 0), best(stride), trial(stride);
    if (from > 0) rowPixels(image, from - 1, channels, prev.data());
    const size_t bpp = size_t(channels);
    auto cost = [](uint8_t v) -> uint64_t { return v < 128 ? v : 256 - v; };
    for (int y = from; y < to; y++) {
        rowPixels(image, y, channels, line.data());
        const uint8_t* cur = line.data();
        const uint8_t* up = prev.data();
        uint64_t bestScore = UINT64_MAX;
        uint8_t bestFilter = 0;
        auto consider = [&](uint8_t filter, uint64_t score) {
            if (score < bestScore) {
                bestScore = score;
                bestFilter = filter;
                best.swap(trial);
            }
        };
        uint64_t score = 0;
        for (size_t x = 0; x < stride; x++) { trial[x] = cur[x]; score += cost(cur[x]); }
        consider(0, score);
        score = 0;
        for (size_t x = 0; x < stride; x++) { uint8_t v = uint8_t(cur[x] - (x >= bpp ? cur[x - bpp] : 0)); trial[x] = v; score += cost(v); }
        consider(1, score);
        score = 0;
        for (size_t x = 0; x < stride; x++) { uint8_t v = uint8_t(cur[x] - up[x]); trial[x] = v; score += cost(v); }
        consider(2, score);
        score = 0;
        for (size_t x = 0; x < stride; x++) { uint8_t v = uint8_t(cur[x] - ((x >= bpp ? cur[x - bpp] : 0) + up[x]) / 2); trial[x] = v; score += cost(v); }
        consider(3, score);
        score = 0;
        for (size_t x = 0; x < stride; x++) {
            int pa = x >= bpp ? cur[x - bpp] : 0, pb = up[x], pc = x >= bpp ? up[x - bpp] : 0;
            uint8_t v = uint8_t(cur[x] - paeth(pa, pb, pc));
            trial[x] = v;
            score += cost(v);
        }
        consider(4, score);
        uint8_t* row = out + size_t(y - from) * (stride + 1);
        row[0] = bestFilter;
        std::memcpy(row + 1, best.data(), stride);
        prev.swap(line);
    }
}

struct Sink {
    std::vector<uint8_t>* out;
};

mz_bool sinkPut(const void* data, int length, void* user) {
    auto* sink = static_cast<Sink*>(user);
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    sink->out->insert(sink->out->end(), bytes, bytes + length);
    return MZ_TRUE;
}

void deflatePart(const uint8_t* data, size_t size, int level, bool last, std::vector<uint8_t>& out) {
    tdefl_compressor* compressor = static_cast<tdefl_compressor*>(std::malloc(sizeof(tdefl_compressor)));
    if (!compressor) return;
    Sink sink{&out};
    mz_uint flags = tdefl_create_comp_flags_from_zip_params(level, -15, MZ_DEFAULT_STRATEGY);
    tdefl_init(compressor, sinkPut, &sink, int(flags));
    tdefl_compress_buffer(compressor, data, size, last ? TDEFL_FINISH : TDEFL_SYNC_FLUSH);
    std::free(compressor);
}

uint32_t crcTable[8][256];

void buildCrc() {
    static bool ready = false;
    if (ready) return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[0][n] = c;
    }
    for (uint32_t n = 0; n < 256; n++) for (int t = 1; t < 8; t++) crcTable[t][n] = (crcTable[t - 1][n] >> 8) ^ crcTable[0][crcTable[t - 1][n] & 0xFF];
    ready = true;
}

uint32_t crc32(uint32_t crc, const uint8_t* p, size_t n) {
    crc = ~crc;
    while (n >= 8) {
        uint32_t lo = crc ^ (uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24));
        uint32_t hi = uint32_t(p[4]) | (uint32_t(p[5]) << 8) | (uint32_t(p[6]) << 16) | (uint32_t(p[7]) << 24);
        crc = crcTable[7][lo & 0xFF] ^ crcTable[6][(lo >> 8) & 0xFF] ^ crcTable[5][(lo >> 16) & 0xFF] ^ crcTable[4][lo >> 24] ^
              crcTable[3][hi & 0xFF] ^ crcTable[2][(hi >> 8) & 0xFF] ^ crcTable[1][(hi >> 16) & 0xFF] ^ crcTable[0][hi >> 24];
        p += 8;
        n -= 8;
    }
    while (n--) crc = crcTable[0][(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

}

std::vector<uint8_t> encodePng(const Image& image, int level) {
    static std::once_flag crcOnce;
    std::call_once(crcOnce, buildCrc);
    bool opaque = true;
    for (size_t i = 3; i < image.rgba.size(); i += 4) {
        if (image.rgba[i] != 255) {
            opaque = false;
            break;
        }
    }
    int channels = opaque ? 3 : 4;
    size_t stride = size_t(image.width) * channels;
    size_t total = (stride + 1) * size_t(image.height);
    std::vector<uint8_t> filtered(total);
    int probes = std::clamp(level, 0, 10);
    unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    int parts = int(std::min<size_t>(std::min(8u, hardware), std::max<size_t>(1, total / (256 * 1024))));
    parts = std::max(1, std::min(parts, image.height));
    std::vector<int> bounds(parts + 1);
    for (int i = 0; i <= parts; i++) bounds[i] = int(int64_t(image.height) * i / parts);
    std::vector<std::vector<uint8_t>> packed(parts);
    auto work = [&](int i) {
        uint8_t* start = filtered.data() + size_t(bounds[i]) * (stride + 1);
        filterRows(image, channels, bounds[i], bounds[i + 1], start);
        deflatePart(start, size_t(bounds[i + 1] - bounds[i]) * (stride + 1), probes, i == parts - 1, packed[i]);
    };
    if (parts == 1) {
        work(0);
    } else {
        std::vector<std::thread> threads;
        for (int i = 1; i < parts; i++) threads.emplace_back(work, i);
        work(0);
        for (auto& t : threads) t.join();
    }
    std::vector<uint8_t> zlib = {0x78, 0x9C};
    for (auto& part : packed) zlib.insert(zlib.end(), part.begin(), part.end());
    uint32_t adler = uint32_t(mz_adler32(MZ_ADLER32_INIT, filtered.data(), filtered.size()));
    put32(zlib, adler);
    std::vector<uint8_t> out = {137, 80, 78, 71, 13, 10, 26, 10};
    auto chunk = [&](const char* kind, const uint8_t* body, size_t length) {
        put32(out, uint32_t(length));
        size_t start = out.size();
        out.insert(out.end(), kind, kind + 4);
        if (length) out.insert(out.end(), body, body + length);
        put32(out, crc32(0, out.data() + start, length + 4));
    };
    std::vector<uint8_t> header;
    put32(header, uint32_t(image.width));
    put32(header, uint32_t(image.height));
    header.insert(header.end(), {8, uint8_t(opaque ? 2 : 6), 0, 0, 0});
    chunk("IHDR", header.data(), header.size());
    chunk("IDAT", zlib.data(), zlib.size());
    chunk("IEND", nullptr, 0);
    return out;
}

namespace {

struct Packed {
    int colorType = 6;
    int depth = 8;
    int channels = 4;
    std::vector<uint8_t> palette;
    std::vector<uint8_t> trns;
    std::vector<uint8_t> rows;
    size_t stride = 0;
    bool adaptive = true;
};

void filterPacked(const Packed& p, int from, int to, uint8_t* out) {
    size_t stride = p.stride;
    size_t bpp = std::max<size_t>(1, size_t(p.channels * p.depth / 8));
    std::vector<uint8_t> zero(stride, 0), trial(stride), best(stride);
    auto cost = [](uint8_t v) -> uint64_t { return v < 128 ? v : 256 - v; };
    for (int y = from; y < to; y++) {
        const uint8_t* cur = p.rows.data() + size_t(y) * stride;
        const uint8_t* up = y > 0 ? p.rows.data() + size_t(y - 1) * stride : zero.data();
        uint8_t* row = out + size_t(y - from) * (stride + 1);
        if (!p.adaptive) {
            row[0] = 0;
            std::memcpy(row + 1, cur, stride);
            continue;
        }
        uint64_t bestScore = UINT64_MAX;
        uint8_t bestFilter = 0;
        for (uint8_t filter = 0; filter < 5; filter++) {
            uint64_t score = 0;
            for (size_t x = 0; x < stride; x++) {
                int a = x >= bpp ? cur[x - bpp] : 0, b = up[x], c = x >= bpp ? up[x - bpp] : 0;
                int predicted = filter == 0 ? 0 : filter == 1 ? a : filter == 2 ? b : filter == 3 ? (a + b) / 2 : paeth(a, b, c);
                uint8_t v = uint8_t(cur[x] - predicted);
                trial[x] = v;
                score += cost(v);
                if (score >= bestScore) break;
            }
            if (score < bestScore) {
                bestScore = score;
                bestFilter = filter;
                best.swap(trial);
            }
        }
        row[0] = bestFilter;
        std::memcpy(row + 1, best.data(), stride);
    }
}

std::vector<uint8_t> writePacked(const Packed& p, int width, int height, int level) {
    static std::once_flag crcOnce;
    std::call_once(crcOnce, buildCrc);
    size_t line = p.stride + 1;
    std::vector<uint8_t> filtered(line * size_t(height));
    unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    int parts = int(std::min<size_t>(std::min(8u, hardware), std::max<size_t>(1, filtered.size() / (4u * 1024 * 1024))));
    parts = std::max(1, std::min(parts, height));
    std::vector<int> bounds(parts + 1);
    for (int i = 0; i <= parts; i++) bounds[i] = int(int64_t(height) * i / parts);
    std::vector<std::vector<uint8_t>> packed(parts);
    int probes = std::clamp(level, 1, 10);
    auto work = [&](int i) {
        uint8_t* start = filtered.data() + size_t(bounds[i]) * line;
        filterPacked(p, bounds[i], bounds[i + 1], start);
        deflatePart(start, size_t(bounds[i + 1] - bounds[i]) * line, probes, i == parts - 1, packed[i]);
    };
    if (parts == 1) {
        work(0);
    } else {
        std::vector<std::thread> threads;
        for (int i = 1; i < parts; i++) threads.emplace_back(work, i);
        work(0);
        for (auto& t : threads) t.join();
    }
    std::vector<uint8_t> zlib = {0x78, 0xDA};
    for (auto& part : packed) zlib.insert(zlib.end(), part.begin(), part.end());
    put32(zlib, uint32_t(mz_adler32(MZ_ADLER32_INIT, filtered.data(), filtered.size())));
    std::vector<uint8_t> out = {137, 80, 78, 71, 13, 10, 26, 10};
    auto chunk = [&](const char* kind, const uint8_t* body, size_t length) {
        put32(out, uint32_t(length));
        size_t start = out.size();
        out.insert(out.end(), kind, kind + 4);
        if (length) out.insert(out.end(), body, body + length);
        put32(out, crc32(0, out.data() + start, length + 4));
    };
    std::vector<uint8_t> header;
    put32(header, uint32_t(width));
    put32(header, uint32_t(height));
    header.insert(header.end(), {uint8_t(p.depth), uint8_t(p.colorType), 0, 0, 0});
    chunk("IHDR", header.data(), header.size());
    if (!p.palette.empty()) chunk("PLTE", p.palette.data(), p.palette.size());
    if (!p.trns.empty()) chunk("tRNS", p.trns.data(), p.trns.size());
    chunk("IDAT", zlib.data(), zlib.size());
    chunk("IEND", nullptr, 0);
    return out;
}

bool packPalette(const Image& image, Packed& p) {
    std::unordered_map<uint32_t, int> seen;
    std::vector<uint32_t> colors;
    const uint8_t* px = image.rgba.data();
    size_t count = size_t(image.width) * image.height;
    uint32_t last = 0;
    bool hasLast = false;
    for (size_t i = 0; i < count; i++) {
        uint32_t c = (uint32_t(px[i * 4]) << 24) | (uint32_t(px[i * 4 + 1]) << 16) | (uint32_t(px[i * 4 + 2]) << 8) | px[i * 4 + 3];
        if (hasLast && c == last) continue;
        last = c;
        hasLast = true;
        if (seen.emplace(c, 0).second) {
            colors.push_back(c);
            if (colors.size() > 256) return false;
        }
    }
    std::stable_sort(colors.begin(), colors.end(), [](uint32_t a, uint32_t b) { return (a & 0xFF) < (b & 0xFF); });
    size_t translucent = 0;
    for (size_t i = 0; i < colors.size(); i++) {
        seen[colors[i]] = int(i);
        if ((colors[i] & 0xFF) != 0xFF) translucent = i + 1;
    }
    p.colorType = 3;
    p.channels = 1;
    p.depth = colors.size() <= 2 ? 1 : colors.size() <= 4 ? 2 : colors.size() <= 16 ? 4 : 8;
    p.adaptive = p.depth == 8 && image.width * image.height > 65536;
    for (uint32_t c : colors) {
        p.palette.push_back(uint8_t(c >> 24));
        p.palette.push_back(uint8_t(c >> 16));
        p.palette.push_back(uint8_t(c >> 8));
    }
    for (size_t i = 0; i < translucent; i++) p.trns.push_back(uint8_t(colors[i] & 0xFF));
    p.stride = (size_t(image.width) * p.depth + 7) / 8;
    p.rows.assign(p.stride * image.height, 0);
    int perByte = 8 / p.depth;
    for (int y = 0; y < image.height; y++) {
        uint8_t* row = p.rows.data() + size_t(y) * p.stride;
        for (int x = 0; x < image.width; x++) {
            const uint8_t* q = px + (size_t(y) * image.width + x) * 4;
            uint32_t c = (uint32_t(q[0]) << 24) | (uint32_t(q[1]) << 16) | (uint32_t(q[2]) << 8) | q[3];
            int index = seen[c];
            if (p.depth == 8) {
                row[x] = uint8_t(index);
            } else {
                int shift = 8 - p.depth * (x % perByte + 1);
                row[x / perByte] |= uint8_t(index << shift);
            }
        }
    }
    return true;
}

void packDirect(const Image& image, Packed& p) {
    const uint8_t* px = image.rgba.data();
    size_t count = size_t(image.width) * image.height;
    bool opaque = true, gray = true;
    for (size_t i = 0; i < count && (opaque || gray); i++) {
        const uint8_t* q = px + i * 4;
        if (q[3] != 255) opaque = false;
        if (q[0] != q[1] || q[1] != q[2]) gray = false;
    }
    p.channels = (gray ? 1 : 3) + (opaque ? 0 : 1);
    p.colorType = gray ? (opaque ? 0 : 4) : (opaque ? 2 : 6);
    p.depth = 8;
    p.stride = size_t(image.width) * p.channels;
    p.rows.resize(p.stride * image.height);
    uint8_t* o = p.rows.data();
    for (size_t i = 0; i < count; i++) {
        const uint8_t* q = px + i * 4;
        if (gray) {
            *o++ = q[0];
        } else {
            *o++ = q[0];
            *o++ = q[1];
            *o++ = q[2];
        }
        if (!opaque) *o++ = q[3];
    }
}

bool safeToRewrite(const uint8_t* data, size_t size) {
    static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (size < 33 || std::memcmp(data, signature, 8) != 0) return false;
    size_t i = 8;
    while (i + 12 <= size) {
        uint32_t length = be32(data + i);
        if (length > size - i - 12) return false;
        const uint8_t* kind = data + i + 4;
        if (std::memcmp(kind, "IHDR", 4) == 0 && length >= 13 && data[i + 8 + 8] > 8) return false;
        if (std::memcmp(kind, "acTL", 4) == 0 || std::memcmp(kind, "fcTL", 4) == 0) return false;
        if (!(kind[0] & 0x20) && std::memcmp(kind, "IHDR", 4) != 0 && std::memcmp(kind, "PLTE", 4) != 0 &&
            std::memcmp(kind, "IDAT", 4) != 0 && std::memcmp(kind, "IEND", 4) != 0) return false;
        if (std::memcmp(kind, "IEND", 4) == 0) return true;
        i += 12 + length;
    }
    return false;
}

}

bool optimizePng(const uint8_t* data, size_t size, int level, std::vector<uint8_t>& out) {
    if (!safeToRewrite(data, size)) return false;
    Image image;
    std::string error;
    if (!decodePng(data, size, image, error) || image.width <= 0 || image.height <= 0) return false;
    std::vector<uint8_t> best;
    Packed palette;
    bool hasPalette = packPalette(image, palette);
    if (hasPalette) best = writePacked(palette, image.width, image.height, level);
    if (!hasPalette || size_t(image.width) * image.height <= 4u * 1024 * 1024) {
        Packed direct;
        packDirect(image, direct);
        std::vector<uint8_t> candidate = writePacked(direct, image.width, image.height, level);
        if (best.empty() || candidate.size() < best.size()) best.swap(candidate);
    }
    if (best.empty() || best.size() >= size) return false;
    Image check;
    if (!decodePng(best.data(), best.size(), check, error) || check.width != image.width || check.height != image.height || check.rgba != image.rgba) return false;
    out.swap(best);
    return true;
}

Image resizeImage(const Image& image, int width, int height, bool smooth) {
    Image out;
    out.width = std::max(1, width);
    out.height = std::max(1, height);
    out.rgba.assign(size_t(out.width) * out.height * 4, 0);
    if (image.width <= 0 || image.height <= 0) return out;
    if (out.width == image.width && out.height == image.height) return image;
    double sx = double(image.width) / out.width;
    double sy = double(image.height) / out.height;
    const uint8_t* src = image.rgba.data();
    if (!smooth) {
        for (int y = 0; y < out.height; y++) {
            int py = std::min(image.height - 1, int(std::floor((y + 0.5) * sy)));
            for (int x = 0; x < out.width; x++) {
                int px = std::min(image.width - 1, int(std::floor((x + 0.5) * sx)));
                std::memcpy(&out.rgba[(size_t(y) * out.width + x) * 4], src + (size_t(py) * image.width + px) * 4, 4);
            }
        }
        return out;
    }
    auto add = [&](int px, int py, double weight, double* acc) {
        const uint8_t* p = src + (size_t(py) * image.width + px) * 4;
        double a = p[3] / 255.0 * weight;
        acc[0] += p[0] / 255.0 * a;
        acc[1] += p[1] / 255.0 * a;
        acc[2] += p[2] / 255.0 * a;
        acc[3] += a;
    };
    for (int y = 0; y < out.height; y++) {
        double y0 = y * sy, y1 = std::min(double(image.height), (y + 1) * sy);
        for (int x = 0; x < out.width; x++) {
            double x0 = x * sx, x1 = std::min(double(image.width), (x + 1) * sx);
            double acc[4] = {0, 0, 0, 0};
            double area = 0;
            if (sx >= 1.0 && sy >= 1.0) {
                for (int py = int(std::floor(y0)); py < int(std::ceil(y1)); py++) {
                    double wy = std::min(y1, py + 1.0) - std::max(y0, double(py));
                    for (int px = int(std::floor(x0)); px < int(std::ceil(x1)); px++) {
                        double wx = std::min(x1, px + 1.0) - std::max(x0, double(px));
                        double w = wx * wy;
                        if (w > 0) {
                            add(px, py, w, acc);
                            area += w;
                        }
                    }
                }
            } else {
                double fx = (x + 0.5) * sx - 0.5, fy = (y + 0.5) * sy - 0.5;
                int ix = int(std::floor(fx)), iy = int(std::floor(fy));
                double tx = fx - ix, ty = fy - iy;
                const double weights[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
                const int offs[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
                for (int k = 0; k < 4; k++) {
                    if (weights[k] <= 0) continue;
                    add(std::clamp(ix + offs[k][0], 0, image.width - 1), std::clamp(iy + offs[k][1], 0, image.height - 1), weights[k], acc);
                    area += weights[k];
                }
            }
            if (area <= 0 || acc[3] <= 0) continue;
            uint8_t* o = &out.rgba[(size_t(y) * out.width + x) * 4];
            for (int c = 0; c < 3; c++) o[c] = uint8_t(std::clamp(int(std::lround(acc[c] / acc[3] * 255.0)), 0, 255));
            o[3] = uint8_t(std::clamp(int(std::lround(acc[3] / area * 255.0)), 0, 255));
        }
    }
    return out;
}

void bakeColors(Image& image, uint32_t r, uint32_t g, uint32_t b) {
    auto split = [](uint32_t c, double* out) {
        out[0] = ((c >> 16) & 0xFF) / 255.0;
        out[1] = ((c >> 8) & 0xFF) / 255.0;
        out[2] = (c & 0xFF) / 255.0;
    };
    double cr[3], cg[3], cb[3];
    split(r, cr);
    split(g, cg);
    split(b, cb);
    for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        uint8_t* p = &image.rgba[i];
        if (p[3] == 0) continue;
        double vr = p[0] / 255.0, vg = p[1] / 255.0, vb = p[2] / 255.0;
        for (int c = 0; c < 3; c++) p[c] = uint8_t(std::lround(std::min(1.0, vr * cr[c] + vg * cg[c] + vb * cb[c]) * 255.0));
    }
}

}
