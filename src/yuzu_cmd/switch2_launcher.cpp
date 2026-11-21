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
#include <vector>

#include "common/stb.h"

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
        const int w = static_cast<int>(text.size()) * 8 * scale + 18;
        const int h = 8 * scale + 10;
        DrawRoundedRect(x, y, w, h, h / 2, bg);
        DrawText(x + 9, y + 5, text, fg, scale, false);
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

bool IsSupportedRomExt(std::string_view ext) {
    return ext == ".nsp" || ext == ".xci" || ext == ".nro" || ext == ".nca";
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
    std::error_code ec;
    if (!std::filesystem::exists(folder, ec)) {
        return list;
    }
    for (const auto& dir_entry : std::filesystem::directory_iterator(folder, ec)) {
        const std::string fname = dir_entry.path().filename().string();
        if (fname.empty() || fname[0] == '.') continue;
        const bool is_dir = dir_entry.is_directory(ec);
        const std::string ext = is_dir ? "" : ToLowerExt(dir_entry.path());
        if (!is_dir && !IsSupportedRomExt(ext)) continue;
        GameEntry item;
        item.name = fname;
        item.display_title = is_dir ? fname : dir_entry.path().stem().string();
        item.full_path = dir_entry.path().generic_string();
        item.ext = ext;
        item.is_dir = is_dir;
        if (!is_dir) {
            item.size_bytes = dir_entry.file_size(ec);
            TryLoadNroMetadata(item);
        }
        list.push_back(std::move(item));
    }
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
    std::string label;
    std::string help;
    std::vector<std::string> values;
    std::vector<std::string> labels;
    int selected = 0;
};

