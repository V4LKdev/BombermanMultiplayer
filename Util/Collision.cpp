#include "Util/Collision.h"

#include <algorithm>

namespace bomberman::collision
{
    bool intersects(const SDL_FRect& a, const SDL_FRect& b)
    {
        if(a.x + a.w <= b.x)
            return false;
        if(a.x >= b.x + b.w)
            return false;
        if(a.y + a.h <= b.y)
            return false;
        if(a.y >= b.y + b.h)
            return false;
        return true;
    }

    SDL_FRect scaleCentered(const SDL_FRect& rect, float scale)
    {
        SDL_FRect result = rect;
        scale = std::clamp(scale, 0.01f, 1.0f);
        const float newW = rect.w * scale;
        const float newH = rect.h * scale;
        const float centerX = rect.x + rect.w * 0.5f;
        const float centerY = rect.y + rect.h * 0.5f;
        result.w = newW;
        result.h = newH;
        result.x = centerX - newW * 0.5f;
        result.y = centerY - newH * 0.5f;
        return result;
    }
} // namespace bomberman::collision
