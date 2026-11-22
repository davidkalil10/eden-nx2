// SPDX-FileCopyrightText: Copyright 2026 Eden-NX2 Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef __SWITCH2__

#include "yuzu_cmd/switch2_launcher.h"
#include "yuzu_cmd/switch2_runtime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "common/stb.h"
#ifdef HAS_LSFG
#include "video_core/frame_gen/lossless_dll.h"
#endif

namespace {

constexpr int kScreenW = 1280;
constexpr int kScreenH = 720;

// Little-endian RGBA_8888 pixel packing: byte 0=R, 1=G, 2=B, 3=A.
constexpr std::uint32_t PackRGBA(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                 std::uint8_t a = 255) {
    return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(r);
}

constexpr std::uint32_t BlendColor(std::uint32_t dst, std::uint32_t src, std::uint8_t alpha) {
    const std::uint32_t inv = 255u - alpha;
    const std::uint32_t sr = src & 0xFFu;
    const std::uint32_t sg = (src >> 8) & 0xFFu;
    const std::uint32_t sb = (src >> 16) & 0xFFu;
    const std::uint32_t dr = dst & 0xFFu;
    const std::uint32_t dg = (dst >> 8) & 0xFFu;
    const std::uint32_t db = (dst >> 16) & 0xFFu;
    const std::uint32_t r = (sr * alpha + dr * inv) / 255u;
    const std::uint32_t g = (sg * alpha + dg * inv) / 255u;
    const std::uint32_t b = (sb * alpha + db * inv) / 255u;
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

constexpr const char* T(bool en, const char* pt, const char* en_str) {
    return en ? en_str : pt;
}

// Crisp 8x8 bitmap font (ASCII 0x20..0x7E, MSB-left per row, 8 bytes/glyph).
constexpr std::array<std::array<std::uint8_t, 8>, 95> kFont8x8 = {{
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // ' ' 32
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // '!' 33
    {0x6C, 0x6C, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00}, // '"' 34
    {0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00}, // '#' 35
    {0x18, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x18, 0x00}, // '$' 36
    {0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00}, // '%' 37
    {0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00}, // '&' 38
    {0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00}, // '\'' 39
    {0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00}, // '(' 40
    {0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00}, // ')' 41
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // '*' 42
    {0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00}, // '+' 43
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30}, // ',' 44
    {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00}, // '-' 45
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00}, // '.' 46
    {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00}, // '/' 47
    {0x7C, 0xC6, 0xCE, 0xDE, 0xF6, 0xE6, 0x7C, 0x00}, // '0' 48
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}, // '1' 49
    {0x7C, 0xC6, 0x06, 0x1C, 0x30, 0x60, 0xFE, 0x00}, // '2' 50
    {0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C, 0x00}, // '3' 51
    {0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x0C, 0x00}, // '4' 52
    {0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00}, // '5' 53
    {0x3C, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00}, // '6' 54
    {0xFE, 0xC6, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00}, // '7' 55
    {0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00}, // '8' 56
    {0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00}, // '9' 57
    {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00}, // ':' 58
    {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x30, 0x00}, // ';' 59
    {0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00}, // '<' 60
    {0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00}, // '=' 61
    {0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0x00}, // '>' 62
    {0x7C, 0xC6, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00}, // '?' 63
    {0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x7C, 0x00}, // '@' 64
    {0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0x00}, // 'A' 65
    {0xFC, 0xC6, 0xC6, 0xFC, 0xC6, 0xC6, 0xFC, 0x00}, // 'B' 66
    {0x7C, 0xC6, 0xC0, 0xC0, 0xC0, 0xC6, 0x7C, 0x00}, // 'C' 67
    {0xF8, 0xCC, 0xC6, 0xC6, 0xC6, 0xCC, 0xF8, 0x00}, // 'D' 68
    {0xFE, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, 0xFE, 0x00}, // 'E' 69
    {0xFE, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, 0xC0, 0x00}, // 'F' 70
    {0x7C, 0xC6, 0xC0, 0xCE, 0xC6, 0xC6, 0x7E, 0x00}, // 'G' 71
    {0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00}, // 'H' 72
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}, // 'I' 73
    {0x3E, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00}, // 'J' 74
    {0xC6, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0xC6, 0x00}, // 'K' 75
    {0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFE, 0x00}, // 'L' 76
    {0xC6, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6, 0xC6, 0x00}, // 'M' 77
    {0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00}, // 'N' 78
    {0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00}, // 'O' 79
    {0xFC, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0, 0xC0, 0x00}, // 'P' 80
    {0x7C, 0xC6, 0xC6, 0xC6, 0xD6, 0xCC, 0x76, 0x00}, // 'Q' 81
    {0xFC, 0xC6, 0xC6, 0xFC, 0xD8, 0xCC, 0xC6, 0x00}, // 'R' 82
    {0x7C, 0xC6, 0xC0, 0x7C, 0x06, 0xC6, 0x7C, 0x00}, // 'S' 83
    {0xFE, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00}, // 'T' 84
    {0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00}, // 'U' 85
    {0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x18, 0x00}, // 'V' 86
    {0xC6, 0xC6, 0xD6, 0xFE, 0xFE, 0xEE, 0xC6, 0x00}, // 'W' 87
    {0xC6, 0x6C, 0x38, 0x38, 0x6C, 0xC6, 0xC6, 0x00}, // 'X' 88
    {0xC6, 0xC6, 0x6C, 0x38, 0x18, 0x18, 0x18, 0x00}, // 'Y' 89
    {0xFE, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xFE, 0x00}, // 'Z' 90
    {0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00}, // '[' 91
    {0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00}, // '\' 92
    {0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00}, // ']' 93
    {0x18, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00}, // '^' 94
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // '_' 95
    {0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00}, // '`' 96
    {0x00, 0x00, 0x7C, 0x06, 0x7E, 0xC6, 0x7E, 0x00}, // 'a' 97
    {0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xFC, 0x00}, // 'b' 98
    {0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC6, 0x7C, 0x00}, // 'c' 99
    {0x06, 0x06, 0x7E, 0xC6, 0xC6, 0xC6, 0x7E, 0x00}, // 'd' 100
    {0x00, 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0x7C, 0x00}, // 'e' 101
    {0x3C, 0x60, 0x60, 0xF8, 0x60, 0x60, 0x60, 0x00}, // 'f' 102
    {0x00, 0x00, 0x7E, 0xC6, 0xC6, 0x7E, 0x06, 0x7C}, // 'g' 103
    {0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0x00}, // 'h' 104
    {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00}, // 'i' 105
    {0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78}, // 'j' 106
    {0xC0, 0xC0, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0x00}, // 'k' 107
    {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, // 'l' 108
    {0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xC6, 0xC6, 0x00}, // 'm' 109
    {0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0x00}, // 'n' 110
    {0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0x00}, // 'o' 111
    {0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0}, // 'p' 112
    {0x00, 0x00, 0x7E, 0xC6, 0xC6, 0x7E, 0x06, 0x06}, // 'q' 113
    {0x00, 0x00, 0xDC, 0xE6, 0xC0, 0xC0, 0xC0, 0x00}, // 'r' 114
    {0x00, 0x00, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x00}, // 's' 115
    {0x30, 0x30, 0xFC, 0x30, 0x30, 0x36, 0x1C, 0x00}, // 't' 116
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E, 0x00}, // 'u' 117
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00}, // 'v' 118
    {0x00, 0x00, 0xC6, 0xC6, 0xD6, 0xFE, 0x6C, 0x00}, // 'w' 119
    {0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0x00}, // 'x' 120
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x7C}, // 'y' 121
    {0x00, 0x00, 0xFE, 0x0C, 0x38, 0x60, 0xFE, 0x00}, // 'z' 122
    {0x1C, 0x30, 0x30, 0xE0, 0x30, 0x30, 0x1C, 0x00}, // '{' 123
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // '|' 124
    {0xE0, 0x30, 0x30, 0x1C, 0x30, 0x30, 0xE0, 0x00}, // '}' 125
    {0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // '~' 126
}};

class Canvas {
public:
    Canvas() : buf(kScreenW * kScreenH, PackRGBA(11, 16, 24)) {}

    void FillGradientV(int x, int y, int w, int h, std::uint32_t top, std::uint32_t bottom) {
        const int x0 = std::max(0, x);
        const int y0 = std::max(0, y);
        const int x1 = std::min(kScreenW, x + w);
        const int y1 = std::min(kScreenH, y + h);
        if (x0 >= x1 || y0 >= y1) return;
        const std::uint8_t r0 = top & 0xFFu, g0 = (top >> 8) & 0xFFu, b0 = (top >> 16) & 0xFFu;
        const std::uint8_t r1 = bottom & 0xFFu, g1 = (bottom >> 8) & 0xFFu, b1 = (bottom >> 16) & 0xFFu;
        const int span = std::max(1, h - 1);
        for (int py = y0; py < y1; ++py) {
            const int t = py - y;
            const std::uint8_t r = static_cast<std::uint8_t>(r0 + (int(r1) - int(r0)) * t / span);
            const std::uint8_t g = static_cast<std::uint8_t>(g0 + (int(g1) - int(g0)) * t / span);
            const std::uint8_t b = static_cast<std::uint8_t>(b0 + (int(b1) - int(b0)) * t / span);
            const std::uint32_t col = PackRGBA(r, g, b);
            std::fill(buf.begin() + py * kScreenW + x0, buf.begin() + py * kScreenW + x1, col);
        }
    }

    void FillRect(int x, int y, int w, int h, std::uint32_t col, std::uint8_t alpha = 255) {
        const int x0 = std::max(0, x);
        const int y0 = std::max(0, y);
        const int x1 = std::min(kScreenW, x + w);
        const int y1 = std::min(kScreenH, y + h);
        if (x0 >= x1 || y0 >= y1) return;
        if (alpha == 255) {
            for (int py = y0; py < y1; ++py) {
                std::fill(buf.begin() + py * kScreenW + x0, buf.begin() + py * kScreenW + x1, col);
            }
        } else {
            for (int py = y0; py < y1; ++py) {
                std::uint32_t* row = buf.data() + py * kScreenW;
                for (int px = x0; px < x1; ++px) {
                    row[px] = BlendColor(row[px], col, alpha);
                }
            }
        }
    }

    void DrawRoundedRect(int x, int y, int w, int h, int radius, std::uint32_t fill,
                         std::uint32_t border = 0, std::uint8_t fill_alpha = 255) {
        if (w <= 0 || h <= 0) return;
        const int r = std::min({radius, w / 2, h / 2});
        const int x0 = std::max(0, x);
        const int y0 = std::max(0, y);
        const int x1 = std::min(kScreenW, x + w);
        const int y1 = std::min(kScreenH, y + h);
        const int r_sq = r * r;
        const int inner_r_sq = std::max(0, (r - 2) * (r - 2));

        for (int py = y0; py < y1; ++py) {
            const int dy_top = y + r - 1 - py;
            const int dy_bot = py - (y + h - r);
            const int cy = (dy_top > 0) ? dy_top : (dy_bot > 0 ? dy_bot : 0);
            std::uint32_t* row = buf.data() + py * kScreenW;
            const bool on_h_edge = (py == y || py == y + 1 || py == y + h - 1 || py == y + h - 2);
            for (int px = x0; px < x1; ++px) {
                const int dx_left = x + r - 1 - px;
                const int dx_right = px - (x + w - r);
                const int cx = (dx_left > 0) ? dx_left : (dx_right > 0 ? dx_right : 0);
                const int dist_sq = cx * cx + cy * cy;
                if (cx > 0 && cy > 0 && dist_sq > r_sq) {
                    continue;
                }
                const bool on_v_edge = (px == x || px == x + 1 || px == x + w - 1 || px == x + w - 2);
                const bool is_border =
                    border != 0 && (on_h_edge || on_v_edge ||
                                    (cx > 0 && cy > 0 && dist_sq >= inner_r_sq));
                const std::uint32_t col = is_border ? border : fill;
                const std::uint8_t a = is_border ? 255 : fill_alpha;
                row[px] = (a == 255) ? col : BlendColor(row[px], col, a);
            }
        }
    }

    void DrawText(int x, int y, std::string_view text, std::uint32_t color, int scale = 2,
                  bool shadow = false) {
        if (shadow) {
            DrawText(x + scale, y + scale, text, PackRGBA(6, 9, 14), scale, false);
        }
        const int step = 8 * scale;
        for (std::size_t i = 0; i < text.size(); ++i) {
            unsigned char ch = static_cast<unsigned char>(text[i]);
            if (ch < 32 || ch > 126) ch = '?';
            const auto& glyph = kFont8x8[ch - 32];
            const int gx = x + static_cast<int>(i) * step;
            if (gx >= kScreenW) break;
            if (gx + step <= 0) continue;
            for (int row = 0; row < 8; ++row) {
                const std::uint8_t bits = glyph[row];
                if (!bits) continue;
                for (int col = 0; col < 8; ++col) {
                    if ((bits & (0x80u >> col)) != 0) {
                        FillRect(gx + col * scale, y + row * scale, scale, scale, color);
                    }
                }
            }
        }
    }

