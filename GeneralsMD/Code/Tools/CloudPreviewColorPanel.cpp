// Dependency-free color controls for the standalone cloud preview.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "CloudPreviewColorPanel.h"
#include "CloudPreviewColors.h"

#include "Common/UnicodeString.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "SdlDevice/GameClient/SdlInput.h"

// Legacy engine headers expose these macros; keep them out of the panel's
// standard-library calls without changing shared headers.
#undef min
#undef max

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace CloudPreviewColorPanel {
namespace {

constexpr float Left = 16.f, Top = 16.f, Width = 320.f;
constexpr int SliderCount = 5;
constexpr float FirstTrack = 274.f, SliderStep = 38.f;
constexpr float ActionsTop = FirstTrack + SliderStep * (SliderCount - 1) + 22.f;
constexpr float Height = ActionsTop + 114.f;
constexpr float TrackLeft = 23.f, TrackWidth = 274.f;
constexpr unsigned PaletteOrder[] = {0, 2, 3, 1};
constexpr const char *PaletteLabels[] = {"Toxin", "Toxin beta", "Toxin gamma", "Nuclear"};
constexpr unsigned Ink = 0xffe8eef2, Muted = 0xffa5b5c2, Accent = 0xff87c9d7;

GameWindow *overlay = nullptr;
bool shown = true;
bool shadowSelected = false;
unsigned selection = 0;
int previewRequest = -1;
int activeSlider = -1;
Uint32 ownedButtons = 0;
float scale = 1.f, mouseX = -1.f, mouseY = -1.f;
std::string localStatus;

struct Label {
    DisplayString *display = nullptr;
    std::string value;
    int fontSize = 0;
    bool bold = false;
    int x = 0, y = 0;
    unsigned color = Ink;
};
std::array<Label, 32> labels;
unsigned labelCount = 0;

float clamp(float value, float low = 0.f, float high = 1.f)
{
    return std::max(low, std::min(high, value));
}

unsigned byte(float value)
{
    return static_cast<unsigned>(std::lround(clamp(value) * 255.f));
}

unsigned color(const CloudPreviewColors::Color &value)
{
    return 0xff000000u | (byte(value.r) << 16) | (byte(value.g) << 8) | byte(value.b);
}

bool hit(float x, float y, float left, float top, float width, float height)
{
    return x >= left && x < left + width && y >= top && y < top + height;
}

bool inside(float x, float y)
{
    return hit(x, y, Left, Top, Width, Height);
}

bool over(float x, float y, float width, float height)
{
    return hit(mouseX, mouseY, Left + x, Top + y, width, height);
}

void releaseSlider()
{
    if (activeSlider >= 0) SDL_CaptureMouse(false);
    activeSlider = -1;
}

void refreshScale()
{
    if (!TheDisplay) return;
    scale = std::min(1.f, std::min(TheDisplay->getWidth() / (Width + 32.f),
        TheDisplay->getHeight() / (Height + 32.f)));
    scale = std::max(.1f, scale);
    if (overlay) {
        overlay->winSetPosition(int(std::lround(Left * scale)), int(std::lround(Top * scale)));
        overlay->winSetSize(int(std::lround(Width * scale)), int(std::lround(Height * scale)));
    }
}

void updateMouse(float x, float y)
{
    refreshScale();
    // SDL reports window coordinates; the game draws in backbuffer pixels.
    int windowWidth = 0, windowHeight = 0;
    if (SDL_Window *window = SdlInput_gameWindow())
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    const float pixelX = TheDisplay && windowWidth > 0
        ? float(TheDisplay->getWidth()) / windowWidth : 1.f;
    const float pixelY = TheDisplay && windowHeight > 0
        ? float(TheDisplay->getHeight()) / windowHeight : 1.f;
    mouseX = x * pixelX / scale;
    mouseY = y * pixelY / scale;
}

float trackY(int row)
{
    return Top + FirstTrack + SliderStep * row;
}

void updateSlider(int row)
{
    CloudPreviewColors::Palette palette = CloudPreviewColors::get(selection);
    CloudPreviewColors::Color &selected = shadowSelected ? palette.shadow : palette.main;
    float *component = row == 0 ? &selected.r : row == 1 ? &selected.g
        : row == 2 ? &selected.b : row == 3 ? &palette.brightness : &palette.transparency;
    const float fraction = clamp((mouseX - Left - TrackLeft) / TrackWidth);
    // RGB controls land on shareable bytes; brightness/transparency use 1% steps.
    const float value = row == 3 ? std::round(fraction * 200.f) / 100.f
        : row == 4 ? std::round(fraction * 100.f) / 100.f : std::round(fraction * 255.f) / 255.f;
    if (*component != value) {
        *component = value;
        CloudPreviewColors::set(selection, palette);
        localStatus.clear();
    }
}

void rect(float x, float y, float width, float height, unsigned fill)
{
    if (width <= 0.f || height <= 0.f) return;
    const int left = int(std::lround((Left + x) * scale));
    const int top = int(std::lround((Top + y) * scale));
    const int right = int(std::lround((Left + x + width) * scale));
    const int bottom = int(std::lround((Top + y + height) * scale));
    TheDisplay->drawFillRect(left, top, std::max(1, right - left), std::max(1, bottom - top), fill);
}

void text(float x, float y, const std::string &value, unsigned ink = Ink,
    int pointSize = 11, bool bold = false, bool centered = false, float width = 0.f)
{
    if (labelCount >= labels.size()) return;
    Label &label = labels[labelCount++];
    if (!label.display) label.display = TheDisplayStringManager->newDisplayString();
    if (!label.display) return;
    const int size = std::max(7, int(std::lround(pointSize * scale)));
    if (label.fontSize != size || label.bold != bold) {
        label.display->setFont(TheFontLibrary->getFont(AsciiString("Arial"), size, bold));
        label.fontSize = size;
        label.bold = bold;
    }
    if (label.value != value) {
        UnicodeString translated;
        translated.translate(AsciiString(value.c_str()));
        label.display->setText(translated);
        label.value = value;
    }
    label.x = int(std::lround((Left + x) * scale));
    if (centered) label.x += (int(std::lround(width * scale)) - label.display->getWidth()) / 2;
    label.y = int(std::lround((Top + y) * scale));
    label.color = ink;
}

void button(float x, float y, float width, float height, const char *label,
    bool selected = false, bool primary = false)
{
    const bool hover = over(x, y, width, height);
    rect(x, y, width, height, selected || primary ? 0xff60828f : 0xff455561);
    rect(x + 1, y + 1, width - 2, height - 2,
        selected ? 0xff304a57 : hover ? 0xff354653 : primary ? 0xff283f4a : 0xff242f3a);
    text(x, y + (height - 16.f) * .5f - 1.f, label,
        selected || primary ? 0xffdbf2f5 : Ink, 10, selected, true, width);
}

void draw(GameWindow *, WinInstanceData *)
{
    if (!shown || !TheDisplay || !TheDisplayStringManager || !TheFontLibrary) return;
    refreshScale();
    labelCount = 0;
    // Rectangles share a single engine batch. Text is queued and drawn only once
    // the geometry is flushed, preserving both renderer state and overlay order.
    TheDisplay->beginBatch2D();
    rect(4, 5, Width, Height, 0x66000000);
    rect(0, 0, Width, Height, 0xfa18212a);
    rect(0, 0, Width, 3, Accent);
    rect(0, Height - 1, Width, 1, 0xff455562);
    text(16, 13, "Cloud colors", Ink, 16, true);
    button(244, 12, 60, 25, "Hide  P");
    text(16, 39, "VOLUMETRIC COLORS", Muted, 9);

    for (unsigned index = 0; index < 4; ++index) {
        const float x = 16.f + 148.f * (index % 2);
        const float y = 60.f + 34.f * (index / 2);
        const unsigned palette = PaletteOrder[index];
        button(x, y, 140, 28, PaletteLabels[index], selection == palette);
        rect(x + 9, y + 10, 8, 8, color(CloudPreviewColors::get(palette).main));
    }
    button(16, 132, 288, 28, "SHOW SCENE", false, true);

    const CloudPreviewColors::Palette &palette = CloudPreviewColors::get(selection);
    const CloudPreviewColors::Color &selected = shadowSelected ? palette.shadow : palette.main;
    button(16, 174, 140, 28, "Main", !shadowSelected);
    button(164, 174, 140, 28, "Shadow", shadowSelected);
    rect(133, 182, 14, 12, color(palette.main));
    rect(281, 182, 14, 12, color(palette.shadow));

    const unsigned red = byte(selected.r), green = byte(selected.g), blue = byte(selected.b);
    const unsigned swatchInk = .2126f * red + .7152f * green + .0722f * blue > 145.f
        ? 0xff101820 : 0xfff6f8fc;
    rect(16, 212, 288, 27, color(selected));
    char hex[16], rgb[32];
    std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", red, green, blue);
    std::snprintf(rgb, sizeof(rgb), "RGB %u, %u, %u", red, green, blue);
    text(25, 217, hex, swatchInk, 11, true);
    text(145, 218, rgb, swatchInk, 10);

    const char *names[] = {"Red", "Green", "Blue", "Brightness", "Transparency (more = clearer)"};
    const unsigned accents[] = {0xffdf9394, 0xff9dca8b, 0xff92b7e2, 0xffc9c8a7, Accent};
    const float values[] = {selected.r, selected.g, selected.b, palette.brightness, palette.transparency};
    for (int row = 0; row < SliderCount; ++row) {
        const float fraction = clamp(values[row] / (row == 3 ? 2.f : 1.f));
        const float y = trackY(row) - Top;
        const float knob = TrackLeft + fraction * TrackWidth;
        char amount[16];
        std::snprintf(amount, sizeof(amount), row >= 3 ? "%d%%" : "%d",
            int(std::lround(values[row] * (row >= 3 ? 100.f : 255.f))));
        text(16, y - 23, names[row], Ink, 10);
        text(251, y - 23, amount, accents[row], 10, true, true, 53);
        rect(TrackLeft, y - 3, TrackWidth, 6, 0xff3b4a58);
        rect(TrackLeft, y - 3, fraction * TrackWidth, 6, accents[row]);
        if (activeSlider == row) rect(knob - 7, y - 9, 14, 18, 0x607fc3d3);
        rect(knob - 4, y - 7, 8, 14, 0xffeef3f7);
        rect(knob - 2, y - 4, 4, 8, accents[row]);
    }

    button(16, ActionsTop, 140, 28, "Reset palette");
    button(164, ActionsTop, 140, 28, "Save all", false, true);
    button(16, ActionsTop + 34, 140, 28, "Load saved");
    button(164, ActionsTop + 34, 140, 28, "Copy all");
    const std::string status = localStatus.empty() ? CloudPreviewColors::lastStatus() : localStatus;
    text(16, ActionsTop + 72, status.empty() ? "Drag sliders to change the selected cloud." : status.substr(0, 43),
        0xffb4c9b8, 9);
    text(16, ActionsTop + 94, "P hide panel    Space pause    B replay", Muted, 9);
    TheDisplay->endBatch2D();
    for (unsigned i = 0; i < labelCount; ++i) {
        Label &label = labels[i];
        if (label.display) label.display->draw(label.x, label.y, label.color, 0x00000000);
    }
}

WindowMsgHandledType system(GameWindow *window, UnsignedInt message, WindowMsgData, WindowMsgData)
{
    // A map reset may destroy the overlay before ordinary host teardown.
    if (message == GWM_DESTROY && window == overlay) overlay = nullptr;
    return MSG_IGNORED;
}

} // namespace

bool initialize()
{
    if (overlay) return true;
    if (!TheWindowManager || !TheDisplay || !TheDisplayStringManager || !TheFontLibrary) return false;
    overlay = TheWindowManager->winCreate(nullptr,
        WIN_STATUS_ENABLED | WIN_STATUS_ABOVE | WIN_STATUS_NO_INPUT | WIN_STATUS_NO_FOCUS,
        int(Left), int(Top), int(Width), int(Height), system, nullptr);
    if (!overlay) return false;
    overlay->winSetDrawFunc(draw);
    overlay->winHide(!shown);
    refreshScale();
    return true;
}

void shutdown()
{
    releaseSlider();
    ownedButtons = 0;
    if (overlay && TheWindowManager) TheWindowManager->winDestroy(overlay);
    overlay = nullptr;
    for (Label &label : labels) {
        if (label.display && TheDisplayStringManager)
            TheDisplayStringManager->freeDisplayString(label.display);
        label = {};
    }
    labelCount = 0;
    previewRequest = -1;
}

bool handle(const SDL_Event &event)
{
    if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
        && event.key.key == SDLK_P) {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) setVisible(!shown);
        return true;
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        releaseSlider();
        ownedButtons = 0;
        return false;
    }
    // A button belongs to the surface that handled its press. Check release
    // ownership even while hidden: Hide/P may hide the panel during a click.
    // Likewise, a map drag released over the panel must reach the game.
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        const Uint32 button = event.button.button > 0 && event.button.button <= 32
            ? Uint32(1) << (event.button.button - 1) : 0;
        const bool consumed = (ownedButtons & button) != 0;
        ownedButtons &= ~button;
        if (consumed && event.button.button == SDL_BUTTON_LEFT) releaseSlider();
        return consumed;
    }
    if (!shown) return false;
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        updateMouse(event.motion.x, event.motion.y);
        if (activeSlider >= 0) { updateSlider(activeSlider); return true; }
        if (ownedButtons) return true;
        if (event.motion.state) return false;
        return inside(mouseX, mouseY);
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        updateMouse(event.wheel.mouse_x, event.wheel.mouse_y);
        return inside(mouseX, mouseY);
    }
    if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN) return false;
    updateMouse(event.button.x, event.button.y);
    if (!inside(mouseX, mouseY)) return false;
    if (event.button.button > 0 && event.button.button <= 32)
        ownedButtons |= Uint32(1) << (event.button.button - 1);
    if (event.button.button != SDL_BUTTON_LEFT) return true;
    if (over(244, 12, 60, 25)) { setVisible(false); return true; }
    for (unsigned index = 0; index < 4; ++index) {
        if (over(16.f + 148.f * (index % 2), 60.f + 34.f * (index / 2), 140, 28)) {
            selectPalette(PaletteOrder[index]);
            return true;
        }
    }
    if (over(16, 132, 288, 28)) { previewRequest = int(selection); return true; }
    if (over(16, 174, 140, 28)) { shadowSelected = false; releaseSlider(); return true; }
    if (over(164, 174, 140, 28)) { shadowSelected = true; releaseSlider(); return true; }
    for (int row = 0; row < SliderCount; ++row) {
        if (hit(mouseX, mouseY, Left + 16, trackY(row) - 12, Width - 32, 25)) {
            activeSlider = row;
            SDL_CaptureMouse(true);
            updateSlider(row);
            return true;
        }
    }
    if (over(16, ActionsTop, 140, 28)) {
        CloudPreviewColors::reset(selection);
        localStatus.clear();
    } else if (over(164, ActionsTop, 140, 28)) {
        CloudPreviewColors::save();
        localStatus.clear();
    } else if (over(16, ActionsTop + 34, 140, 28)) {
        CloudPreviewColors::load();
        localStatus.clear();
    } else if (over(164, ActionsTop + 34, 140, 28)) {
        const std::string exported = CloudPreviewColors::exportText();
        localStatus = SDL_SetClipboardText(exported.c_str())
            ? "All colors copied. Paste them into the chat."
            : "Clipboard unavailable. Use Save all instead.";
    }
    return true;
}

bool visible() { return shown; }

void setVisible(bool value)
{
    shown = value;
    releaseSlider();
    if (overlay) overlay->winHide(!shown);
}

unsigned selectedPalette() { return selection; }

void selectPalette(unsigned palette)
{
    if (palette >= 4) return;
    selection = palette;
    releaseSlider();
    localStatus.clear();
}

int takePreviewRequest()
{
    const int requested = previewRequest;
    previewRequest = -1;
    return requested;
}

} // namespace CloudPreviewColorPanel
