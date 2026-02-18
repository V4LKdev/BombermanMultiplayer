#include "Entities/Creature.h"

namespace bomberman
{
    void Creature::revertLastMove()
    {
        setPositionF(getPositionXF() - prevPosDeltaX, getPositionYF() - prevPosDeltaY);
    }

    void Creature::setMoving(bool _moving)
    {
        this->moving = _moving;
    }

    bool Creature::isMoving() const
    {
        return moving;
    }
} // namespace bomberman