    void DrawTextMaxWidth(int x, int y, int max_px, std::string_view text, std::uint32_t color,
                          int scale = 2, bool shadow = false) {
        const std::size_t max_chars = static_cast<std::size_t>(std::max(1, max_px / (8 * scale)));
        if (text.size() <= max_chars) {
            DrawText(x, y, text, color, scale, shadow);
            return;
        }
        std::string clipped(text.substr(0, max_chars > 3 ? max_chars - 3 : max_chars));
        if (max_chars > 3) clipped += "...";
        DrawText(x, y, clipped, color, scale, shadow);
    }

    int DrawPill(int x, int y, std::string_view text, std::uint32_t bg, std::uint32_t fg,
                 int scale = 2) {
        const int w = static_cast<int>(text.size()) * 8 * scale + 16;
        const int h = 8 * scale + 10;
        DrawRoundedRect(x, y, w, h, h / 2, bg);
        DrawText(x + 8, y + 5, text, fg, scale, false);
        return w;
    }

    void DrawIcon56(int x, int y, const std::vector<std::uint32_t>& rgba56) {
        if (rgba56.size() != 56u * 56u) return;
        for (int py = 0; py < 56; ++py) {
            const int sy = y + py;
            if (sy < 0 || sy >= kScreenH) continue;
            for (int px = 0; px < 56; ++px) {
                const int sx = x + px;
                if (sx < 0 || sx >= kScreenW) continue;
                buf[sy * kScreenW + sx] = rgba56[py * 56 + px];
            }
        }
    }

    void Present() {
        std::uint32_t stride_px = 0;
        std::uint32_t* dst = Switch2FbBegin(&stride_px);
        if (!dst) return;
        if (stride_px == static_cast<std::uint32_t>(kScreenW)) {
            std::memcpy(dst, buf.data(), buf.size() * sizeof(std::uint32_t));
        } else {
            for (int row = 0; row < kScreenH; ++row) {
                std::memcpy(dst + row * stride_px, buf.data() + row * kScreenW,
                            kScreenW * sizeof(std::uint32_t));
            }
        }
        Switch2FbEnd();
    }

private:
    std::vector<std::uint32_t> buf;
};

// --- Theme palette ---
constexpr std::uint32_t kBgTop        = PackRGBA(10, 15, 24);
constexpr std::uint32_t kBgBottom     = PackRGBA(17, 25, 39);
constexpr std::uint32_t kCardBg       = PackRGBA(24, 34, 52);
constexpr std::uint32_t kCardSelected = PackRGBA(30, 48, 68);
constexpr std::uint32_t kPanelBg      = PackRGBA(18, 27, 42);
constexpr std::uint32_t kBorderDim    = PackRGBA(45, 62, 88);
constexpr std::uint32_t kAccent       = PackRGBA(16, 185, 129);  // Eden emerald
constexpr std::uint32_t kAccentSoft   = PackRGBA(18, 72, 60);
constexpr std::uint32_t kCyan         = PackRGBA(56, 189, 248);
constexpr std::uint32_t kAmber        = PackRGBA(245, 158, 11);
constexpr std::uint32_t kTextPrimary  = PackRGBA(241, 245, 249);
constexpr std::uint32_t kTextMuted    = PackRGBA(148, 163, 184);

struct GameEntry {
    std::string name;
    std::string display_title;
    std::string author;
    std::string full_path;
    std::string ext;
    std::uint64_t size_bytes = 0;
    bool is_dir = false;
    std::vector<std::uint32_t> icon56;
};

std::string ToLowerExt(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool IsAddonExt(std::string_view ext) {
    return ext == ".nsp" || ext == ".nsz" || ext == ".xci" || ext == ".xcz";
}

bool IsSupportedRomExt(std::string_view ext) {
    return IsAddonExt(ext) || ext == ".nup" || ext == ".nro" || ext == ".nca";
}

template <typename Fn>
void SafeForEachDirEntry(const std::string& dir, Fn&& fn) {
    try {
        std::error_code ec;
        if (dir.empty() || !std::filesystem::is_directory(dir, ec) || ec) return;
        std::filesystem::directory_iterator it(dir, ec);
        const std::filesystem::directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            fn(*it, ec);
        }
    } catch (...) {
    }
}

std::string SanitizeGameId(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    std::string_view base = (slash == std::string_view::npos) ? path : path.substr(slash + 1);
    const std::size_t dot = base.find_last_of('.');
    if (dot != std::string_view::npos) {
        base = base.substr(0, dot);
    }
    std::string out;
    for (unsigned char c : base) {
        if (std::isalnum(c) || c == '_' || c == '-') {
            out.push_back(static_cast<char>(c));
        }
    }
    if (out.empty()) out = "game";
    if (out.size() > 60) out.resize(60);
    return out;
}

std::string GameConfigPath(std::string_view rom_path) {
    return "sdmc:/switch/eden-nx2/games/" + SanitizeGameId(rom_path) + ".ini";
}

constexpr const char* kGlobalSettingsPath = "sdmc:/switch/eden-nx2/settings.ini";
constexpr const char* kLastFolderPath = "sdmc:/switch/eden-nx2/last_folder.txt";
constexpr const char* kDefaultRomsDir = "sdmc:/switch/eden-nx2/roms";
constexpr const char* kDefaultUpdatesDir = "sdmc:/switch/eden-nx2/updates/";
constexpr const char* kDefaultDlcDir = "sdmc:/switch/eden-nx2/dlc/";

enum class LsfgBadge { Ready, Missing, Invalid };

LsfgBadge EnsureLosslessInstalled() {
#ifdef HAS_LSFG
    using namespace VideoCore::FrameGen;
    const auto st = GetInstalledLosslessStatus();
    if (st == LosslessStatus::Ok) {
        (void)BuildShaderCache();
        return (GetInstalledLosslessStatus() == LosslessStatus::Ok) ? LsfgBadge::Ready
                                                                    : LsfgBadge::Invalid;
    }
    return (st == LosslessStatus::NotInstalled) ? LsfgBadge::Missing : LsfgBadge::Invalid;
#else
    return LsfgBadge::Missing;
#endif
}

void TryLoadNroMetadata(GameEntry& entry) {
    if (entry.ext != ".nro") return;
    std::FILE* f = std::fopen(entry.full_path.c_str(), "rb");
    if (!f) return;

    std::uint32_t nro_size = 0;
    if (std::fseek(f, 0x18, SEEK_SET) == 0 && std::fread(&nro_size, 4, 1, f) == 1 &&
        nro_size > 0x100 && std::fseek(f, static_cast<long>(nro_size), SEEK_SET) == 0) {
        char magic[4]{};
        std::uint32_t version = 0;
        std::uint64_t icon_off = 0, icon_size = 0;
        std::uint64_t nacp_off = 0, nacp_size = 0;
        if (std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, "ASET", 4) == 0 &&
            std::fread(&version, 4, 1, f) == 1 &&
            std::fread(&icon_off, 8, 1, f) == 1 && std::fread(&icon_size, 8, 1, f) == 1 &&
            std::fread(&nacp_off, 8, 1, f) == 1 && std::fread(&nacp_size, 8, 1, f) == 1) {
            if (nacp_size >= 0x300 && nacp_size <= 0x8000 &&
                std::fseek(f, static_cast<long>(nro_size + nacp_off), SEEK_SET) == 0) {
                char title[0x200]{};
                char author[0x100]{};
                if (std::fread(title, 1, sizeof(title), f) == sizeof(title) &&
                    std::fread(author, 1, sizeof(author), f) == sizeof(author)) {
                    title[sizeof(title) - 1] = '\0';
                    author[sizeof(author) - 1] = '\0';
                    if (title[0] != '\0') entry.display_title = title;
                    if (author[0] != '\0') entry.author = author;
                }
            }
            if (icon_size > 0 && icon_size <= 2 * 1024 * 1024 &&
                std::fseek(f, static_cast<long>(nro_size + icon_off), SEEK_SET) == 0) {
                std::vector<unsigned char> jpg(static_cast<std::size_t>(icon_size));
                if (std::fread(jpg.data(), 1, jpg.size(), f) == jpg.size()) {
                    int w = 0, h = 0, comp = 0;
                    stbi_uc* pixels = stbi_load_from_memory(
                        jpg.data(), static_cast<int>(jpg.size()), &w, &h, &comp, 4);
                    if (pixels && w > 0 && h > 0) {
                        entry.icon56.resize(56u * 56u);
                        for (int dy = 0; dy < 56; ++dy) {
                            const int sy = (dy * h) / 56;
                            for (int dx = 0; dx < 56; ++dx) {
                                const int sx = (dx * w) / 56;
                                const stbi_uc* p = pixels + (sy * w + sx) * 4;
                                entry.icon56[dy * 56 + dx] = PackRGBA(p[0], p[1], p[2], 255);
                            }
                        }
                        stbi_image_free(pixels);
                    }
                }
            }
        }
    }
    std::fclose(f);
}

std::vector<GameEntry> ScanDirectory(const std::string& folder) {
    std::vector<GameEntry> list;
    SafeForEachDirEntry(folder, [&](const std::filesystem::directory_entry& dir_entry,
                                    std::error_code&) {
        std::error_code entry_ec;
        const std::string fname = dir_entry.path().filename().string();
        if (fname.empty() || fname[0] == '.') return;
        const bool is_dir = dir_entry.is_directory(entry_ec);
        if (entry_ec) return;
        const std::string ext = is_dir ? "" : ToLowerExt(dir_entry.path());
        if (!is_dir && !IsSupportedRomExt(ext)) return;
        GameEntry item;
        item.name = fname;
        item.display_title = is_dir ? fname : dir_entry.path().stem().string();
        item.full_path = dir_entry.path().generic_string();
        item.ext = ext;
        item.is_dir = is_dir;
        if (!is_dir) {
            const auto sz = dir_entry.file_size(entry_ec);
            item.size_bytes = entry_ec ? 0 : sz;
            TryLoadNroMetadata(item);
        }
        list.push_back(std::move(item));
    });
    std::sort(list.begin(), list.end(), [](const GameEntry& a, const GameEntry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir;
        return a.display_title < b.display_title;
    });
    return list;
}

std::string FormatSize(std::uint64_t bytes) {
    char buf[32];
    if (bytes >= (1ULL << 30)) {
        std::snprintf(buf, sizeof(buf), "%.2f GB", static_cast<double>(bytes) / (1ULL << 30));
    } else if (bytes >= (1ULL << 20)) {
        std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1ULL << 20));
    } else {
        std::snprintf(buf, sizeof(buf), "%llu KB",
                      static_cast<unsigned long long>(bytes >> 10));
    }
    return buf;
}

std::string ParentFolder(const std::string& folder) {
    if (folder == "sdmc:/" || folder == "sdmc:") return "sdmc:/";
    std::string trimmed = folder;
    while (trimmed.size() > 6 && trimmed.back() == '/') trimmed.pop_back();
    const std::size_t slash = trimmed.find_last_of('/');
    if (slash == std::string::npos || slash < 5) return "sdmc:/";
    return trimmed.substr(0, slash + 1);
}

std::string ReadInitialFolder() {
    std::error_code ec;
    std::filesystem::create_directories(kDefaultRomsDir, ec);
    std::filesystem::create_directories(kDefaultUpdatesDir, ec);
    std::filesystem::create_directories(kDefaultDlcDir, ec);
    std::filesystem::create_directories("sdmc:/switch/eden-nx2/games", ec);
    std::ifstream in(kLastFolderPath);
    std::string folder;
    if (std::getline(in, folder) && !folder.empty() && std::filesystem::exists(folder, ec)) {
        if (folder.back() != '/') folder.push_back('/');
        return folder;
    }
    return std::string(kDefaultRomsDir) + "/";
}

void SaveLastFolder(const std::string& folder) {
    std::ofstream out(kLastFolderPath, std::ios::trunc);
    out << folder << '\n';
}

struct SettingOption {
    std::string key;
    std::string label_pt;
    std::string label_en;
    std::string help_pt;
    std::string help_en;
    std::vector<std::string> values;
    std::vector<std::string> labels_pt;
    std::vector<std::string> labels_en;
    int selected = 0;
    bool per_game = true;
};

