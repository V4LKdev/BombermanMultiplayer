/** @file MultiplayerLevelScene.Presentation.cpp
 *  @brief Local presentation and match banner logic.
 *  @ingroup multiplayer_level_scene
 */

#include "Scenes/MultiplayerLevelScene/MultiplayerLevelSceneInternal.h"

#include <cassert>
#include <cmath>
#include <memory>
#include <unordered_set>

#include <SDL.h>

#include "Entities/Player.h"
#include "Entities/Text.h"
#include "Game.h"
#include "Net/NetClient.h"
#include "Util/Log.h"

namespace bomberman
{
    using namespace multiplayer_level_scene_internal;

    void MultiplayerLevelScene::showCenterBanner(const std::string_view message, const SDL_Color color)
    {
        showCenterBanner(message, {}, color);
    }

    void MultiplayerLevelScene::showCenterBanner(const std::string_view mainMessage,
                                                 const std::string_view detailMessage,
                                                 const SDL_Color color)
    {
        if (!centerBannerText_)
        {
            auto font = game->getAssetManager()->getFont(kCenterBannerPointSize);
            centerBannerText_ = std::make_shared<Text>(font, game->getRenderer(), std::string(mainMessage));
            centerBannerText_->attachToCamera(false);
            addObject(centerBannerText_);
        }
        else
        {
            centerBannerText_->setText(std::string(mainMessage));
        }

        centerBannerText_->fitToContent();
        centerBannerText_->setColor(color);

        if (!detailMessage.empty())
        {
            if (!centerBannerDetailText_)
            {
                auto detailFont = game->getAssetManager()->getFont(kCenterBannerDetailPointSize);
                centerBannerDetailText_ =
                    std::make_shared<Text>(detailFont, game->getRenderer(), std::string(detailMessage));
                centerBannerDetailText_->attachToCamera(false);
                addObject(centerBannerDetailText_);
            }
            else
            {
                centerBannerDetailText_->setText(std::string(detailMessage));
            }

            centerBannerDetailText_->fitToContent();
            centerBannerDetailText_->setColor(color);
        }
        else if (centerBannerDetailText_)
        {
            removeObject(centerBannerDetailText_);
            centerBannerDetailText_.reset();
        }

        const int totalHeight = centerBannerDetailText_
            ? centerBannerDetailText_->getHeight() + kCenterBannerLineGapPx + centerBannerText_->getHeight()
            : centerBannerText_->getHeight();
        const int topY = game->getWindowHeight() / 2 - totalHeight / 2;

        if (centerBannerDetailText_)
        {
            centerBannerDetailText_->setPosition(
                game->getWindowWidth() / 2 - centerBannerDetailText_->getWidth() / 2,
                topY);
            centerBannerText_->setPosition(
                game->getWindowWidth() / 2 - centerBannerText_->getWidth() / 2,
                topY + centerBannerDetailText_->getHeight() + kCenterBannerLineGapPx);
            return;
        }

        centerBannerText_->setPosition(game->getWindowWidth() / 2 - centerBannerText_->getWidth() / 2,
                                       game->getWindowHeight() / 2 - centerBannerText_->getHeight() / 2);
    }

    void MultiplayerLevelScene::hideCenterBanner()
    {
        if (centerBannerText_)
        {
            removeObject(centerBannerText_);
            centerBannerText_.reset();
        }

        if (centerBannerDetailText_)
        {
            removeObject(centerBannerDetailText_);
            centerBannerDetailText_.reset();
        }
    }

    void MultiplayerLevelScene::ensureLocalPresentation(const uint8_t localId)
    {
        if (!player)
            return;

        if (!localPlayerId_.has_value() || localPlayerId_.value() != localId)
        {
            localPlayerId_ = localId;

            const util::PlayerColor color = util::colorForPlayerId(localId);
            player->setColorMod(color.r, color.g, color.b);

            if (localPlayerTag_)
            {
                localPlayerTag_->setText(formatPlayerTag(localId));
                localPlayerTag_->fitToContent();
                localPlayerTag_->setColor(SDL_Color{color.r, color.g, color.b, 0xFF});
            }
        }

        if (!localPlayerTag_)
        {
            const int pointSize = computeTagPointSize(scaledTileSize);
            auto font = game->getAssetManager()->getFont(pointSize);
            localPlayerTag_ = std::make_shared<Text>(font, game->getRenderer(), formatPlayerTag(localId));
            localPlayerTag_->fitToContent();

            const util::PlayerColor color = util::colorForPlayerId(localId);
            localPlayerTag_->setColor(SDL_Color{color.r, color.g, color.b, 0xFF});

            addObject(localPlayerTag_);
        }
    }

    void MultiplayerLevelScene::updateLocalPlayerTagPosition()
    {
        if (!player || !localPlayerTag_ || !localPlayerAlive_)
            return;

        const int tagX = player->getPositionX() + player->getWidth() / 2 - localPlayerTag_->getWidth() / 2;
        const int tagY = player->getPositionY() - localPlayerTag_->getHeight() - kNameTagOffsetPx;
        localPlayerTag_->setPosition(tagX, tagY);
    }

} // namespace bomberman
