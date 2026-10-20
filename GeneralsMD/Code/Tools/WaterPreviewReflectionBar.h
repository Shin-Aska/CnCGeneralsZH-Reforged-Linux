// Reflection selector for the standalone water preview: legacy planar, screen-space trace,
// the game's hybrid of the two, or all three side by side.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include "WaterPreviewColorPanel.h"
#include <cstring>

namespace WaterPreviewReflection {

enum Choice { Mirror, ScreenSpace, Hybrid, Compare, ChoiceCount };

inline const char *Label(int choice, bool banked = false)
{
    if (banked && choice == Mirror) return "CAPTURED PLANAR";
    static const char *labels[ChoiceCount] = {"LEGACY", "SSR", "HYBRID", "COMPARE ALL"};
    return choice >= 0 && choice < ChoiceCount ? labels[choice] : "";
}

class Bar {
public:
    int choice = Hybrid;
    bool changed = false;

    bool handle(const SDL_Event &event, unsigned width)
    {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            const SDL_Keycode keys[ChoiceCount] = {SDLK_4, SDLK_5, SDLK_6, SDLK_7};
            for (int i = 0; i < ChoiceCount; ++i)
                if (event.key.key == keys[i]) { select(i); return true; }
            return false;
        }
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            mouseX = event.motion.x; mouseY = event.motion.y;
            return over(width) >= 0;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            mouseX = event.button.x; mouseY = event.button.y;
            const int button = over(width);
            if (button < 0) return false;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) select(button);
            return true;
        }
        return false;
    }

    // Column labels name the three viewports of the comparison, left to right.
    void draw(PosixDevice9 *device, unsigned width, unsigned height, unsigned columns, bool banked = false)
    {
        if (!device || !width || !height) return;
        vertices.clear();
        const float left = barLeft(width), size = buttonScale(width);
        rect(left - 6, Top - 6, ChoiceCount * (ButtonWidth + Gap) * size - Gap * size + 12, ButtonHeight * size + 12, 0xc0101418);
        for (int i = 0; i < ChoiceCount; ++i) {
            const float x = left + i * (ButtonWidth + Gap) * size;
            const bool active = i == choice, hover = over(width) == i;
            rect(x, Top, ButtonWidth * size, ButtonHeight * size, active ? 0xff95ba79 : 0xff56636d);
            rect(x + 1, Top + 1, ButtonWidth * size - 2, ButtonHeight * size - 2,
                active ? (hover ? 0xff466043 : 0xff354b34) : (hover ? 0xff3e4a55 : 0xff2d3741));
            char key[2] = {char('4' + i), 0};
            text(x + 7 * size, Top + 11 * size, key, 1.f * size, 0xff879ba8);
            const float labelSize = banked && i == Mirror ? 1.25f : 1.5f;
            const float labelWidth = std::strlen(Label(i,banked)) * 6 * labelSize * size;
            text(x + (ButtonWidth * size - labelWidth) * .5f + 4 * size, Top + 10 * size, Label(i,banked), labelSize * size,
                active ? 0xffeaf0e6 : 0xffc3ced5);
        }
        if (columns == 3) {
            const unsigned column = width / 3;
            for (unsigned i = 0; i < 3; ++i) {
                const float labelWidth = std::strlen(Label(int(i),banked)) * 6 * 2.f;
                const float x = column * i + (column - labelWidth) * .5f, y = Top + ButtonHeight * size + 22;
                rect(x - 8, y - 6, labelWidth + 16, 26, 0xc0101418);
                text(x, y, Label(int(i),banked), 2.f, 0xffeaf0e6);
                if (i) rect(float(column * i) - 1, 0, 2, float(height), 0xc0101418);
            }
        }
        device->SetPixelShader(nullptr);
        device->SetVertexShader(nullptr);
        device->SetRenderState(D3DRS_LIGHTING, 0);
        device->SetRenderState(D3DRS_FOGENABLE, 0);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        device->SetRenderState(D3DRS_ZWRITEENABLE, 0);
        device->SetRenderState(D3DRS_ALPHATESTENABLE, 0);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, 0);
        device->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
        for (unsigned stage = 0; stage < 16; ++stage) device->SetTexture(stage, nullptr);
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<unsigned>(vertices.size() / 3), vertices.data(), sizeof(Vertex));
    }

private:
    struct Vertex { float x, y, z, rhw; uint32_t color; };
    static constexpr float Top = 14, ButtonWidth = 150, ButtonHeight = 30, Gap = 8;
    float mouseX = -1, mouseY = -1;
    std::vector<Vertex> vertices;

    void select(int next) { if (next != choice) { choice = next; changed = true; } }
    // Centred, beside the color panel on wide windows; shrunk to fit narrow ones.
    static float buttonScale(unsigned width)
    { return std::min(1.f, float(width) / (ChoiceCount * (ButtonWidth + Gap) + 32)); }
    static float barLeft(unsigned width)
    { return (float(width) - (ChoiceCount * (ButtonWidth + Gap) - Gap) * buttonScale(width)) * .5f; }
    int over(unsigned width) const
    {
        const float left = barLeft(width), size = buttonScale(width);
        if (mouseY < Top || mouseY >= Top + ButtonHeight * size) return -1;
        for (int i = 0; i < ChoiceCount; ++i) {
            const float x = left + i * (ButtonWidth + Gap) * size;
            if (mouseX >= x && mouseX < x + ButtonWidth * size) return i;
        }
        return -1;
    }
    void rect(float x, float y, float w, float h, uint32_t color)
    {
        if (w <= 0 || h <= 0) return;
        const Vertex a = {x, y, 0.f, 1.f, color}, b = {x + w, y, 0.f, 1.f, color};
        const Vertex c = {x + w, y + h, 0.f, 1.f, color}, d = {x, y + h, 0.f, 1.f, color};
        vertices.insert(vertices.end(), {a, b, c, a, c, d});
    }
    void text(float x, float y, const char *label, float size, uint32_t color)
    {
        for (; *label; ++label, x += 6 * size) {
            const auto rows = WaterPreviewColor::Panel::glyph(*label);
            for (unsigned row = 0; row < rows.size(); ++row)
                for (unsigned column = 0; column < 5; ++column)
                    if (rows[row] & (1u << (4 - column)))
                        rect(x + column * size, y + row * size, size, size, color);
        }
    }
};

} // namespace WaterPreviewReflection