std::vector<SettingOption> MakeDefaultOptions() {
    return {
        {"controller",
         "Controle (Player 1)", "Controller (Player 1)",
         "Perfil de controle. Para Let's Go Pikachu use Handheld ou Joy-Con Direito.",
         "Controller profile. For Let's Go Pikachu use Handheld or Right Joy-Con.",
         {"pro_controller", "handheld", "dual_joycon", "right_joycon", "left_joycon"},
         {"Pro Controller", "Handheld (Portatil / Let's Go)", "Dual Joy-Con", "Joy-Con Direito (Let's Go)", "Joy-Con Esquerdo"},
         {"Pro Controller", "Handheld (Portable / Let's Go)", "Dual Joy-Con", "Right Joy-Con (Let's Go)", "Left Joy-Con"},
         0, true},
        {"console_mode",
         "Modo do Console", "Console Mode",
         "Handheld renderiza em 720p (mais leve); Docked em 1080p.",
         "Handheld renders at 720p (faster); Docked renders at 1080p.",
         {"handheld", "docked"},
         {"Handheld (720p)", "Docked (1080p)"},
         {"Handheld (720p)", "Docked (1080p)"},
         0, true},
        {"resolution",
         "Resolucao Interna", "Internal Resolution",
         "Escala de renderizacao. 0.5x ou 0.75x aumentam bastante o FPS em 3D.",
         "Rendering scale. 0.5x or 0.75x greatly improve FPS in 3D games.",
         {"0.5", "0.75", "1", "1.5", "2"},
         {"0.5x (Mais Rapido)", "0.75x (Equilibrado)", "1.0x (Nativo)", "1.5x", "2.0x"},
         {"0.5x (Fastest)", "0.75x (Balanced)", "1.0x (Native)", "1.5x", "2.0x"},
         2, true},
        {"gpu_mode",
         "Precisao da GPU", "GPU Accuracy",
         "Fast liga Shaders Assincronos e DMA rapido; Accurate evita glitches.",
         "Fast enables Async Shaders & fast DMA; Accurate avoids glitches.",
         {"fast", "accurate"},
         {"Fast (Recomendado)", "Accurate (Preciso)"},
         {"Fast (Recommended)", "Accurate"},
         0, true},
        {"cpu_accuracy",
         "Precisao da CPU", "CPU Accuracy",
         "Unsafe usa atalhos rapidos no Dynarmic para ganhar FPS em jogos pesados.",
         "Unsafe enables fast Dynarmic optimizations for higher FPS.",
         {"unsafe", "auto"},
         {"Unsafe (Mais Rapido)", "Auto (Padrao)"},
         {"Unsafe (Faster)", "Auto (Default)"},
         0, true},
        {"frame_gen",
         "Lossless Scaling (LSFG)", "Lossless Scaling (LSFG)",
         "Gera frames extras na GPU Switch 2 (coloque Lossless.dll em eden-nx2/).",
         "Generates extra frames on Switch 2 GPU (place Lossless.dll in eden-nx2/).",
         {"off", "2x", "3x", "4x"},
         {"Desligado", "2x Frame Gen", "3x Frame Gen", "4x Frame Gen"},
         {"Off", "2x Frame Gen", "3x Frame Gen", "4x Frame Gen"},
         0, true},
        {"show_fps",
         "Mostrar FPS na Tela", "Show On-Screen FPS",
         "Exibe contador de FPS e velocidade no canto superior esquerdo do jogo.",
         "Displays FPS and emulation speed counter at top-left in game.",
         {"on", "detailed", "off"},
         {"FPS + Velocidade", "Detalhado (FPS + Modo)", "Desligado"},
         {"FPS + Speed", "Detailed (FPS + Mode)", "Off"},
         0, true},
        {"fastload_boost",
         "Modo de Clock (FastLoad)", "Clock Mode (FastLoad)",
         "Normal usa GPU total (recomendado). FastLoad sobe CPU mas REDUZ a GPU.",
         "Normal keeps full GPU clocks (recommended). FastLoad boosts CPU but throttles GPU.",
         {"off", "on"},
         {"Normal (GPU Total)", "FastLoad (CPU+/GPU-)"},
         {"Normal (Full GPU)", "FastLoad (CPU+/GPU-)"},
         0, true},
        {"address_space",
         "Espaco de RAM (Tabela)", "Memory Address Space",
         "38-bit (512MB) equilibrado; 36-bit (128MB) libera mais RAM; 39-bit (1GB).",
         "38-bit (512MB) balanced; 36-bit (128MB) more free RAM; 39-bit (1GB).",
         {"38bit", "36bit", "39bit"},
         {"38-bit (Equilibrado)", "36-bit (Compacto)", "39-bit (Completo)"},
         {"38-bit (Balanced)", "36-bit (Compact)", "39-bit (Full)"},
         0, true},
        {"jit_ahead",
         "JIT Blocos Antecipados", "JIT Ahead Blocks",
         "Pre-compila blocos sucessores em um unico IPC kaiHEN reduzindo stutter.",
         "Pre-compiles successor blocks in one kaiHEN IPC call to cut stutter.",
         {"8", "4", "16", "0"},
         {"8 Blocos (Padrao)", "4 Blocos", "16 Blocos", "Desligado"},
         {"8 Blocks (Default)", "4 Blocks", "16 Blocks", "Off"},
         0, true},
        {"shader_cache",
         "Shader Cache em Disco", "Disk Shader Cache",
         "Salva shaders Vulkan no cartao SD para reduzir travamentos futuros.",
         "Saves compiled Vulkan shaders to SD card to eliminate repeat stutter.",
         {"on", "off"},
         {"Ligado", "Desligado"},
         {"On", "Off"},
         0, true},
        {"language",
         "Idioma dos Jogos", "Game Language",
         "Idioma e regiao informados aos jogos emulados.",
         "System language and region reported to emulated games.",
         {"pt-BR", "en-US", "es-419", "es-ES", "fr", "de", "it", "ja"},
         {"Portugues (Brasil)", "Ingles (EUA)", "Espanhol (LatAm)", "Espanhol (ES)", "Frances", "Alemao", "Italiano", "Japones"},
         {"Portuguese (Brazil)", "English (US)", "Spanish (LatAm)", "Spanish (Spain)", "French", "German", "Italian", "Japanese"},
         0, true},
        {"ui_lang",
         "Idioma do Menu", "Launcher Language",
         "Idioma da interface do Eden-NX2 (ou aperte [-] na tela inicial).",
         "Eden-NX2 interface language (or press [-] on main screen).",
         {"pt", "en"},
         {"Portugues", "English"},
         {"Portuguese", "English"},
         0, false},
    };
}

std::string BaseName(std::string_view path) {
    while (!path.empty() && (path.back() == '/' || path.back() == '\\')) {
        path.remove_suffix(1);
    }
    const std::size_t slash = path.find_last_of("/\\");
    return std::string((slash == std::string_view::npos) ? path : path.substr(slash + 1));
}

std::vector<std::string> SplitPipe(const std::string& s) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < s.size()) {
        const std::size_t bar = s.find('|', start);
        const std::string part = (bar == std::string::npos) ? s.substr(start)
                                                            : s.substr(start, bar - start);
        if (!part.empty()) out.push_back(part);
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    return out;
}

std::string JoinPipe(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out.push_back('|');
        out += items[i];
    }
    return out;
}

std::string FormatAddonSummary(bool is_dlc, const std::string& val, bool en) {
    if (val.empty() || val == "auto") {
        return is_dlc ? T(en, "Auto (pasta dlc/)", "Auto (dlc/ folder)")
                      : T(en, "Auto (pasta updates/)", "Auto (updates/ folder)");
    }
    if (val == "none") {
        return is_dlc ? T(en, "Desativado (Sem DLC)", "Disabled (No DLC)")
                      : T(en, "Desativado (Base v1.0)", "Disabled (Base v1.0)");
    }
    if (!is_dlc) {
        return BaseName(val);
    }
    const auto items = SplitPipe(val);
    if (items.empty()) {
        return T(en, "Auto (pasta dlc/)", "Auto (dlc/ folder)");
    }
    if (items.size() == 1) {
        return BaseName(items[0]);
    }
    return std::to_string(items.size()) + (en ? " DLCs selected" : " DLCs selecionadas");
}

void SetAddonOptionValue(SettingOption& opt, const std::string& val) {
    const bool is_dlc = (opt.key == "dlc_pkg");
    const auto it = std::find(opt.values.begin(), opt.values.end(), val);
    if (it != opt.values.end()) {
        opt.selected = static_cast<int>(it - opt.values.begin());
        return;
    }
    opt.values.push_back(val);
    opt.labels_pt.push_back(FormatAddonSummary(is_dlc, val, false));
    opt.labels_en.push_back(FormatAddonSummary(is_dlc, val, true));
    opt.selected = static_cast<int>(opt.values.size()) - 1;
}

void LoadIniInto(std::vector<SettingOption>& opts, const std::string& path, bool is_per_game) {
    std::ifstream in(path);
    if (!in) return;
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
            line.pop_back();
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq);
        const std::string v = line.substr(eq + 1);
        for (auto& opt : opts) {
            if (opt.key != k) continue;
            if (opt.key == "update_pkg" || opt.key == "dlc_pkg") {
                SetAddonOptionValue(opt, v);
                continue;
            }
            const auto it = std::find(opt.values.begin(), opt.values.end(), v);
            if (it != opt.values.end()) {
                opt.selected = static_cast<int>(it - opt.values.begin());
            } else if (is_per_game && v == "inherit") {
                opt.selected = 0;
            }
        }
    }
}

void SaveGlobalIni(const std::vector<SettingOption>& opts) {
    std::error_code ec;
    std::filesystem::create_directories("sdmc:/switch/eden-nx2", ec);
    std::ofstream out(kGlobalSettingsPath, std::ios::trunc);
    for (const auto& opt : opts) {
        out << opt.key << '=' << opt.values[static_cast<std::size_t>(opt.selected)] << '\n';
    }
}

struct AddonItem {
    std::string label;
    std::string full_path;
    std::string source_tag;
    std::uint64_t size_bytes = 0;
    bool is_dir = false;
};

struct AddonLists {
    std::vector<AddonItem> updates;
    std::vector<AddonItem> dlcs;
};

AddonLists ScanAddonListsOnce() {
    AddonLists cache;
    SafeForEachDirEntry(kDefaultUpdatesDir,
                        [&](const std::filesystem::directory_entry& e, std::error_code&) {
                            std::error_code ec;
                            const std::string fname = e.path().filename().string();
                            if (fname.empty() || fname[0] == '.') return;
                            const std::string full = e.path().generic_string();
                            if (e.is_directory(ec)) {
                                SafeForEachDirEntry(
                                    full + "/",
                                    [&](const std::filesystem::directory_entry& sub, std::error_code&) {
                                        std::error_code sec;
                                        const std::string sname = sub.path().filename().string();
                                        if (sname.empty() || sname[0] == '.' || sub.is_directory(sec)) return;
                                        if (IsAddonExt(ToLowerExt(sub.path()))) {
                                            const auto sz = sub.file_size(sec);
                                            cache.updates.push_back(AddonItem{
                                                fname + "/" + sname, sub.path().generic_string(),
                                                "UPD", sec ? 0 : sz, false});
                                        }
                                    });
                                return;
                            }
                            if (IsAddonExt(ToLowerExt(e.path()))) {
                                const auto sz = e.file_size(ec);
                                cache.updates.push_back(
                                    AddonItem{fname, full, "UPD", ec ? 0 : sz, false});
                            }
                        });
    std::sort(cache.updates.begin(), cache.updates.end(),
              [](const AddonItem& a, const AddonItem& b) { return a.label < b.label; });

    SafeForEachDirEntry(kDefaultDlcDir,
                        [&](const std::filesystem::directory_entry& e, std::error_code&) {
                            std::error_code ec;
                            const std::string fname = e.path().filename().string();
                            if (fname.empty() || fname[0] == '.') return;
                            const std::string full = e.path().generic_string();
                            if (e.is_directory(ec)) {
                                cache.dlcs.push_back(AddonItem{fname, full, "DIR", 0, true});
                                SafeForEachDirEntry(
                                    full + "/",
                                    [&](const std::filesystem::directory_entry& sub, std::error_code&) {
                                        std::error_code sec;
                                        const std::string sname = sub.path().filename().string();
                                        if (sname.empty() || sname[0] == '.' || sub.is_directory(sec)) return;
                                        if (IsAddonExt(ToLowerExt(sub.path()))) {
                                            const auto sz = sub.file_size(sec);
                                            cache.dlcs.push_back(AddonItem{
                                                fname + "/" + sname, sub.path().generic_string(),
                                                "DLC", sec ? 0 : sz, false});
                                        }
                                    });
                                return;
                            }
                            if (IsAddonExt(ToLowerExt(e.path()))) {
                                const auto sz = e.file_size(ec);
                                cache.dlcs.push_back(
                                    AddonItem{fname, full, "DLC", ec ? 0 : sz, false});
                            }
                        });
    std::sort(cache.dlcs.begin(), cache.dlcs.end(), [](const AddonItem& a, const AddonItem& b) {
        if (a.is_dir != b.is_dir) return a.is_dir;
        return a.label < b.label;
    });
    return cache;
}

