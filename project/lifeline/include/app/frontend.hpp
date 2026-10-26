#pragma once
#include "assets/asset_directory.hpp"
#include "assets/flash.hpp"
#include "rendering/image.hpp"
#include "ui/bonus.hpp"
#include "ui/menus.hpp"
#include "ui/title.hpp"
#include <string>
#include <vector>

namespace lifeline {
void show_assets(const std::vector<AssetImage>& images,const std::string& capture="",unsigned initial=0);
void show_movies(const std::vector<FlashMovie>& movies,const std::string& capture="",unsigned frame=0,
    const FlashMovie* cursor=nullptr,const TitleNavigation* navigation=nullptr,const OptionsAssets* options=nullptr,
    const SaveMenuAssets* saves=nullptr,const BonusAssets* bonus=nullptr,const AssetDirectory* data=nullptr);
void show_tutorial_intro(const AssetDirectory&,const std::string& capture="",unsigned page=0);
void show_cinematic(const AssetDirectory&,const std::string& name,const std::string& capture,unsigned frame);
}
