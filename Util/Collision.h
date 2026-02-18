#ifndef _BOMBERMAN_UTIL_COLLISION_H_
#define _BOMBERMAN_UTIL_COLLISION_H_

#include <SDL.h>

namespace bomberman::collision
{
    // Axis-aligned bounding box overlap (top-left origin, width/height)
    bool intersects(const SDL_FRect& a, const SDL_FRect& b);

    // Scale a rect around its center (scale in range (0, 1] for shrinking)
    SDL_FRect scaleCentered(const SDL_FRect& rect, float scale);
} // namespace bomberman::collision

#endif // _BOMBERMAN_UTIL_COLLISION_H_