std::vector<SettingOption> BuildPerGameAddonOptions(const std::string&, const AddonLists&,
                                                    const std::vector<GameEntry>&) {
    SettingOption upd;
    upd.key = "update_pkg";
    upd.label_pt = "Update do Jogo";
    upd.label_en = "Game Update";
    upd.help_pt = "Pressione [A] para abrir a lista completa ou escolher um arquivo Update no cartao SD.";
    upd.help_en = "Press [A] to open the Update picker or browse any file on the SD card.";
    upd.values = {"auto", "none"};
    upd.labels_pt = {"Auto (pasta updates/)", "Desativado (Base v1.0)"};
    upd.labels_en = {"Auto (updates/ folder)", "Disabled (Base v1.0)"};
    upd.selected = 0;
    upd.per_game = true;

    SettingOption dlc;
    dlc.key = "dlc_pkg";
    dlc.label_pt = "Pacotes DLC";
    dlc.label_en = "DLC Packages";
    dlc.help_pt = "Pressione [A] para marcar uma ou mais DLCs / pastas no gerenciador de DLC.";
    dlc.help_en = "Press [A] to open the multi-select DLC manager or browse folders/files on SD.";
    dlc.values = {"auto", "none"};
    dlc.labels_pt = {"Auto (pasta dlc/)", "Desativado (Sem DLC)"};
    dlc.labels_en = {"Auto (dlc/ folder)", "Disabled (No DLC)"};
    dlc.selected = 0;
    dlc.per_game = true;

    return {std::move(upd), std::move(dlc)};
}

std::vector<SettingOption> MakePerGameOptions(const std::vector<SettingOption>& global_opts,
                                                const std::string& rom_path, bool en,
                                                const AddonLists& cache,
                                                const std::vector<GameEntry>& dir_entries) {
    std::vector<SettingOption> out = BuildPerGameAddonOptions(rom_path, cache, dir_entries);
    for (const auto& opt : global_opts) {
        if (!opt.per_game) continue;
        SettingOption copy = opt;
        const auto& src_labels = en ? opt.labels_en : opt.labels_pt;
        const std::string global_lbl =
            src_labels.empty()
                ? std::string{}
                : src_labels[static_cast<std::size_t>(
                      std::clamp(opt.selected, 0, static_cast<int>(src_labels.size()) - 1))];
        const std::string inh_pt = "Padrao Global (" + global_lbl + ")";
        const std::string inh_en = "Global Default (" + global_lbl + ")";
        copy.values.insert(copy.values.begin(), "inherit");
        copy.labels_pt.insert(copy.labels_pt.begin(), inh_pt);
        copy.labels_en.insert(copy.labels_en.begin(), inh_en);
        copy.selected = 0;
        out.push_back(std::move(copy));
    }
    LoadIniInto(out, GameConfigPath(rom_path), true);
    return out;
}

void SavePerGameIni(const std::vector<SettingOption>& game_opts, const std::string& rom_path) {
    std::error_code ec;
    std::filesystem::create_directories("sdmc:/switch/eden-nx2/games", ec);
    const std::string path = GameConfigPath(rom_path);
    const bool all_inherit =
        std::all_of(game_opts.begin(), game_opts.end(), [](const auto& o) { return o.selected == 0; });
    if (all_inherit) {
        std::filesystem::remove(path, ec);
        return;
    }
    std::ofstream out(path, std::ios::trunc);
    for (const auto& opt : game_opts) {
        if (opt.selected > 0) {
            out << opt.key << '=' << opt.values[static_cast<std::size_t>(opt.selected)] << '\n';
        }
    }
}

std::vector<SettingOption> EffectiveOptions(const std::vector<SettingOption>& global_opts,
                                            const std::string& rom_path, bool en,
                                            const AddonLists& cache,
                                            const std::vector<GameEntry>& dir_entries) {
    std::vector<SettingOption> eff = global_opts;
    const auto game_opts = MakePerGameOptions(global_opts, rom_path, en, cache, dir_entries);
    for (const auto& g : game_opts) {
        if (g.key == "update_pkg" || g.key == "dlc_pkg") {
            eff.push_back(g);
            continue;
        }
        if (g.selected <= 0) continue;
        for (auto& e : eff) {
            if (e.key == g.key && !e.values.empty()) {
                e.selected = std::clamp(g.selected - 1, 0, static_cast<int>(e.values.size()) - 1);
                break;
            }
        }
    }
    return eff;
}

std::string GetValue(const std::vector<SettingOption>& opts, std::string_view key) {
    for (const auto& o : opts) {
        if (o.key == key && !o.values.empty()) {
            const int idx = std::clamp(o.selected, 0, static_cast<int>(o.values.size()) - 1);
            return o.values[static_cast<std::size_t>(idx)];
        }
    }
    return {};
}

std::string GetLabel(const std::vector<SettingOption>& opts, std::string_view key, bool en) {
    for (const auto& o : opts) {
        if (o.key == key) {
            const auto& lbls = en ? o.labels_en : o.labels_pt;
            if (lbls.empty()) return {};
            const int idx = std::clamp(o.selected, 0, static_cast<int>(lbls.size()) - 1);
            return lbls[static_cast<std::size_t>(idx)];
        }
    }
    return {};
}

void FillLaunchConfig(const std::vector<SettingOption>& eff, const std::string& rom_path,
                      Switch2LaunchConfig* cfg) {
    cfg->rom_path = rom_path;
    const std::string ctrl = GetValue(eff, "controller");
    if (ctrl == "handheld") cfg->controller_profile = Switch2ControllerProfile::Handheld;
    else if (ctrl == "dual_joycon") cfg->controller_profile = Switch2ControllerProfile::DualJoycon;
    else if (ctrl == "right_joycon") cfg->controller_profile = Switch2ControllerProfile::RightJoycon;
    else if (ctrl == "left_joycon") cfg->controller_profile = Switch2ControllerProfile::LeftJoycon;
    else cfg->controller_profile = Switch2ControllerProfile::ProController;

    cfg->docked_mode = (GetValue(eff, "console_mode") == "docked");
    cfg->resolution_scale = std::strtof(GetValue(eff, "resolution").c_str(), nullptr);
    if (cfg->resolution_scale <= 0.1f) cfg->resolution_scale = 1.0f;
    cfg->gpu_fast = (GetValue(eff, "gpu_mode") == "fast");
    cfg->cpu_unsafe = (GetValue(eff, "cpu_accuracy") == "unsafe");

    const std::string fg = GetValue(eff, "frame_gen");
    cfg->frame_gen_mult = (fg == "2x") ? 2 : (fg == "3x" ? 3 : (fg == "4x" ? 4 : 0));

    const std::string fps = GetValue(eff, "show_fps");
    cfg->show_fps_mode = (fps == "detailed") ? 2 : (fps == "on" ? 1 : 0);

    cfg->cpu_boost = (GetValue(eff, "fastload_boost") == "on");

    const std::string as_mode = GetValue(eff, "address_space");
    cfg->address_space_mode = (as_mode == "36bit") ? 0 : (as_mode == "39bit" ? 2 : 1);

    cfg->jit_blocks_ahead = std::atoi(GetValue(eff, "jit_ahead").c_str());
    cfg->shader_cache = (GetValue(eff, "shader_cache") == "on");
    cfg->language = GetValue(eff, "language");
    cfg->ui_english = (GetValue(eff, "ui_lang") == "en");
    cfg->update_pkg = GetValue(eff, "update_pkg");
    if (cfg->update_pkg.empty()) cfg->update_pkg = "auto";
    cfg->dlc_pkg = GetValue(eff, "dlc_pkg");
    if (cfg->dlc_pkg.empty()) cfg->dlc_pkg = "auto";
}

void DrawFormatBadge(Canvas& cv, int x, int y, const GameEntry& entry) {
    if (!entry.icon56.empty()) {
        cv.DrawIcon56(x, y, entry.icon56);
        return;
    }
    std::uint32_t bg = PackRGBA(45, 75, 115);
    std::string tag = "DIR";
    if (!entry.is_dir) {
        if (entry.ext == ".nsp") {
            bg = PackRGBA(14, 116, 90);
            tag = "NSP";
        } else if (entry.ext == ".nsz") {
            bg = PackRGBA(13, 148, 136);
            tag = "NSZ";
        } else if (entry.ext == ".xci") {
            bg = PackRGBA(168, 95, 14);
            tag = "XCI";
        } else if (entry.ext == ".xcz") {
            bg = PackRGBA(180, 120, 14);
            tag = "XCZ";
        } else if (entry.ext == ".nup") {
            bg = PackRGBA(124, 58, 237);
            tag = "NUP";
        } else if (entry.ext == ".nca") {
            bg = PackRGBA(14, 116, 144);
            tag = "NCA";
        } else {
            bg = PackRGBA(79, 70, 229);
            tag = "NRO";
        }
    }
    cv.DrawRoundedRect(x, y, 56, 56, 10, bg, PackRGBA(255, 255, 255), 255);
    cv.DrawText(x + 4, y + 20, tag, kTextPrimary, 2, true);
}

void DrawStickWidget(Canvas& cv, int cx, int cy, int r, float sx, float sy, std::string_view tag,
                     bool clicked) {
    cv.DrawRoundedRect(cx - r, cy - r, r * 2, r * 2, r,
                       clicked ? PackRGBA(20, 56, 74) : PackRGBA(14, 21, 34),
                       clicked ? kAmber : kBorderDim);
    const int dot_x = cx + static_cast<int>(std::clamp(sx, -1.0f, 1.0f) * (r - 6));
    const int dot_y = cy - static_cast<int>(std::clamp(sy, -1.0f, 1.0f) * (r - 6));
    cv.DrawRoundedRect(dot_x - 5, dot_y - 5, 10, 10, 5, clicked ? kAmber : kAccent);
    cv.DrawText(cx - 8, cy + r + 4, tag, clicked ? kAmber : kTextMuted, 1);
}

