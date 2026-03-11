#ifndef _BOMBERMAN_ENTITIES_PLAYER_H_
#define _BOMBERMAN_ENTITIES_PLAYER_H_

#include "Entities/Creature.h"

#include "Core/Animation.h"

namespace bomberman
{
    /**
     * @brief Enumeration of movement directions
     */
    enum class MovementDirection
    {
        None,
        Up,
        Down,
        Left,
        Right
    };
    /**
     * @brief Player class
     *
     * Responsible for animation and sprite direction only.
     */
    class Player : public Creature
    {
      public:
        /**
         * @brief Create player
         */
        Player(std::shared_ptr<SDL_Texture> texture, SDL_Renderer* renderer);
        /**
         * @brief Set movement direction of player.
         *
         * Updates the moving flag, animation playback, and horizontal flip.
         *
         * @param direction - movement direction
         */
        void setMovementDirection(MovementDirection direction);
        /**
         * @brief Tick animations.
         *
         * Advances all attached animations by `delta` milliseconds.
         *
         * @param delta - time in milliseconds
         */
        virtual void update(const unsigned int delta) override;

      private:
        MovementDirection movementDirection = MovementDirection::None; // movement direction
        std::shared_ptr<Animation> movement;                           // movement animation
    };
} // namespace bomberman

#endif // _BOMBERMAN_ENTITIES_PLAYER_H_