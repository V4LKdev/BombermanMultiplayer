#ifndef _BOMBERMAN_ENTITIES_CREATURE_H_
#define _BOMBERMAN_ENTITIES_CREATURE_H_

#include "Entities/Sprite.h"

namespace bomberman
{
    /**
     * @brief Creature class
     *
     */
    class Creature : public Sprite
    {
      public:
        /**
         * @brief Create creature
         */
        using Sprite::Sprite;
        /**
         * @brief Revert last movement of creature
         *
         */
        void revertLastMove();
        /**
         * @brief Set creature to move
         *
         * @param isMoving - moving
         */
        void setMoving(bool isMoving);
        /**
         * @brief Is creature moving at this moment
         *
         * @return true - if moving
         * @return false - if idle
         */
        bool isMoving() const;

      protected:
        // for movement revert
        float prevPosDeltaX = 0.0f;
        float prevPosDeltaY = 0.0f;

      private:
        bool moving = false; // movement status
    };
} // namespace bomberman

#endif // _BOMBERMAN_ENTITIES_CREATURE_H_