void DrawMainScreen(Canvas& cv, const std::string& folder, const std::vector<GameEntry>& entries,
                    int selected, int scroll, const std::vector<SettingOption>& eff,
                    bool has_game, bool en, const Switch2PadSnapshot& pad,
                    LsfgBadge lsfg_badge) {
    cv.FillGradientV(0, 0, kScreenW, kScreenH, kBgTop, kBgBottom);

    // Top bar
    cv.FillRect(0, 0, kScreenW, 68, PackRGBA(15, 23, 38));
    cv.FillRect(0, 66, kScreenW, 2, kAccent);

    cv.DrawPill(24, 16, "EDEN-NX2", kAccent, PackRGBA(7, 22, 16), 2);
    cv.DrawText(186, 18, "Nintendo Switch 2", kTextPrimary, 2, true);
    cv.DrawText(186, 42, "Port by @davidkalil10 (ko-fi.com/davidkalil10)", kTextMuted, 1);

    const char* lsfg_txt =
        (lsfg_badge == LsfgBadge::Ready)
            ? "LSFG OK"
            : ((lsfg_badge == LsfgBadge::Invalid) ? "BAD DLL" : "NO DLL");
    const std::uint32_t lsfg_bg =
        (lsfg_badge == LsfgBadge::Ready)
            ? kAccentSoft
            : ((lsfg_badge == LsfgBadge::Invalid) ? PackRGBA(110, 68, 16) : kCardBg);
    const std::uint32_t lsfg_fg =
        (lsfg_badge == LsfgBadge::Ready)
            ? kAccent
            : ((lsfg_badge == LsfgBadge::Invalid) ? kAmber : kTextMuted);
    cv.DrawPill(615, 18, lsfg_txt, lsfg_bg, lsfg_fg, 2);

    std::uint64_t mem_used = 0, mem_total = 0;
    Switch2GetMemoryStats(&mem_used, &mem_total);
    char ram_buf[64];
    std::snprintf(ram_buf, sizeof(ram_buf), "RAM %llu/%lluM",
                  static_cast<unsigned long long>(mem_used >> 20),
                  static_cast<unsigned long long>(mem_total >> 20));
    cv.DrawPill(765, 18, ram_buf, kCardBg, kCyan, 2);

    char core_buf[32];
    std::snprintf(core_buf, sizeof(core_buf), "CPU 0x%llx",
                  static_cast<unsigned long long>(Switch2GetCoreMask()));
    cv.DrawPill(1075, 18, core_buf, kCardBg, kTextPrimary, 2);

    // Left panel: Library
    cv.DrawRoundedRect(24, 84, 724, 570, 14, kPanelBg, kBorderDim);
    cv.DrawText(42, 98, T(en, "BIBLIOTECA DE JOGOS", "GAME LIBRARY"), kAccent, 2);
    cv.DrawTextMaxWidth(365, 102, 360, folder, kTextMuted, 1);
    cv.FillRect(40, 122, 692, 1, kBorderDim);

    constexpr int kVisible = 6;
    if (entries.empty()) {
        cv.DrawRoundedRect(46, 150, 680, 180, 12, kCardBg, kBorderDim);
        cv.DrawTextMaxWidth(68, 174, 636,
                            T(en, "Nenhuma ROM encontrada nesta pasta.", "No ROMs found in this folder."),
                            kTextPrimary, 2);
        cv.DrawTextMaxWidth(68, 210, 636,
                            T(en, "Coloque arquivos .nsp / .nsz / .xci / .nup / .nro em:",
                              "Place .nsp / .nsz / .xci / .nup / .nro files in:"),
                            kTextMuted, 1);
        cv.DrawTextMaxWidth(68, 230, 636, "sdmc:/switch/eden-nx2/roms/", kAccent, 2);
        cv.DrawTextMaxWidth(68, 266, 636, "Keys: sdmc:/switch/eden-nx2/.local/share/eden/keys/", kTextMuted, 1);
        cv.DrawTextMaxWidth(68, 286, 636,
                            T(en, "Pressione [B] para navegar em outras pastas do cartao SD.",
                              "Press [B] to browse parent folders on the SD card."),
                            kCyan, 1);
    } else {
        for (int i = 0; i < kVisible && (scroll + i) < static_cast<int>(entries.size()); ++i) {
            const int idx = scroll + i;
            const auto& item = entries[static_cast<std::size_t>(idx)];
            const bool is_sel = (idx == selected);
            const int cy = 136 + i * 84;
            cv.DrawRoundedRect(40, cy, 692, 74, 10, is_sel ? kCardSelected : kCardBg,
                               is_sel ? kAccent : kBorderDim);
            DrawFormatBadge(cv, 50, cy + 9, item);
            cv.DrawTextMaxWidth(120, cy + 14, 500, item.display_title, kTextPrimary, 2, is_sel);

            std::string sub = item.is_dir
                                  ? T(en, "Pasta - pressione [A] para abrir", "Folder - press [A] to open")
                                  : (item.author.empty() ? FormatSize(item.size_bytes)
                                                         : (item.author + "  |  " + FormatSize(item.size_bytes)));
            cv.DrawTextMaxWidth(120, cy + 44, 480, sub, kTextMuted, 1);

            if (!item.is_dir) {
                std::error_code ec;
                if (std::filesystem::exists(GameConfigPath(item.full_path), ec)) {
                    cv.DrawPill(602, cy + 20, "CUSTOM", kAccentSoft, kAccent, 1);
                }
            }
        }
    }

    // Right panel: Inspector & Controller Test
    cv.DrawRoundedRect(764, 84, 492, 570, 14, kPanelBg, kBorderDim);
    cv.DrawText(784, 98,
                has_game ? T(en, "PERFIL DO JOGO", "GAME PROFILE")
                         : T(en, "PERFIL GLOBAL", "GLOBAL PROFILE"),
                kAccent, 2);
    cv.FillRect(784, 122, 452, 1, kBorderDim);

    if (!entries.empty() && selected >= 0 && selected < static_cast<int>(entries.size())) {
        cv.DrawTextMaxWidth(784, 132, 452, entries[static_cast<std::size_t>(selected)].display_title,
                            kTextPrimary, 2);
    }

    const std::array<std::pair<const char*, std::string>, 7> rows = {{
        {T(en, "Controle", "Controller"), GetLabel(eff, "controller", en)},
        {T(en, "Modo", "Mode"), GetLabel(eff, "console_mode", en) + " " + GetValue(eff, "resolution") + "x"},
        {"GPU / CPU", GetLabel(eff, "gpu_mode", en) + " | " + GetValue(eff, "cpu_accuracy")},
        {"LSFG / FPS", GetLabel(eff, "frame_gen", en) + " | " + GetValue(eff, "show_fps")},
        {"Clock / RAM", GetLabel(eff, "fastload_boost", en) + " | " + GetValue(eff, "address_space")},
        {"JIT / Shader", GetValue(eff, "jit_ahead") + " blk | " + GetValue(eff, "shader_cache") +
                             " | " + GetValue(eff, "language")},
        {"Update / DLC", has_game ? (GetLabel(eff, "update_pkg", en) + " | " +
                                     GetLabel(eff, "dlc_pkg", en))
                                  : std::string("Auto | Auto")},
    }};
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const int ry = 164 + static_cast<int>(i) * 32;
        cv.FillRect(784, ry, 452, 26, (i % 2 == 0) ? PackRGBA(22, 32, 49) : PackRGBA(19, 28, 43));
        cv.DrawText(792, ry + 6, rows[i].first, kTextMuted, 1);
        cv.DrawTextMaxWidth(896, ry + 6, 334, rows[i].second, kTextPrimary, 1);
    }

    // Live controller test widget
    cv.DrawRoundedRect(784, 400, 452, 238, 10, kCardBg, kBorderDim);
    cv.DrawText(800, 412, T(en, "TESTE DE CONTROLE AO VIVO", "LIVE CONTROLLER TEST"), kCyan, 2);

    const bool l3 = (pad.raw_held & SWITCH2_BTN_LSTICK) != 0;
    const bool r3 = (pad.raw_held & SWITCH2_BTN_RSTICK) != 0;
    DrawStickWidget(cv, 836, 480, 26, pad.left_x, pad.left_y, "L3", l3);
    DrawStickWidget(cv, 912, 480, 26, pad.right_x, pad.right_y, "R3", r3);

    auto btnColor = [&](std::uint64_t mask) {
        return (pad.raw_held & mask) ? kAccent : PackRGBA(50, 68, 95);
    };
    cv.DrawPill(960, 444, "A", btnColor(SWITCH2_BTN_A), kTextPrimary, 1);
    cv.DrawPill(994, 444, "B", btnColor(SWITCH2_BTN_B), kTextPrimary, 1);
    cv.DrawPill(1028, 444, "X", btnColor(SWITCH2_BTN_X), kTextPrimary, 1);
    cv.DrawPill(1062, 444, "Y", btnColor(SWITCH2_BTN_Y), kTextPrimary, 1);
    cv.DrawPill(1098, 444, "ZL", btnColor(SWITCH2_BTN_ZL), kTextPrimary, 1);
    cv.DrawPill(1140, 444, "ZR", btnColor(SWITCH2_BTN_ZR), kTextPrimary, 1);

    cv.DrawPill(960, 472, "L", btnColor(SWITCH2_BTN_L), kTextPrimary, 1);
    cv.DrawPill(994, 472, "R", btnColor(SWITCH2_BTN_R), kTextPrimary, 1);
    cv.DrawPill(1028, 472, "L3", l3 ? kAmber : PackRGBA(50, 68, 95), kTextPrimary, 1);
    cv.DrawPill(1070, 472, "R3", r3 ? kAmber : PackRGBA(50, 68, 95), kTextPrimary, 1);
    cv.DrawPill(1112, 472, "SL", btnColor(SWITCH2_BTN_SL), kTextPrimary, 1);
    cv.DrawPill(1154, 472, "SR", btnColor(SWITCH2_BTN_SR), kTextPrimary, 1);

    cv.DrawPill(960, 500, "+", btnColor(SWITCH2_BTN_PLUS), kTextPrimary, 1);
    cv.DrawPill(994, 500, "-", btnColor(SWITCH2_BTN_MINUS), kTextPrimary, 1);
    cv.DrawText(1036, 505, pad.has_motion ? "Gyro: OK" : "Gyro: ...",
                pad.has_motion ? kAccent : kTextMuted, 1);

    cv.FillRect(800, 542, 420, 1, kBorderDim);
    cv.DrawText(800, 554, T(en, "Dica Let's Go Pikachu / Eevee:", "Tip Let's Go Pikachu / Eevee:"),
                kAmber, 1);
    cv.DrawText(800, 572, T(en, "Aperte [Y] -> Controle = Handheld ou Joy-Con Dir.",
                            "Press [Y] -> Controller = Handheld or Right Joy-Con"),
                kTextPrimary, 1);
    cv.DrawText(800, 594, T(en, "Voltar ao Launcher: segure [+]+[-] por 2 seg.",
                            "Return to Launcher: hold [+]+[-] for 2 sec."),
                kCyan, 1);
    cv.DrawText(800, 614, T(en, "LSFG: coloque Lossless.dll em sdmc:/switch/eden-nx2/",
                            "LSFG: put Lossless.dll in sdmc:/switch/eden-nx2/"),
                kTextMuted, 1);

    // Bottom footer hints
    cv.FillRect(0, 666, kScreenW, 54, PackRGBA(12, 18, 29));
    cv.FillRect(0, 666, kScreenW, 1, kBorderDim);
    int fx = 24;
    fx += cv.DrawPill(fx, 678, T(en, "[A] Abrir", "[A] Launch"), kAccentSoft, kAccent, 1) + 10;
    fx += cv.DrawPill(fx, 678, T(en, "[B] Voltar", "[B] Up Dir"), kCardBg, kTextPrimary, 1) + 10;
    fx += cv.DrawPill(fx, 678, T(en, "[X] Global", "[X] Global"), kCardBg, kCyan, 1) + 10;
    fx += cv.DrawPill(fx, 678, T(en, "[Y] Do Jogo", "[Y] Per-Game"), kCardBg, kAmber, 1) + 10;
    fx += cv.DrawPill(fx, 678, T(en, "[-] PT/EN", "[-] PT/EN"), kCardBg, kTextPrimary, 1) + 10;
    cv.DrawPill(1136, 678, T(en, "[+] Sair", "[+] Exit"), PackRGBA(120, 28, 36), kTextPrimary, 1);
}

struct PerGameContext {
    std::string rom_path;
    std::string rom_title;
    const AddonLists* addon_cache = nullptr;
    const std::vector<GameEntry>* dir_entries = nullptr;
};

void DrawScrollbar(Canvas& cv, int x, int y, int h, int start, int visible, int total) {
    if (total <= visible || visible <= 0 || h <= 0) return;
    cv.DrawRoundedRect(x, y, 6, h, 3, PackRGBA(28, 40, 60));
    const int thumb_h = std::max(28, (h * visible) / total);
    const int max_start = std::max(1, total - visible);
    const int thumb_y = y + ((h - thumb_h) * start) / max_start;
    cv.DrawRoundedRect(x, thumb_y, 6, thumb_h, 3, kAccent);
}

