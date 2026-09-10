#include "sources/text_source.hpp"

// Windows headers for GDI text rendering
#include <windows.h>
#include <iostream>

namespace securestream {

TextSource::TextSource(std::string name, std::string text,
                       uint32_t width, uint32_t height)
    : Source(std::move(name), SourceOrigin::Generated)
    , text_(std::move(text))
    , width_(width)
    , height_(height) {}

void TextSource::set_text(std::string text) {
    text_ = std::move(text);
    dirty_ = true;
}

void TextSource::render_text() {
    // Use GDI to render text onto a bitmap, then convert to BGRA8.
    // This is simple and reliable. Later we can upgrade to DirectWrite
    // for better quality (anti-aliasing, custom fonts, etc).

    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);

    // Create a 32-bit DIB section for direct pixel access
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = static_cast<LONG>(width_);
    bi.bmiHeader.biHeight = -static_cast<LONG>(height_);  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HBITMAP hOldBitmap = static_cast<HBITMAP>(SelectObject(memDC, hBitmap));

    // Clear to transparent
    uint32_t* pixels = static_cast<uint32_t*>(pBits);
    size_t pixelCount = static_cast<size_t>(width_) * height_;
    for (size_t i = 0; i < pixelCount; ++i) {
        pixels[i] = 0;  // fully transparent
    }

    // Set up text rendering
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));  // white text

    // Create a font (lower-third style: bold, large)
    int fontSize = static_cast<int>(height_ * 0.05);  // 5% of height
    HFONT hFont = CreateFontW(
        fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
    HFONT hOldFont = static_cast<HFONT>(SelectObject(memDC, hFont));

    // Draw a semi-transparent background bar (lower-third)
    uint32_t barY = height_ * 2 / 3;
    uint32_t barH = height_ / 6;
    RECT barRect = {0, static_cast<LONG>(barY),
                    static_cast<LONG>(width_), static_cast<LONG>(barY + barH)};

    // Draw the bar manually with alpha
    for (uint32_t y = barY; y < barY + barH && y < height_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            size_t idx = y * width_ + x;
            // BGRA format in the DIB: set to dark with 70% opacity
            pixels[idx] = (180 << 24) | (20 << 16) | (20 << 8) | 20;
        }
    }

    // Convert text to wide string for GDI
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text_.c_str(),
                                    static_cast<int>(text_.length()), nullptr, 0);
    std::wstring wtext(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text_.c_str(),
                        static_cast<int>(text_.length()), wtext.data(), wlen);

    // Draw text on the bar
    RECT textRect = {20, static_cast<LONG>(barY + 10),
                     static_cast<LONG>(width_ - 20),
                     static_cast<LONG>(barY + barH - 10)};
    DrawTextW(memDC, wtext.c_str(), static_cast<int>(wtext.length()),
              &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Copy the rendered pixels to our Frame
    // GDI gives us BGRA in the DIB (top-down), which is exactly our Frame format
    cached_frame_.allocate(width_, height_);
    std::memcpy(cached_frame_.data.data(), pBits,
                static_cast<size_t>(width_) * height_ * 4);

    // Cleanup
    SelectObject(memDC, hOldFont);
    DeleteObject(hFont);
    SelectObject(memDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    dirty_ = false;
}

Frame TextSource::get_video_frame() {
    if (dirty_) render_text();
    return cached_frame_;
}

} // namespace securestream
