#include "Entities/Object.h"
#include <cmath>

namespace bomberman
{
    Object::Object(SDL_Renderer* _renderer)
    {
        this->renderer = _renderer;
        rect = {0,0,0,0};
        clip = {0,0,0,0};
        positionX = 0.0f;
        positionY = 0.0f;
    }

    Object::~Object() {}

    void Object::setSize(const int width, const int height)
    {
        rect.w = width;  // controls the width of the rect
        rect.h = height; // controls the height of the rect
    }

    void Object::setPosition(const int x, const int y)
    {
        setPositionF(static_cast<float>(x), static_cast<float>(y));
    }

    void Object::setPositionF(const float x, const float y)
    {
        positionX = x;
        positionY = y;
        rect.x = static_cast<int>(std::round(positionX));
        rect.y = static_cast<int>(std::round(positionY));
    }

    void Object::setClip(const int width, const int height, const int x, const int y)
    {
        clip.w = width;  // controls the width of the rect
        clip.h = height; // controls the height of the rect
        clip.x = x;      // controls the rect's x coordinate
        clip.y = y;      // controls the rect's y coordinate
    }

    void Object::attachToCamera(bool isAttached /* = true*/)
    {
        this->isAttachedToCamera = isAttached;
    }

    int Object::getWidth() const
    {
        return rect.w;
    }

    int Object::getHeight() const
    {
        return rect.h;
    }

    int Object::getPositionX() const
    {
        return rect.x;
    }

    int Object::getPositionY() const
    {
        return rect.y;
    }

    float Object::getPositionXF() const
    {
        return positionX;
    }

    float Object::getPositionYF() const
    {
        return positionY;
    }

    const SDL_Rect& Object::getRect() const
    {
        return rect;
    }

    SDL_FRect Object::getRectF() const
    {
        SDL_FRect rectF;
        rectF.x = positionX;
        rectF.y = positionY;
        rectF.w = static_cast<float>(rect.w);
        rectF.h = static_cast<float>(rect.h);
        return rectF;
    }

    void Object::setFlip(SDL_RendererFlip flip)
    {
        flipping = flip;
    }

    void Object::setColorMod(const Uint8 r, const Uint8 g, const Uint8 b)
    {
        colorModR = r;
        colorModG = g;
        colorModB = b;
    }

    void Object::update(const unsigned int /*delta*/) {}

    void Object::draw(const SDL_Rect& camera) const
    {
        if(renderer != nullptr && texture != nullptr)
        {
            // change position according to camera
            SDL_Rect destrinationRect = rect;
            if(isAttachedToCamera)
            {
                destrinationRect.x -= camera.x;
                destrinationRect.y -= camera.y;
            }

            SDL_SetTextureColorMod(texture.get(), colorModR, colorModG, colorModB);
            // draw on the screen
            SDL_RenderCopyEx(renderer, texture.get(), &clip, &destrinationRect, 0, nullptr, flipping);
            SDL_SetTextureColorMod(texture.get(), 0xFF, 0xFF, 0xFF);
        }
    }
} // namespace bomberman