bool RunSdBrowserModal(Canvas& cv, const std::string& start_dir, bool allow_pick_dir, bool en,
                       std::string* out_path, bool* out_app_exit) {
    struct BrowserItem {
        std::string label;
        std::string full_path;
        std::uint64_t size_bytes = 0;
        bool is_dir = false;
        bool is_parent = false;
    };

    std::string cur_dir = start_dir;
    std::error_code dir_ec;
    if (cur_dir.empty() || !std::filesystem::is_directory(cur_dir, dir_ec)) {
        cur_dir = "sdmc:/switch/eden-nx2/";
        if (!std::filesystem::is_directory(cur_dir, dir_ec)) {
            cur_dir = "sdmc:/";
        }
    }
    if (!cur_dir.empty() && cur_dir.back() != '/') cur_dir.push_back('/');

    auto scan_browser = [](const std::string& dir, bool en_flag) {
        std::vector<BrowserItem> items;
        if (dir != "sdmc:/" && dir != "sdmc:") {
            items.push_back(BrowserItem{
                T(en_flag, ".. (Pasta anterior)", ".. (Parent folder)"),
                ParentFolder(dir), 0, true, true});
        }
        std::vector<BrowserItem> sub;
        SafeForEachDirEntry(dir, [&](const std::filesystem::directory_entry& e, std::error_code&) {
            std::error_code ec;
            const std::string fname = e.path().filename().string();
            if (fname.empty() || fname[0] == '.') return;
            const bool is_dir = e.is_directory(ec);
            if (ec) return;
            if (!is_dir && !IsAddonExt(ToLowerExt(e.path()))) return;
            std::uint64_t sz = 0;
            if (!is_dir) {
                sz = e.file_size(ec);
                if (ec) sz = 0;
            }
            sub.push_back(BrowserItem{fname, e.path().generic_string(), sz, is_dir, false});
        });
        std::sort(sub.begin(), sub.end(), [](const BrowserItem& a, const BrowserItem& b) {
            if (a.is_dir != b.is_dir) return a.is_dir;
            return a.label < b.label;
        });
        items.insert(items.end(), std::make_move_iterator(sub.begin()),
                     std::make_move_iterator(sub.end()));
        return items;
    };

    auto items = scan_browser(cur_dir, en);
    int selected = 0;
    constexpr int kMaxVis = 11;
    bool redraw = true;
    bool prev_touch = true;
    Switch2PadSnapshot pad{};

    while (Switch2PollInput(&pad, false)) {
        const std::uint64_t down = pad.raw_down;
        const int total = static_cast<int>(items.size());

        if (down & SWITCH2_BTN_B) {
            return false;
        }
        if (allow_pick_dir && (down & SWITCH2_BTN_X)) {
            if (out_path) *out_path = cur_dir;
            return true;
        }
        if (total > 0 && (down & (SWITCH2_BTN_UP | SWITCH2_BTN_DOWN | SWITCH2_BTN_LEFT | SWITCH2_BTN_RIGHT))) {
            if (down & SWITCH2_BTN_UP) selected = (selected + total - 1) % total;
            if (down & SWITCH2_BTN_DOWN) selected = (selected + 1) % total;
            if (down & SWITCH2_BTN_LEFT) selected = std::max(0, selected - kMaxVis);
            if (down & SWITCH2_BTN_RIGHT) selected = std::min(total - 1, selected + kMaxVis);
            redraw = true;
        }

        const int start = std::clamp(selected - kMaxVis / 2, 0, std::max(0, total - kMaxVis));
        bool activate = (down & SWITCH2_BTN_A) != 0 && total > 0;
        if (pad.has_touch && !prev_touch) {
            const int tx = static_cast<int>(pad.touch_x * kScreenW);
            const int ty = static_cast<int>(pad.touch_y * kScreenH);
            if (tx >= 96 && tx <= 1180 && ty >= 112 && ty < 112 + kMaxVis * 40) {
                const int tapped = start + (ty - 112) / 40;
                if (tapped >= 0 && tapped < total) {
                    if (tapped == selected) {
                        activate = true;
                    } else {
                        selected = tapped;
                        redraw = true;
                    }
                }
            }
        }
        prev_touch = pad.has_touch;

        if (activate) {
            const auto& it = items[static_cast<std::size_t>(selected)];
            if (it.is_dir) {
                cur_dir = it.full_path;
                if (!cur_dir.empty() && cur_dir.back() != '/') cur_dir.push_back('/');
                items = scan_browser(cur_dir, en);
                selected = 0;
                redraw = true;
            } else {
                if (out_path) *out_path = it.full_path;
                return true;
            }
        }

        if (redraw) {
            cv.FillRect(0, 0, kScreenW, kScreenH, PackRGBA(4, 8, 14), 215);
            cv.DrawRoundedRect(70, 36, 1140, 648, 16, kPanelBg, kCyan);
            cv.DrawTextMaxWidth(
                100, 54, 720,
                allow_pick_dir ? T(en, "PROCURAR DLC NO CARTAO SD", "BROWSE DLC ON SD CARD")
                               : T(en, "PROCURAR UPDATE NO CARTAO SD", "BROWSE UPDATE ON SD CARD"),
                kCyan, 2, true);
            cv.DrawTextMaxWidth(100, 82, 1070, cur_dir, kTextMuted, 1);
            cv.FillRect(96, 100, 1088, 2, kBorderDim);

            const int cur_total = static_cast<int>(items.size());
            const int cur_start =
                std::clamp(selected - kMaxVis / 2, 0, std::max(0, cur_total - kMaxVis));
            const int cur_end = std::min(cur_total, cur_start + kMaxVis);

            if (cur_total == 0) {
                cv.DrawText(112, 150,
                            T(en, "Nenhum arquivo .nsp / .nsz / .xci nesta pasta.",
                              "No .nsp / .nsz / .xci files in this folder."),
                            kTextMuted, 1);
            } else {
                for (int i = cur_start; i < cur_end; ++i) {
                    const int vis = i - cur_start;
                    const int ry = 112 + vis * 40;
                    const bool is_sel = (i == selected);
                    const auto& it = items[static_cast<std::size_t>(i)];
                    cv.DrawRoundedRect(96, ry, 1080, 34, 6, is_sel ? kCardSelected : kCardBg,
                                       is_sel ? kCyan : kBorderDim);
                    const std::uint32_t tag_bg =
                        it.is_parent ? PackRGBA(55, 65, 81)
                                     : (it.is_dir ? PackRGBA(37, 78, 128) : PackRGBA(14, 116, 90));
                    const char* tag_txt = it.is_parent ? "UP" : (it.is_dir ? "DIR" : "PKG");
                    cv.DrawPill(106, ry + 7, tag_txt, tag_bg, kTextPrimary, 1);
                    cv.DrawTextMaxWidth(164, ry + 13, 890, it.label,
                                        is_sel ? kTextPrimary : PackRGBA(220, 228, 240), 1);
                    if (!it.is_dir && it.size_bytes > 0) {
                        cv.DrawTextMaxWidth(1070, ry + 13, 96, FormatSize(it.size_bytes),
                                            kTextMuted, 1);
                    }
                }
                DrawScrollbar(cv, 1188, 112, kMaxVis * 40 - 6, cur_start, kMaxVis, cur_total);
            }

            cv.FillRect(96, 570, 1088, 1, kBorderDim);
            int fx = 100;
            fx += cv.DrawPill(fx, 594, T(en, "[A] Abrir / Selecionar", "[A] Open / Select"),
                              kAccentSoft, kAccent, 1) + 12;
            if (allow_pick_dir) {
                fx += cv.DrawPill(fx, 594, T(en, "[X] Usar esta pasta", "[X] Use this folder"),
                                  PackRGBA(20, 62, 86), kCyan, 1) + 12;
            }
            cv.DrawPill(fx, 594, T(en, "[B] Cancelar", "[B] Cancel"), kCardBg, kTextPrimary, 1);
            cv.Present();
            redraw = false;
        }
        Switch2SleepMs(16);
    }
    if (out_app_exit) *out_app_exit = true;
    return false;
}

bool RunUpdatePickerModal(Canvas& cv, SettingOption& upd_opt, const PerGameContext& ctx, bool en) {
    struct Row {
        enum Kind { Auto, None, Browse, File } kind;
        std::string label;
        std::string value;
        std::string badge;
        std::uint64_t size_bytes = 0;
    };

    const std::string cur_val =
        upd_opt.values.empty()
            ? std::string("auto")
            : upd_opt.values[static_cast<std::size_t>(
                  std::clamp(upd_opt.selected, 0, static_cast<int>(upd_opt.values.size()) - 1))];

    std::vector<Row> rows;
    rows.push_back(Row{Row::Auto,
                       T(en, "Auto - Carregar automaticamente de updates/ e pasta do jogo",
                         "Auto - Load automatically from updates/ and game folder"),
                       "auto", "AUTO", 0});
    rows.push_back(Row{Row::None,
                       T(en, "Desativado - Rodar jogo base v1.0 (sem update)",
                         "Disabled - Run base v1.0 game (no update)"),
                       "none", "OFF", 0});
    rows.push_back(Row{Row::Browse,
                       T(en, "+ Procurar arquivo de Update no cartao SD...",
                         "+ Browse Update file on SD card..."),
                       "", "SD", 0});

    auto add_file_row = [&](const std::string& label, const std::string& full,
                            const std::string& badge, std::uint64_t sz) {
        if (full.empty() || full == ctx.rom_path) return;
        for (const auto& r : rows) {
            if (r.kind == Row::File && r.value == full) return;
        }
        rows.push_back(Row{Row::File, label, full, badge, sz});
    };

    if (cur_val != "auto" && cur_val != "none" && !cur_val.empty()) {
        std::error_code ec;
        const auto sz = std::filesystem::file_size(cur_val, ec);
        add_file_row(BaseName(cur_val), cur_val, "SEL", ec ? 0 : sz);
    }
    if (ctx.addon_cache) {
        for (const auto& it : ctx.addon_cache->updates) {
            add_file_row(it.label, it.full_path, it.source_tag, it.size_bytes);
        }
    }
    if (ctx.dir_entries) {
        for (const auto& e : *ctx.dir_entries) {
            if (!e.is_dir && e.full_path != ctx.rom_path && IsAddonExt(e.ext)) {
                add_file_row(e.name, e.full_path, "ROMS", e.size_bytes);
            }
        }
    }

    int selected = 0;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (rows[static_cast<std::size_t>(i)].value == cur_val &&
            rows[static_cast<std::size_t>(i)].kind != Row::Browse) {
            selected = i;
            break;
        }
    }

    constexpr int kMaxVis = 10;
    bool redraw = true;
    bool prev_touch = true;
    Switch2PadSnapshot pad{};

    while (Switch2PollInput(&pad, false)) {
        const std::uint64_t down = pad.raw_down;
        const int total = static_cast<int>(rows.size());
        if (down & SWITCH2_BTN_B) {
            return true;
        }
        if (down & (SWITCH2_BTN_UP | SWITCH2_BTN_DOWN | SWITCH2_BTN_LEFT | SWITCH2_BTN_RIGHT)) {
            if (down & SWITCH2_BTN_UP) selected = (selected + total - 1) % total;
            if (down & SWITCH2_BTN_DOWN) selected = (selected + 1) % total;
            if (down & SWITCH2_BTN_LEFT) selected = std::max(0, selected - kMaxVis);
            if (down & SWITCH2_BTN_RIGHT) selected = std::min(total - 1, selected + kMaxVis);
            redraw = true;
        }

        const int start = std::clamp(selected - kMaxVis / 2, 0, std::max(0, total - kMaxVis));
        bool activate = (down & SWITCH2_BTN_A) != 0;
        if (pad.has_touch && !prev_touch) {
            const int tx = static_cast<int>(pad.touch_x * kScreenW);
            const int ty = static_cast<int>(pad.touch_y * kScreenH);
            if (tx >= 96 && tx <= 1180 && ty >= 122 && ty < 122 + kMaxVis * 42) {
                const int tapped = start + (ty - 122) / 42;
                if (tapped >= 0 && tapped < total) {
                    if (tapped == selected) {
                        activate = true;
                    } else {
                        selected = tapped;
                        redraw = true;
                    }
                }
            }
        }
        prev_touch = pad.has_touch;

        if (activate) {
            const auto& r = rows[static_cast<std::size_t>(selected)];
            if (r.kind == Row::Browse) {
                std::string picked;
                bool app_exit = false;
                if (RunSdBrowserModal(cv, kDefaultUpdatesDir, false, en, &picked, &app_exit)) {
                    SetAddonOptionValue(upd_opt, picked);
                    return true;
                }
                if (app_exit) return false;
                redraw = true;
            } else {
                SetAddonOptionValue(upd_opt, r.value);
                return true;
            }
        }

        if (redraw) {
            cv.FillRect(0, 0, kScreenW, kScreenH, PackRGBA(4, 8, 14), 215);
            cv.DrawRoundedRect(70, 36, 1140, 648, 16, kPanelBg, kAccent);
            cv.DrawTextMaxWidth(100, 52, 820,
                                T(en, "SELECIONAR UPDATE DO JOGO", "SELECT GAME UPDATE"), kAccent,
                                2, true);
            cv.DrawTextMaxWidth(100, 82, 820, ctx.rom_title, kTextMuted, 1);
            cv.DrawPill(940, 52,
                        std::to_string(selected + 1) + "/" + std::to_string(total),
                        kCardBg, kCyan, 2);
            cv.FillRect(96, 108, 1088, 2, kBorderDim);

            const int cur_start =
                std::clamp(selected - kMaxVis / 2, 0, std::max(0, total - kMaxVis));
            const int cur_end = std::min(total, cur_start + kMaxVis);
            for (int i = cur_start; i < cur_end; ++i) {
                const int vis = i - cur_start;
                const int ry = 122 + vis * 42;
                const bool is_sel = (i == selected);
                const auto& r = rows[static_cast<std::size_t>(i)];
                const bool is_active = (r.kind != Row::Browse && r.value == cur_val);

                cv.DrawRoundedRect(96, ry, 1080, 36, 7,
                                   is_sel ? kCardSelected
                                          : (is_active ? PackRGBA(19, 44, 48) : kCardBg),
                                   is_sel ? kAccent : (is_active ? kCyan : kBorderDim));

                const char* mark = (r.kind == Row::Browse) ? "[+]" : (is_active ? "(*)" : "( )");
                cv.DrawText(108, ry + 14, mark,
                            (r.kind == Row::Browse) ? kCyan : (is_active ? kAccent : kTextMuted), 1);

                const std::uint32_t bbg =
                    (r.kind == Row::Auto)
                        ? kAccentSoft
                        : ((r.kind == Row::None)
                               ? PackRGBA(90, 34, 42)
                               : ((r.kind == Row::Browse) ? PackRGBA(20, 62, 86)
                                                          : PackRGBA(36, 52, 76)));
                cv.DrawPill(144, ry + 8, r.badge, bbg,
                            (r.kind == Row::Auto) ? kAccent : kTextPrimary, 1);

                cv.DrawTextMaxWidth(206, ry + 14, 845, r.label,
                                    is_active ? kAccent : kTextPrimary, 1);
                if (r.size_bytes > 0) {
                    cv.DrawTextMaxWidth(1066, ry + 14, 98, FormatSize(r.size_bytes), kTextMuted, 1);
                }
            }
            DrawScrollbar(cv, 1188, 122, kMaxVis * 42 - 6, cur_start, kMaxVis, total);

            cv.FillRect(96, 566, 1088, 1, kBorderDim);
            const std::string cur_full =
                (cur_val == "auto" || cur_val == "none") ? FormatAddonSummary(false, cur_val, en)
                                                         : cur_val;
            cv.DrawTextMaxWidth(
                100, 578, 1080,
                std::string(T(en, "Atual: ", "Current: ")) + cur_full, kTextMuted, 1);

            int fx = 100;
            fx += cv.DrawPill(fx, 610, T(en, "[A] Selecionar", "[A] Select"), kAccentSoft, kAccent,
                              1) + 12;
            cv.DrawPill(fx, 610, T(en, "[B] Voltar", "[B] Back"), kCardBg, kTextPrimary, 1);
            cv.Present();
            redraw = false;
        }
        Switch2SleepMs(16);
    }
    return false;
}

