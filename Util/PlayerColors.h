#ifndef BOMBERMAN_UTIL_PLAYERCOLORS_H
#define BOMBERMAN_UTIL_PLAYERCOLORS_H

#include <array>
#include <cstddef>
#include <cstdint>

/**
 * @brief Small shared utility helpers that do not belong to the networking, simulation, or scene subsystems.
 */
namespace bomberman::util
{
    struct PlayerColor
    {
        uint8_t r = 0xFF;
        uint8_t g = 0xFF;
        uint8_t b = 0xFF;
    };

    constexpr std::array<PlayerColor, 8> kPlayerColors = {{
        {0xFF, 0x22, 0x22},
        {0x22, 0xFF, 0x22},
        {0x22, 0x88, 0xFF},
        {0xFF, 0xFF, 0x22},
        {0xFF, 0x88, 0x22},
        {0xD4, 0x4D, 0xFF},
        {0x22, 0xFF, 0xD5},
        {0xFF, 0x66, 0xB3},
    }};

    constexpr std::size_t kPlayerColorCount = kPlayerColors.size();

    [[nodiscard]]
    constexpr PlayerColor colorForPlayerId(const uint8_t playerId)
    {
        return kPlayerColors[static_cast<std::size_t>(playerId) % kPlayerColorCount];
    }
} // namespace bomberman::util

#endif // BOMBERMAN_UTIL_PLAYERCOLORS_H
