// The synthetic preview's ground-target camera, with the game's mouse and key gestures.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace WaterPreviewCamera {

constexpr float TargetLimit = 700.0f;

struct State {
    float x = 0, y = 0;
    float yaw = .10f, pitch = .82f, distance = 600;
};

class Camera {
public:
    State state;
    explicit Camera(State start) : state(start), start(start) {}

    void cancelDrag() { rightArmed=false; panning=false; middleDown=false; }

    // A drag that began in the view keeps its motion and release when it crosses a panel.
    bool captures(const SDL_Event &event) const
    {
        if (event.type==SDL_EVENT_MOUSE_MOTION) return rightArmed || middleDown;
        return event.type==SDL_EVENT_MOUSE_BUTTON_UP
            && ((event.button.button==SDL_BUTTON_RIGHT && rightArmed)
                || (event.button.button==SDL_BUTTON_MIDDLE && middleDown));
    }

    bool handle(const SDL_Event &event)
    {
        if (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (event.button.button==SDL_BUTTON_RIGHT) {
                rightArmed=true; panning=false;
                rightX=mouseX=event.button.x; rightY=mouseY=event.button.y;
                return true;
            }
            if (event.button.button==SDL_BUTTON_MIDDLE) {
                middleDown=true; middleMoved=false;
                pitching=(SDL_GetModState() & SDL_KMOD_ALT)!=0;
                middleX=lastX=event.button.x; middleY=lastY=event.button.y;
                return true;
            }
        } else if (event.type==SDL_EVENT_MOUSE_BUTTON_UP) {
            if (event.button.button==SDL_BUTTON_RIGHT) {
                rightArmed=false; panning=false;
                return true;
            }
            if (event.button.button==SDL_BUTTON_MIDDLE && middleDown) {
                middleMotion(event.button.x,event.button.y);
                if (!middleMoved) resetOrientation();
                middleDown=false;
                return true;
            }
        } else if (event.type==SDL_EVENT_MOUSE_MOTION && (rightArmed || middleDown)) {
            mouseX=event.motion.x; mouseY=event.motion.y;
            // A press only arms the pan. Once it leaves the tolerance, its original anchor
            // pulls the camera continuously, even when the cursor itself has stopped moving.
            if (rightArmed && (std::abs(mouseX-rightX)>4 || std::abs(mouseY-rightY)>4)) panning=true;
            if (middleDown) middleMotion(mouseX,mouseY);
            return true;
        } else if (event.type==SDL_EVENT_MOUSE_WHEEL) {
            const float spin=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event.wheel.y:event.wheel.y;
            const SDL_Keymod mod=SDL_GetModState();
            // Here pitch is the elevation above the ground; the game's pitch is an offset
            // subtracted from that elevation, so its tilt signs are reversed here.
            if (mod & SDL_KMOD_ALT) state.pitch-=spin*(2.f*3.14159265358979323846f/180.f);
            else state.distance*=std::exp(-spin*.09f*((mod & SDL_KMOD_CTRL)?5.f:1.f));
            clamp();
            return true;
        } else if (event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat
            && (event.key.key==SDLK_HOME || event.key.key==SDLK_R)) {
            state=start;
            cancelDrag();
            return true;
        }
        return false;
    }

    void update(float dt, const bool *keys)
    {
        float horizontal=float(keys[SDL_SCANCODE_RIGHT])-float(keys[SDL_SCANCODE_LEFT]);
        float vertical=float(keys[SDL_SCANCODE_DOWN])-float(keys[SDL_SCANCODE_UP]);
        const float length=std::max(1.f,std::hypot(horizontal,vertical));
        horizontal=horizontal/length*.8f; vertical=vertical/length*.8f;
        if (panning) {
            horizontal+=(mouseX-rightX)*.008f;
            vertical+=(mouseY-rightY)*.008f;
        }
        // Right and forward follow the heading on the ground. Scaling by zoom keeps the
        // screen crossing at the same pace when the camera stands farther away.
        const float speed=state.distance*dt, sine=std::sin(state.yaw), cosine=std::cos(state.yaw);
        state.x+=(cosine*horizontal+sine*vertical)*speed;
        state.y+=(sine*horizontal-cosine*vertical)*speed;
        if (keys[SDL_SCANCODE_EQUALS] || keys[SDL_SCANCODE_KP_PLUS]) state.distance*=std::exp(-dt*.7f);
        if (keys[SDL_SCANCODE_MINUS] || keys[SDL_SCANCODE_KP_MINUS]) state.distance*=std::exp(dt*.7f);
        clamp();
    }

    void clamp()
    {
        state.x=std::clamp(state.x,-TargetLimit,TargetLimit);
        state.y=std::clamp(state.y,-TargetLimit,TargetLimit);
        state.pitch=std::clamp(state.pitch,.48f,1.38f);
        state.distance=std::clamp(state.distance,180.f,1050.f);
    }

private:
    State start;
    bool rightArmed=false, panning=false, middleDown=false, middleMoved=false, pitching=false;
    float rightX=0, rightY=0, mouseX=0, mouseY=0;
    float middleX=0, middleY=0, lastX=0, lastY=0;

    void resetOrientation()
    {
        state.yaw=start.yaw; state.pitch=start.pitch; state.distance=start.distance;
    }

    void middleMotion(float x, float y)
    {
        if (std::abs(x-middleX)>5 || std::abs(y-middleY)>5) middleMoved=true;
        if (pitching) state.pitch+=(y-lastY)*.01f;
        else state.yaw+=(x-lastX)*.01f;
        lastX=x; lastY=y;
        clamp();
    }
};

} // namespace WaterPreviewCamera