bool RunDlcPickerModal(Canvas& cv, SettingOption& dlc_opt, const PerGameContext& ctx, bool en) {
    struct Row {
        enum Kind { Auto, None, Browse, Entry } kind;
        std::string label;
        std::string path;
        std::string badge;
        std::uint64_t size_bytes = 0;
        bool is_dir = false;
    };

    const std::string initial_val =
        dlc_opt.values.empty()
            ? std::string("auto")
            : dlc_opt.values[static_cast<std::size_t>(
                  std::clamp(dlc_opt.selected, 0, static_cast<int>(dlc_opt.values.size()) - 1))];

    enum class DlcMode { Auto, None, Custom } mode = DlcMode::Auto;
    std::vector<std::string> checked_paths;
    if (initial_val == "none") {
        mode = DlcMode::None;
    } else if (initial_val != "auto" && !initial_val.empty()) {
        mode = DlcMode::Custom;
        checked_paths = SplitPipe(initial_val);
    }

    auto is_checked = [&](const std::string& p) {
        return mode == DlcMode::Custom &&
               std::find(checked_paths.begin(), checked_paths.end(), p) != checked_paths.end();
    };

    std::vector<Row> rows;
    rows.push_back(Row{Row::Auto,
                       T(en, "Auto - Carregar todas as DLCs de dlc/ e pasta do jogo",
                         "Auto - Load all DLCs from dlc/ and game folder"),
                       "", "AUTO", 0, false});
    rows.push_back(Row{Row::None,
                       T(en, "Desativado - Rodar sem nenhuma DLC",
                         "Disabled - Run without any DLC"),
                       "", "OFF", 0, false});
    rows.push_back(Row{Row::Browse,
                       T(en, "+ Adicionar arquivo ou pasta de DLC do cartao SD...",
                         "+ Add DLC file or folder from SD card..."),
                       "", "SD", 0, false});

    auto add_entry_row = [&](const std::string& label, const std::string& full,
                             const std::string& badge, std::uint64_t sz, bool is_dir) {
        if (full.empty() || full == ctx.rom_path) return;
        for (const auto& r : rows) {
            if (r.kind == Row::Entry && r.path == full) return;
        }
        rows.push_back(Row{Row::Entry, label, full, badge, sz, is_dir});
    };

    for (const auto& p : checked_paths) {
        std::error_code ec;
        const bool is_d = std::filesystem::is_directory(p, ec);
        std::uint64_t sz = 0;
        if (!is_d) {
            const auto fsz = std::filesystem::file_size(p, ec);
            sz = ec ? 0 : fsz;
        }
        add_entry_row(BaseName(p), p, is_d ? "DIR" : "SEL", sz, is_d);
    }
    if (ctx.addon_cache) {
        for (const auto& it : ctx.addon_cache->dlcs) {
            add_entry_row(it.label, it.full_path, it.source_tag, it.size_bytes, it.is_dir);
        }
    }
    if (ctx.dir_entries) {
        for (const auto& e : *ctx.dir_entries) {
            if (!e.is_dir && e.full_path != ctx.rom_path && IsAddonExt(e.ext)) {
                add_entry_row(e.name, e.full_path, "ROMS", e.size_bytes, false);
            }
        }
    }

    auto commit_to_opt = [&]() {
        if (mode == DlcMode::None) {
            SetAddonOptionValue(dlc_opt, "none");
        } else if (mode == DlcMode::Auto || checked_paths.empty()) {
            SetAddonOptionValue(dlc_opt, "auto");
        } else {
            SetAddonOptionValue(dlc_opt, JoinPipe(checked_paths));
        }
    };

    int selected = (mode == DlcMode::None) ? 1 : 0;
    if (mode == DlcMode::Custom && rows.size() > 3) selected = 3;
    constexpr int kMaxVis = 10;
    bool redraw = true;
    bool prev_touch = true;
    Switch2PadSnapshot pad{};

    while (Switch2PollInput(&pad, false)) {
        const std::uint64_t down = pad.raw_down;
        const int total = static_cast<int>(rows.size());
        if (down & SWITCH2_BTN_B) {
            commit_to_opt();
            return true;
        }
        if (down & SWITCH2_BTN_X) {
            mode = DlcMode::Auto;
            checked_paths.clear();
            redraw = true;
        }
        if (down & (SWITCH2_BTN_UP | SWITCH2_BTN_DOWN | SWITCH2_BTN_LEFT | SWITCH2_BTN_RIGHT)) {
            if (down & SWITCH2_BTN_UP) selected = (selected + total - 1) % total;
            if (down & SWITCH2_BTN_DOWN) selected = (selected + 1) % total;
            if (down & SWITCH2_BTN_LEFT) selected = std::max(0, selected - kMaxVis);
            if (down & SWITCH2_BTN_RIGHT) selected = std::min(total - 1, selected + kMaxVis);
            redraw = true;
        }

        const int start = std::clamp(selected - kMaxVis / 2, 0, std::max(0, total - kMaxVis));
        bool activate = (down & SWITCH2_BTN_A) != 0;
        if (pad.has_touch && !prev_touch) {
            const int tx = static_cast<int>(pad.touch_x * kScreenW);
            const int ty = static_cast<int>(pad.touch_y * kScreenH);
            if (tx >= 96 && tx <= 1180 && ty >= 122 && ty < 122 + kMaxVis * 42) {
                const int tapped = start + (ty - 122) / 42;
                if (tapped >= 0 && tapped < total) {
                    if (tapped == selected) {
                        activate = true;
                    } else {
                        selected = tapped;
                        redraw = true;
                    }
                }
            }
        }
        prev_touch = pad.has_touch;

        if (activate) {
            const auto& r = rows[static_cast<std::size_t>(selected)];
            if (r.kind == Row::Auto) {
                mode = DlcMode::Auto;
                checked_paths.clear();
                redraw = true;
            } else if (r.kind == Row::None) {
                mode = DlcMode::None;
                checked_paths.clear();
                redraw = true;
            } else if (r.kind == Row::Browse) {
                std::string picked;
                bool app_exit = false;
                if (RunSdBrowserModal(cv, kDefaultDlcDir, true, en, &picked, &app_exit)) {
                    mode = DlcMode::Custom;
                    if (std::find(checked_paths.begin(), checked_paths.end(), picked) ==
                        checked_paths.end()) {
                        checked_paths.push_back(picked);
                    }
                    std::error_code ec;
                    const bool is_d = std::filesystem::is_directory(picked, ec);
                    std::uint64_t sz = 0;
                    if (!is_d) {
                        const auto fsz = std::filesystem::file_size(picked, ec);
                        sz = ec ? 0 : fsz;
                    }
                    add_entry_row(BaseName(picked), picked, is_d ? "DIR" : "SD", sz, is_d);
                }
                if (app_exit) return false;
                redraw = true;
            } else if (r.kind == Row::Entry) {
                mode = DlcMode::Custom;
                auto it = std::find(checked_paths.begin(), checked_paths.end(), r.path);
                if (it != checked_paths.end()) {
                    checked_paths.erase(it);
                    if (checked_paths.empty()) mode = DlcMode::Auto;
                } else {
                    checked_paths.push_back(r.path);
                }
                redraw = true;
            }
        }

        if (redraw) {
            cv.FillRect(0, 0, kScreenW, kScreenH, PackRGBA(4, 8, 14), 215);
            cv.DrawRoundedRect(70, 36, 1140, 648, 16, kPanelBg, kCyan);
            cv.DrawTextMaxWidth(100, 52, 780,
                                T(en, "GERENCIAR PACOTES DLC", "MANAGE DLC PACKAGES"), kCyan, 2,
                                true);
            cv.DrawTextMaxWidth(100, 82, 780, ctx.rom_title, kTextMuted, 1);

            const std::string status_pill =
                (mode == DlcMode::None)
                    ? T(en, "SEM DLC", "NO DLC")
                    : ((mode == DlcMode::Auto || checked_paths.empty())
                           ? "AUTO"
                           : (std::to_string(checked_paths.size()) + " DLC"));
            cv.DrawPill(940, 52, status_pill,
                        (mode == DlcMode::Custom && !checked_paths.empty()) ? kAccentSoft : kCardBg,
                        (mode == DlcMode::Custom && !checked_paths.empty()) ? kAccent : kCyan, 2);
            cv.FillRect(96, 108, 1088, 2, kBorderDim);

            const int cur_total = static_cast<int>(rows.size());
            const int cur_start =
                std::clamp(selected - kMaxVis / 2, 0, std::max(0, cur_total - kMaxVis));
            const int cur_end = std::min(cur_total, cur_start + kMaxVis);
            for (int i = cur_start; i < cur_end; ++i) {
                const int vis = i - cur_start;
                const int ry = 122 + vis * 42;
                const bool is_sel = (i == selected);
                const auto& r = rows[static_cast<std::size_t>(i)];
                const bool checked =
                    (r.kind == Row::Auto && mode == DlcMode::Auto) ||
                    (r.kind == Row::None && mode == DlcMode::None) ||
                    (r.kind == Row::Entry && is_checked(r.path));

                cv.DrawRoundedRect(96, ry, 1080, 36, 7,
                                   is_sel ? kCardSelected
                                          : (checked ? PackRGBA(19, 44, 48) : kCardBg),
                                   is_sel ? kCyan : (checked ? kAccent : kBorderDim));

                const char* mark = (r.kind == Row::Browse)
                                       ? "[+]"
                                       : ((r.kind == Row::Entry) ? (checked ? "[X]" : "[ ]")
                                                                 : (checked ? "(*)" : "( )"));
                cv.DrawText(108, ry + 14, mark,
                            (r.kind == Row::Browse) ? kCyan : (checked ? kAccent : kTextMuted), 1);

                const std::uint32_t bbg =
                    (r.kind == Row::Auto)
                        ? kAccentSoft
                        : ((r.kind == Row::None)
                               ? PackRGBA(90, 34, 42)
                               : ((r.kind == Row::Browse)
                                      ? PackRGBA(20, 62, 86)
                                      : (r.is_dir ? PackRGBA(37, 78, 128) : PackRGBA(36, 52, 76))));
                cv.DrawPill(144, ry + 8, r.badge, bbg,
                            (r.kind == Row::Auto) ? kAccent : kTextPrimary, 1);

                cv.DrawTextMaxWidth(206, ry + 14, 845, r.label,
                                    checked ? kAccent : kTextPrimary, 1);
                if (!r.is_dir && r.size_bytes > 0) {
                    cv.DrawTextMaxWidth(1066, ry + 14, 98, FormatSize(r.size_bytes), kTextMuted, 1);
                }
            }
            DrawScrollbar(cv, 1188, 122, kMaxVis * 42 - 6, cur_start, kMaxVis, cur_total);

            cv.FillRect(96, 566, 1088, 1, kBorderDim);
            cv.DrawTextMaxWidth(
                100, 578, 1080,
                T(en, "Dica: marque quantas DLCs quiser com [A] (suporta multiplos arquivos e pastas).",
                  "Tip: toggle as many DLCs as needed with [A] (supports multiple files and folders)."),
                kTextMuted, 1);

            int fx = 100;
            fx += cv.DrawPill(fx, 610, T(en, "[A] Marcar / Escolher", "[A] Toggle / Pick"),
                              kAccentSoft, kAccent, 1) + 12;
            fx += cv.DrawPill(fx, 610, T(en, "[X] Limpar (Auto)", "[X] Reset (Auto)"), kCardBg,
                              kAmber, 1) + 12;
            cv.DrawPill(fx, 610, T(en, "[B] Salvar e Voltar", "[B] Save & Back"), kCardBg,
                        kTextPrimary, 1);
            cv.Present();
            redraw = false;
        }
        Switch2SleepMs(16);
    }
    return false;
}