std::vector<SettingOption> MakeDefaultOptions() {
    return {
        {"controller", "Controle (Player 1)",
         "Perfil de controle. Para Pokemon Let's Go Pikachu/Eevee use Handheld ou Joy-Con Direito.",
         {"pro_controller", "handheld", "dual_joycon", "right_joycon", "left_joycon"},
         {"Pro Controller", "Handheld (Let's Go / Portatil)", "Dual Joy-Con", "Joy-Con Direito (Let's Go)", "Joy-Con Esquerdo"},
         0},
        {"console_mode", "Modo do Console",
         "Handheld renderiza tipicamente em 720p (mais leve); Docked em 1080p.",
         {"handheld", "docked"},
         {"Handheld (720p)", "Docked (1080p)"},
         0},
        {"resolution", "Resolucao Interna",
         "Escala de renderizacao. 0.5x ou 0.75x aumentam bastante o FPS em jogos 3D.",
         {"0.5", "0.75", "1", "1.5", "2"},
         {"0.5x (Mais Rapido)", "0.75x (Equilibrado)", "1.0x (Nativo)", "1.5x", "2.0x"},
         2},
        {"gpu_mode", "Precisao da GPU",
         "Fast pula sincronizacoes pesadas para maior velocidade; Accurate evita glitches graficos.",
         {"fast", "accurate"},
         {"Fast (Recomendado Switch 2)", "Accurate (Preciso)"},
         0},
        {"cpu_boost", "CPU Boost (Overclock)",
         "Ativa o modo de clock maximo de carregamento do Switch 2.",
         {"on", "off"},
         {"Ligado (FastLoad)", "Desligado"},
         0},
        {"address_space", "Espaco de RAM (Tabela)",
         "38-bit (512MB) e equilibrado; 36-bit (128MB) libera mais RAM; 39-bit (1GB) p/ jogos raros.",
         {"38bit", "36bit", "39bit"},
         {"38-bit (Equilibrado)", "36-bit (Compacto)", "39-bit (Completo)"},
         0},
        {"jit_ahead", "JIT Compilacao Antecipada",
         "Pre-compila blocos sucessores em um unico IPC kaiHEN reduzindo engasgos.",
         {"8", "4", "16", "0"},
         {"8 Blocos (Padrao)", "4 Blocos", "16 Blocos", "Desligado"},
         0},
        {"shader_cache", "Shader Cache em Disco",
         "Salva shaders Vulkan compilados no SD para eliminar stutter nas proximas aberturas.",
         {"on", "off"},
         {"Ligado", "Desligado"},
         0},
        {"language", "Idioma do Sistema",
         "Idioma e regiao informados aos jogos.",
         {"pt-BR", "en-US", "es-419", "es-ES", "fr", "de", "it", "ja"},
         {"Portugues (Brasil)", "Ingles (EUA)", "Espanhol (LatAm)", "Espanhol (ES)", "Frances", "Alemao", "Italiano", "Japones"},
         0},
    };
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

std::vector<SettingOption> MakePerGameOptions(const std::vector<SettingOption>& global_opts,
                                                const std::string& rom_path) {
    std::vector<SettingOption> out = global_opts;
    for (auto& opt : out) {
        const std::string global_lbl = opt.labels[static_cast<std::size_t>(opt.selected)];
        opt.values.insert(opt.values.begin(), "inherit");
        opt.labels.insert(opt.labels.begin(), "Padrao Global (" + global_lbl + ")");
        opt.selected = 0;
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
                                            const std::string& rom_path) {
    std::vector<SettingOption> eff = global_opts;
    const auto game_opts = MakePerGameOptions(global_opts, rom_path);
    for (std::size_t i = 0; i < eff.size() && i < game_opts.size(); ++i) {
        if (game_opts[i].selected > 0) {
            eff[i].selected = game_opts[i].selected - 1;
        }
    }
    return eff;
}

std::string GetValue(const std::vector<SettingOption>& opts, std::string_view key) {
    for (const auto& o : opts) {
        if (o.key == key) return o.values[static_cast<std::size_t>(o.selected)];
    }
    return {};
}

std::string GetLabel(const std::vector<SettingOption>& opts, std::string_view key) {
    for (const auto& o : opts) {
        if (o.key == key) return o.labels[static_cast<std::size_t>(o.selected)];
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
    cfg->cpu_boost = (GetValue(eff, "cpu_boost") == "on");

    const std::string as_mode = GetValue(eff, "address_space");
    cfg->address_space_mode = (as_mode == "36bit") ? 0 : (as_mode == "39bit" ? 2 : 1);

    cfg->jit_blocks_ahead = std::atoi(GetValue(eff, "jit_ahead").c_str());
    cfg->shader_cache = (GetValue(eff, "shader_cache") == "on");
    cfg->language = GetValue(eff, "language");
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
        } else if (entry.ext == ".xci") {
            bg = PackRGBA(168, 95, 14);
            tag = "XCI";
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

void DrawStickWidget(Canvas& cv, int cx, int cy, int r, float sx, float sy, std::string_view tag) {
    cv.DrawRoundedRect(cx - r, cy - r, r * 2, r * 2, r, PackRGBA(14, 21, 34), kBorderDim);
    const int dot_x = cx + static_cast<int>(std::clamp(sx, -1.0f, 1.0f) * (r - 6));
    const int dot_y = cy - static_cast<int>(std::clamp(sy, -1.0f, 1.0f) * (r - 6));
    cv.DrawRoundedRect(dot_x - 5, dot_y - 5, 10, 10, 5, kAccent);
    cv.DrawText(cx - 4, cy + r + 4, tag, kTextMuted, 1);
}

void DrawMainScreen(Canvas& cv, const std::string& folder, const std::vector<GameEntry>& entries,
                    int selected, int scroll, const std::vector<SettingOption>& global_opts,
                    const Switch2PadSnapshot& pad) {
    cv.FillGradientV(0, 0, kScreenW, kScreenH, kBgTop, kBgBottom);
    // Top bar
    cv.FillRect(0, 0, kScreenW, 68, PackRGBA(15, 23, 38));
    cv.FillRect(0, 66, kScreenW, 2, kAccent);

    cv.DrawPill(24, 16, "EDEN-NX2", kAccent, PackRGBA(7, 22, 16), 2);
    cv.DrawText(192, 18, "Nintendo Switch 2", kTextPrimary, 2, true);
    cv.DrawText(192, 42, "Port by @davidkalil10 (ko-fi.com/davidkalil10)", kTextMuted, 1);

    std::uint64_t mem_used = 0, mem_total = 0;
    Switch2GetMemoryStats(&mem_used, &mem_total);
    char ram_buf[64];
    std::snprintf(ram_buf, sizeof(ram_buf), "RAM %llu / %llu MiB",
                  static_cast<unsigned long long>(mem_used >> 20),
                  static_cast<unsigned long long>(mem_total >> 20));
    cv.DrawPill(740, 18, ram_buf, kCardBg, kCyan, 2);

    char core_buf[32];
    std::snprintf(core_buf, sizeof(core_buf), "CPU 0x%llx",
                  static_cast<unsigned long long>(Switch2GetCoreMask()));
    cv.DrawPill(1075, 18, core_buf, kCardBg, kTextPrimary, 2);

    // Left panel: Library
    cv.DrawRoundedRect(24, 84, 724, 570, 14, kPanelBg, kBorderDim);
    cv.DrawText(42, 98, "BIBLIOTECA DE JOGOS", kAccent, 2);
    cv.DrawTextMaxWidth(365, 102, 360, folder, kTextMuted, 1);
    cv.FillRect(40, 122, 692, 1, kBorderDim);

    constexpr int kVisible = 6;
    if (entries.empty()) {
        cv.DrawRoundedRect(46, 150, 680, 170, 12, kCardBg, kBorderDim);
        cv.DrawText(68, 176, "Nenhuma ROM encontrada nesta pasta.", kTextPrimary, 2);
        cv.DrawText(68, 212, "Coloque arquivos .nsp / .xci / .nro em:", kTextMuted, 1);
        cv.DrawText(68, 232, "sdmc:/switch/eden-nx2/roms/", kAccent, 2);
        cv.DrawText(68, 266, "Keys: sdmc:/switch/eden-nx2/.local/share/eden/keys/", kTextMuted, 1);
        cv.DrawText(68, 286, "Pressione [B] para navegar em outras pastas do cartao SD.", kCyan, 1);
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
                                  ? "Pasta - pressione [A] para abrir"
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
    const bool has_game = !entries.empty() && selected < static_cast<int>(entries.size()) &&
                          !entries[static_cast<std::size_t>(selected)].is_dir;
    const auto eff = has_game ? EffectiveOptions(global_opts, entries[static_cast<std::size_t>(selected)].full_path)
                              : global_opts;

    cv.DrawText(784, 98, has_game ? "PERFIL EFETIVO DO JOGO" : "PERFIL GLOBAL ATIVO", kAccent, 2);
    cv.FillRect(784, 122, 452, 1, kBorderDim);

    if (!entries.empty() && selected < static_cast<int>(entries.size())) {
        cv.DrawTextMaxWidth(784, 134, 452, entries[static_cast<std::size_t>(selected)].display_title,
                            kTextPrimary, 2);
    }

    const std::array<std::pair<const char*, std::string>, 7> rows = {{
        {"Controle", GetLabel(eff, "controller")},
        {"Modo", GetLabel(eff, "console_mode") + "  " + GetValue(eff, "resolution") + "x"},
        {"GPU", GetLabel(eff, "gpu_mode")},
        {"CPU Boost", GetLabel(eff, "cpu_boost")},
        {"Memoria", GetLabel(eff, "address_space")},
        {"JIT Ahead", GetValue(eff, "jit_ahead") + " blocos | Shader: " + GetValue(eff, "shader_cache")},
        {"Idioma", GetValue(eff, "language")},
    }};
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const int ry = 166 + static_cast<int>(i) * 32;
        cv.FillRect(784, ry, 452, 26, (i % 2 == 0) ? PackRGBA(22, 32, 49) : PackRGBA(19, 28, 43));
        cv.DrawText(792, ry + 6, rows[i].first, kTextMuted, 1);
        cv.DrawTextMaxWidth(890, ry + 6, 338, rows[i].second, kTextPrimary, 1);
    }

    // Live controller & Let's Go tip widget
    cv.DrawRoundedRect(784, 404, 452, 232, 10, kCardBg, kBorderDim);
    cv.DrawText(800, 418, "TESTE DE CONTROLE AO VIVO", kCyan, 2);

    DrawStickWidget(cv, 840, 486, 26, pad.left_x, pad.left_y, "L");
    DrawStickWidget(cv, 922, 486, 26, pad.right_x, pad.right_y, "R");

    auto btnColor = [&](std::uint64_t mask) {
        return (pad.raw_held & mask) ? kAccent : PackRGBA(50, 68, 95);
    };
    cv.DrawPill(974, 454, "A", btnColor(SWITCH2_BTN_A), kTextPrimary, 1);
    cv.DrawPill(1010, 454, "B", btnColor(SWITCH2_BTN_B), kTextPrimary, 1);
    cv.DrawPill(1046, 454, "X", btnColor(SWITCH2_BTN_X), kTextPrimary, 1);
    cv.DrawPill(1082, 454, "Y", btnColor(SWITCH2_BTN_Y), kTextPrimary, 1);
    cv.DrawPill(1122, 454, "ZL", btnColor(SWITCH2_BTN_ZL), kTextPrimary, 1);
    cv.DrawPill(1168, 454, "ZR", btnColor(SWITCH2_BTN_ZR), kTextPrimary, 1);

    cv.DrawPill(974, 484, "L", btnColor(SWITCH2_BTN_L), kTextPrimary, 1);
    cv.DrawPill(1010, 484, "R", btnColor(SWITCH2_BTN_R), kTextPrimary, 1);
    cv.DrawPill(1046, 484, "SL", btnColor(SWITCH2_BTN_SL), kTextPrimary, 1);
    cv.DrawPill(1090, 484, "SR", btnColor(SWITCH2_BTN_SR), kTextPrimary, 1);
    cv.DrawText(974, 518, pad.has_motion ? "Gyro/Motion: OK" : "Gyro: Conectando...",
                pad.has_motion ? kAccent : kTextMuted, 1);

    cv.FillRect(800, 548, 420, 1, kBorderDim);
    cv.DrawText(800, 560, "Dica Let's Go Pikachu / Eevee:", kAmber, 1);
    cv.DrawText(800, 578, "Pressione [Y] no jogo e mude Controle", kTextPrimary, 1);
    cv.DrawText(800, 594, "para Handheld ou Joy-Con Direito!", kTextPrimary, 1);
    cv.DrawText(800, 614, "Sair do jogo: segure [+] e [-] por 1 seg.", kTextMuted, 1);

    // Bottom footer hints
    cv.FillRect(0, 666, kScreenW, 54, PackRGBA(12, 18, 29));
    cv.FillRect(0, 666, kScreenW, 1, kBorderDim);
    int fx = 28;
    fx += cv.DrawPill(fx, 678, "[A] Abrir Jogo", kAccentSoft, kAccent, 1) + 14;
    fx += cv.DrawPill(fx, 678, "[B] Pasta Acima", kCardBg, kTextPrimary, 1) + 14;
    fx += cv.DrawPill(fx, 678, "[X] Config Global", kCardBg, kCyan, 1) + 14;
    fx += cv.DrawPill(fx, 678, "[Y] Config do Jogo", kCardBg, kAmber, 1) + 14;
    cv.DrawPill(1136, 678, "[+] Sair", PackRGBA(120, 28, 36), kTextPrimary, 1);
}

void DrawSettingsModal(Canvas& cv, const std::vector<SettingOption>& opts, int selected,
                       const std::string& title) {
    cv.FillRect(0, 0, kScreenW, kScreenH, PackRGBA(4, 8, 14), 205);
    cv.DrawRoundedRect(130, 56, 1020, 608, 16, kPanelBg, kAccent);
    cv.DrawText(164, 80, title, kAccent, 2, true);
    cv.FillRect(164, 108, 952, 2, kBorderDim);

    for (std::size_t i = 0; i < opts.size(); ++i) {
        const int ry = 124 + static_cast<int>(i) * 48;
        const bool is_sel = (static_cast<int>(i) == selected);
        cv.DrawRoundedRect(164, ry, 952, 42, 8, is_sel ? kCardSelected : kCardBg,
                           is_sel ? kAccent : kBorderDim);
        cv.DrawTextMaxWidth(184, ry + 13, 380, opts[i].label, kTextPrimary, 2);
        const std::string val_txt = "< " + opts[i].labels[static_cast<std::size_t>(opts[i].selected)] + " >";
        cv.DrawTextMaxWidth(580, ry + 13, 516, val_txt, is_sel ? kAccent : kCyan, 2);
    }

    if (selected >= 0 && selected < static_cast<int>(opts.size())) {
        cv.DrawRoundedRect(164, 562, 952, 46, 8, PackRGBA(14, 22, 34), kBorderDim);
        cv.DrawTextMaxWidth(182, 578, 916, opts[static_cast<std::size_t>(selected)].help, kTextPrimary, 1);
    }
    cv.DrawText(164, 626, "[Up/Down] Navegar   [Left/Right ou A] Alterar   [B] Salvar e Voltar",
                kTextMuted, 1);
}

bool RunSettingsModal(Canvas& cv, std::vector<SettingOption>& opts, const std::string& title) {
    int selected = 0;
    bool redraw = true;
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
        auto& opt = opts[static_cast<std::size_t>(selected)];
        const int c = static_cast<int>(opt.values.size());
        if (down & (SWITCH2_BTN_LEFT | SWITCH2_BTN_RIGHT | SWITCH2_BTN_A)) {
            const int step = (down & SWITCH2_BTN_LEFT) ? (c - 1) : 1;
            opt.selected = (opt.selected + step) % c;
            redraw = true;
        }
        if (redraw) {
            DrawSettingsModal(cv, opts, selected, title);
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

    auto global_opts = MakeDefaultOptions();
    LoadIniInto(global_opts, kGlobalSettingsPath, false);

    std::string folder = ReadInitialFolder();
    std::vector<GameEntry> entries = ScanDirectory(folder);
    int selected = 0;
    int scroll = 0;
    constexpr int kVisible = 6;
    bool chosen = false;
    bool prev_touch = false;
    int tick_counter = 0;

    Canvas cv;
    Switch2PadSnapshot pad{};

    while (Switch2PollInput(&pad, false)) {
        bool redraw = (tick_counter++ % 6 == 0);
        const std::uint64_t down = pad.raw_down;
        const int count = static_cast<int>(entries.size());

        if (down & SWITCH2_BTN_PLUS) {
            break;
        }

        if (down & SWITCH2_BTN_X) {
            if (!RunSettingsModal(cv, global_opts, "CONFIGURACOES GLOBAIS")) {
                break;
            }
            SaveGlobalIni(global_opts);
            redraw = true;
        }

        if ((down & SWITCH2_BTN_Y) && count > 0 && !entries[static_cast<std::size_t>(selected)].is_dir) {
            const std::string& rpath = entries[static_cast<std::size_t>(selected)].full_path;
            auto game_opts = MakePerGameOptions(global_opts, rpath);
            if (!RunSettingsModal(cv, game_opts, "CONFIG POR JOGO: " + entries[static_cast<std::size_t>(selected)].display_title)) {
                break;
            }
            SavePerGameIni(game_opts, rpath);
            redraw = true;
        }

        if (down & (SWITCH2_BTN_UP | SWITCH2_BTN_DOWN | SWITCH2_BTN_LEFT | SWITCH2_BTN_RIGHT)) {
            if (down & SWITCH2_BTN_UP) --selected;
            if (down & SWITCH2_BTN_DOWN) ++selected;
            if (down & SWITCH2_BTN_LEFT) selected -= kVisible;
            if (down & SWITCH2_BTN_RIGHT) selected += kVisible;
            selected = std::clamp(selected, 0, std::max(0, count - 1));
            redraw = true;
        }

        // Touch screen card tap
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
            const auto eff = EffectiveOptions(global_opts, rpath);
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
            redraw = true;
        }

        if (down != 0 || pad.has_touch || std::fabs(pad.left_x) > 0.12f || std::fabs(pad.left_y) > 0.12f) {
            redraw = true;
        }

        if (redraw) {
            scroll = std::clamp(scroll, std::max(0, selected - kVisible + 1), selected);
            DrawMainScreen(cv, folder, entries, selected, scroll, global_opts, pad);
            cv.Present();
        }
        Switch2SleepMs(16);
    }

    Switch2FbClose();
    return chosen;
}

#endif // __SWITCH2__