void DrawSettingsModal(Canvas& cv, const std::vector<SettingOption>& opts, int selected,
                       const std::string& title, bool en) {
    cv.FillRect(0, 0, kScreenW, kScreenH, PackRGBA(4, 8, 14), 205);
    cv.DrawRoundedRect(70, 36, 1140, 648, 16, kPanelBg, kAccent);
    constexpr int kMaxVis = 11;
    const int total = static_cast<int>(opts.size());
    cv.DrawTextMaxWidth(100, 54, 960, title, kAccent, 2, true);
    if (total > 0) {
        cv.DrawPill(1090, 50, std::to_string(selected + 1) + "/" + std::to_string(total), kCardBg,
                    kCyan, 2);
    }
    cv.FillRect(96, 86, 1088, 2, kBorderDim);

    const int start = std::clamp(selected - kMaxVis / 2, 0, std::max(0, total - kMaxVis));
    const int end = std::min(total, start + kMaxVis);
    for (int i = start; i < end; ++i) {
        const int vis = i - start;
        const int ry = 98 + vis * 40;
        const bool is_sel = (i == selected);
        const std::size_t idx = static_cast<std::size_t>(i);
        const bool is_addon_row = (opts[idx].key == "update_pkg" || opts[idx].key == "dlc_pkg");

        cv.DrawRoundedRect(96, ry, 1080, 34, 7,
                           is_sel ? kCardSelected
                                  : (is_addon_row ? PackRGBA(22, 36, 56) : kCardBg),
                           is_sel ? kAccent : (is_addon_row ? PackRGBA(48, 82, 118) : kBorderDim));

        const std::string& lbl = en ? opts[idx].label_en : opts[idx].label_pt;
        const auto& ch_lbls = en ? opts[idx].labels_en : opts[idx].labels_pt;
        const int lbl_scale = (lbl.size() <= 21) ? 2 : 1;
        cv.DrawTextMaxWidth(112, ry + ((lbl_scale == 2) ? 9 : 13), 344, lbl,
                            is_addon_row ? kCyan : kTextPrimary, lbl_scale);

        const std::string raw_val =
            ch_lbls.empty()
                ? std::string{}
                : ch_lbls[static_cast<std::size_t>(
                      std::clamp(opts[idx].selected, 0, static_cast<int>(ch_lbls.size()) - 1))];

        if (is_addon_row) {
            cv.DrawRoundedRect(468, ry + 4, 536, 26, 5, PackRGBA(14, 22, 34), kBorderDim);
            cv.DrawTextMaxWidth(480, ry + 13, 512, raw_val, is_sel ? kAccent : kTextPrimary, 1);
            cv.DrawPill(1016, ry + 6, T(en, "[A] Escolher >", "[A] Select >"),
                        is_sel ? kAccentSoft : kCardBg, is_sel ? kAccent : kCyan, 1);
        } else {
            const std::string val_txt = "< " + raw_val + " >";
            const int val_scale = (val_txt.size() <= 41) ? 2 : 1;
            cv.DrawTextMaxWidth(468, ry + ((val_scale == 2) ? 9 : 13), 692, val_txt,
                                is_sel ? kAccent : kCyan, val_scale);
        }
    }
    DrawScrollbar(cv, 1188, 98, kMaxVis * 40 - 6, start, kMaxVis, total);

    if (selected >= 0 && selected < static_cast<int>(opts.size())) {
        cv.DrawRoundedRect(96, 550, 1088, 52, 8, PackRGBA(14, 22, 34), kBorderDim);
        const std::string& help = en ? opts[static_cast<std::size_t>(selected)].help_en
                                     : opts[static_cast<std::size_t>(selected)].help_pt;
        cv.DrawTextMaxWidth(114, 566, 1052, help, kTextPrimary, 1);
    }

    int fx = 96;
    fx += cv.DrawPill(fx, 618, T(en, "[Cima/Baixo] Navegar", "[Up/Down] Navigate"), kCardBg,
                      kTextPrimary, 1) + 10;
    fx += cv.DrawPill(fx, 618, T(en, "[Esq/Dir ou A] Alterar", "[Left/Right or A] Change"),
                      kAccentSoft, kAccent, 1) + 10;
    cv.DrawPill(fx, 618, T(en, "[B] Salvar e Voltar", "[B] Save & Back"), kCardBg, kCyan, 1);
}

bool RunSettingsModal(Canvas& cv, std::vector<SettingOption>& opts, const std::string& title,
                      bool* ui_en_state = nullptr, const PerGameContext* pg_ctx = nullptr) {
    if (opts.empty()) return true;
    int selected = 0;
    constexpr int kMaxVis = 11;
    bool redraw = true;
    bool prev_touch = true;
    Switch2PadSnapshot pad{};
    while (Switch2PollInput(&pad, false)) {
        const std::uint64_t down = pad.raw_down;
        if (down & (SWITCH2_BTN_B | SWITCH2_BTN_X | SWITCH2_BTN_Y)) {
            return true;
        }
        const int n = static_cast<int>(opts.size());
        if (down & SWITCH2_BTN_UP) {
            selected = (selected + n - 1) % n;
            redraw = true;
        }
        if (down & SWITCH2_BTN_DOWN) {
            selected = (selected + 1) % n;
            redraw = true;
        }

        const int start = std::clamp(selected - kMaxVis / 2, 0, std::max(0, n - kMaxVis));
        bool touch_activate = false;
        if (pad.has_touch && !prev_touch) {
            const int tx = static_cast<int>(pad.touch_x * kScreenW);
            const int ty = static_cast<int>(pad.touch_y * kScreenH);
            if (tx >= 96 && tx <= 1180 && ty >= 98 && ty < 98 + kMaxVis * 40) {
                const int tapped = start + (ty - 98) / 40;
                if (tapped >= 0 && tapped < n) {
                    if (tapped == selected) {
                        touch_activate = true;
                    } else {
                        selected = tapped;
                        redraw = true;
                    }
                }
            }
        }
        prev_touch = pad.has_touch;

        auto& opt = opts[static_cast<std::size_t>(selected)];
        const bool is_addon = (opt.key == "update_pkg" || opt.key == "dlc_pkg");
        const bool en = ui_en_state ? *ui_en_state : false;

        if (is_addon && pg_ctx != nullptr &&
            (touch_activate || (down & (SWITCH2_BTN_A | SWITCH2_BTN_RIGHT | SWITCH2_BTN_LEFT)))) {
            const bool ok = (opt.key == "update_pkg")
                                ? RunUpdatePickerModal(cv, opt, *pg_ctx, en)
                                : RunDlcPickerModal(cv, opt, *pg_ctx, en);
            if (!ok) return false;
            redraw = true;
        } else {
            const int c = static_cast<int>(opt.values.size());
            if (c > 0 && (touch_activate || (down & (SWITCH2_BTN_LEFT | SWITCH2_BTN_RIGHT | SWITCH2_BTN_A)))) {
                const int step = (down & SWITCH2_BTN_LEFT) ? (c - 1) : 1;
                opt.selected = (opt.selected + step) % c;
                if (ui_en_state && opt.key == "ui_lang") {
                    *ui_en_state = (opt.values[static_cast<std::size_t>(opt.selected)] == "en");
                }
                redraw = true;
            }
        }
        if (redraw) {
            const bool cur_en = ui_en_state ? *ui_en_state : false;
            DrawSettingsModal(cv, opts, selected, title, cur_en);
            cv.Present();
            redraw = false;
        }
        Switch2SleepMs(16);
    }
    return false;
}

} // namespace

bool Switch2RunLauncher(Switch2LaunchConfig* out_cfg) {
    if (!out_cfg) return false;
    Switch2InitInput();
    if (!Switch2FbInit()) {
        return false;
    }

    const LsfgBadge lsfg_badge = EnsureLosslessInstalled();

    auto global_opts = MakeDefaultOptions();
    LoadIniInto(global_opts, kGlobalSettingsPath, false);

    std::string folder = ReadInitialFolder();
    const AddonLists addon_cache = ScanAddonListsOnce();
    std::vector<GameEntry> entries = ScanDirectory(folder);
    int selected = 0;
    int scroll = 0;
    constexpr int kVisible = 6;
    bool chosen = false;
    bool prev_touch = false;
    std::uint64_t prev_held = 0;
    float prev_lx = 0.0f, prev_ly = 0.0f, prev_rx = 0.0f, prev_ry = 0.0f;
    bool prev_motion = false;
    int tick_counter = 0;
    bool eff_dirty = true;
    bool cached_has_game = false;
    std::vector<SettingOption> cached_eff = global_opts;

    Canvas cv;
    Switch2PadSnapshot pad{};

    while (Switch2PollInput(&pad, false)) {
        bool en = (GetValue(global_opts, "ui_lang") == "en");
        bool redraw = (tick_counter++ % 15 == 0);
        const std::uint64_t down = pad.raw_down;
        const int count = static_cast<int>(entries.size());

        if (down & SWITCH2_BTN_PLUS) {
            break;
        }

        if (down & SWITCH2_BTN_MINUS) {
            for (auto& o : global_opts) {
                if (o.key == "ui_lang") {
                    o.selected = (o.selected + 1) % 2;
                    en = (o.selected == 1);
                    break;
                }
            }
            SaveGlobalIni(global_opts);
            eff_dirty = true;
            redraw = true;
        }

        if (down & SWITCH2_BTN_X) {
            if (!RunSettingsModal(cv, global_opts,
                                  T(en, "CONFIGURACOES GLOBAIS", "GLOBAL SETTINGS"), &en)) {
                break;
            }
            SaveGlobalIni(global_opts);
            eff_dirty = true;
            redraw = true;
        }

        if ((down & SWITCH2_BTN_Y) && count > 0 && selected >= 0 && selected < count &&
            !entries[static_cast<std::size_t>(selected)].is_dir) {
            const auto& entry = entries[static_cast<std::size_t>(selected)];
            const std::string& rpath = entry.full_path;
            auto game_opts = MakePerGameOptions(global_opts, rpath, en, addon_cache, entries);
            const std::string modal_title =
                std::string(T(en, "CONFIG DO JOGO: ", "GAME SETTINGS: ")) + entry.display_title;
            PerGameContext pg_ctx{rpath, entry.display_title, &addon_cache, &entries};
            if (!RunSettingsModal(cv, game_opts, modal_title, &en, &pg_ctx)) {
                break;
            }
            SavePerGameIni(game_opts, rpath);
            eff_dirty = true;
            redraw = true;
        }

        if (down & (SWITCH2_BTN_UP | SWITCH2_BTN_DOWN | SWITCH2_BTN_LEFT | SWITCH2_BTN_RIGHT)) {
            if (down & SWITCH2_BTN_UP) --selected;
            if (down & SWITCH2_BTN_DOWN) ++selected;
            if (down & SWITCH2_BTN_LEFT) selected -= kVisible;
            if (down & SWITCH2_BTN_RIGHT) selected += kVisible;
            selected = std::clamp(selected, 0, std::max(0, count - 1));
            eff_dirty = true;
            redraw = true;
        }

        if (pad.has_touch && !prev_touch) {
            const int tx = static_cast<int>(pad.touch_x * kScreenW);
            const int ty = static_cast<int>(pad.touch_y * kScreenH);
            if (tx >= 40 && tx <= 732 && ty >= 136 && ty < 136 + kVisible * 84) {
                const int tapped = scroll + (ty - 136) / 84;
                if (tapped >= 0 && tapped < count) {
                    if (tapped == selected) {
                        pad.raw_down |= SWITCH2_BTN_A;
                    } else {
                        selected = tapped;
                        eff_dirty = true;
                        redraw = true;
                    }
                }
            }
        }
        prev_touch = pad.has_touch;

        const bool open = (pad.raw_down & SWITCH2_BTN_A) != 0 && count > 0;
        if (open && !entries[static_cast<std::size_t>(selected)].is_dir) {
            const std::string& rpath = entries[static_cast<std::size_t>(selected)].full_path;
            SaveGlobalIni(global_opts);
            SaveLastFolder(folder);
            const auto eff = EffectiveOptions(global_opts, rpath, en, addon_cache, entries);
            FillLaunchConfig(eff, rpath, out_cfg);
            chosen = true;
            break;
        }
        if (open || (down & SWITCH2_BTN_B)) {
            folder = open ? (entries[static_cast<std::size_t>(selected)].full_path + "/")
                          : ParentFolder(folder);
            entries = ScanDirectory(folder);
            selected = 0;
            scroll = 0;
            eff_dirty = true;
            redraw = true;
        }

        if (down != 0 || pad.raw_held != prev_held || pad.has_touch ||
            pad.has_motion != prev_motion ||
            std::fabs(pad.left_x) > 0.05f || std::fabs(pad.left_y) > 0.05f ||
            std::fabs(pad.right_x) > 0.05f || std::fabs(pad.right_y) > 0.05f ||
            std::fabs(pad.left_x - prev_lx) > 0.01f || std::fabs(pad.left_y - prev_ly) > 0.01f ||
            std::fabs(pad.right_x - prev_rx) > 0.01f || std::fabs(pad.right_y - prev_ry) > 0.01f) {
            redraw = true;
        }
        prev_held = pad.raw_held;
        prev_lx = pad.left_x;
        prev_ly = pad.left_y;
        prev_rx = pad.right_x;
        prev_ry = pad.right_y;
        prev_motion = pad.has_motion;

        if (eff_dirty) {
            cached_has_game = count > 0 && selected >= 0 && selected < count &&
                              !entries[static_cast<std::size_t>(selected)].is_dir;
            cached_eff = cached_has_game
                             ? EffectiveOptions(global_opts,
                                                entries[static_cast<std::size_t>(selected)].full_path,
                                                en, addon_cache, entries)
                             : global_opts;
            eff_dirty = false;
        }

        if (redraw) {
            scroll = std::clamp(scroll, std::max(0, selected - kVisible + 1), selected);
            DrawMainScreen(cv, folder, entries, selected, scroll, cached_eff, cached_has_game, en,
                           pad, lsfg_badge);
            cv.Present();
        }
        Switch2SleepMs(16);
    }

    Switch2FbClose();
    return chosen;
}

#endif // __SWITCH2__
