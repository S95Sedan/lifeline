#include "ui/script_display.hpp"
#include "script/interface.hpp"
#include "game/effects/effects.hpp"
#include "ui/combat_panel.hpp"
#include "ui/control_panel.hpp"
#include "ui/inventory_panel.hpp"
#include "ui/world_overlay.hpp"
#include "ui/geometry_batch.hpp"
#include <SDL3/SDL.h>

namespace lifeline {
namespace {
void check(bool value){if(!value)throw std::runtime_error(std::string("Script interface renderer: ")+SDL_GetError());}
using Texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>;
}
struct ScriptInterfaceDisplay::Impl {
    struct Slot {std::uint64_t generation=0;std::map<unsigned,Texture> textures;};
    SDL_Renderer* renderer;
    const script::Interface& interface;
    std::array<Slot,12> slots;
    std::array<Slot,7> combat;
    std::uint64_t combat_generation=0;
    Slot control;
    std::array<std::array<Slot,3>,2> inventory;
    std::map<std::shared_ptr<const script::Effects::Display>,Slot> loading;
    Impl(SDL_Renderer* renderer,const script::Interface& interface):renderer(renderer),interface(interface){}
    void render(ScriptInterfaceDisplay::Layer layer){
        for(unsigned i=0;i<slots.size();++i){auto& slot=slots[i];const auto* instance=interface.instance(i);
            if(!instance){slot.textures.clear();slot.generation=0;continue;}
            if(slot.generation==instance->generation)continue;
            slot.textures.clear();
            for(const auto& [id,image]:instance->movie.textures){
                Texture texture(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,int(image.width),int(image.height)),SDL_DestroyTexture);check(bool(texture));
                check(SDL_UpdateTexture(texture.get(),nullptr,image.rgba.data(),int(image.width*4)));
                check(SDL_SetTextureBlendMode(texture.get(),SDL_BLENDMODE_BLEND));
                check(SDL_SetTextureScaleMode(texture.get(),SDL_SCALEMODE_LINEAR));
                slot.textures.emplace(id,std::move(texture));
            }
            slot.generation=instance->generation;
        }
        const auto band=layer==ScriptInterfaceDisplay::Layer::background?script::Interface::Layer::background:layer==ScriptInterfaceDisplay::Layer::foreground?script::Interface::Layer::foreground:script::Interface::Layer::all;
        GeometryBatch geometry(renderer);
        for(const auto& command:interface.compose(band)){
            const auto& draw=command.geometry;geometry.append(draw.textured?slots[command.slot].textures.at(draw.texture).get():nullptr,draw);
        }
        geometry.flush();
    }
};
ScriptInterfaceDisplay::ScriptInterfaceDisplay(SDL_Renderer* renderer,const script::Interface& interface):impl_(std::make_unique<Impl>(renderer,interface)){}
ScriptInterfaceDisplay::~ScriptInterfaceDisplay()=default;
void ScriptInterfaceDisplay::render(Layer layer){impl_->render(layer);}
void ScriptInterfaceDisplay::render_effects(const script::Effects& effects){
    const auto procedural=effects.procedural_displays(true);
    if(!procedural.empty()){
        // Globe lines are already projected beyond 640x448. Draw their existing
        // geometry into the full target instead of clipping at the HUD viewport.
        auto* renderer=impl_->renderer;int logical_w=0,logical_h=0,width=0,height=0;SDL_RendererLogicalPresentation mode;
        check(SDL_GetRenderLogicalPresentation(renderer,&logical_w,&logical_h,&mode));
        if(auto* target=SDL_GetRenderTarget(renderer)){float w=0,h=0;check(SDL_GetTextureSize(target,&w,&h));width=int(w);height=int(h);}
        else check(SDL_GetRenderOutputSize(renderer,&width,&height));
        const float scale=std::min(float(width)/640,float(height)/448),ox=(width-640*scale)*.5f,oy=(height-448*scale)*.5f;
        check(SDL_SetRenderLogicalPresentation(renderer,width,height,SDL_LOGICAL_PRESENTATION_DISABLED));
        for(const auto& draw:procedural){std::vector<SDL_Vertex> vertices;for(const auto& v:draw.vertices)vertices.push_back({{v.x*scale+ox,v.y*scale+oy},{v.color.x,v.color.y,v.color.z,v.color.w},{v.u,v.v}});check(SDL_RenderGeometry(renderer,nullptr,vertices.data(),int(vertices.size()),draw.indices.data(),int(draw.indices.size())));}
        check(SDL_SetRenderLogicalPresentation(renderer,logical_w,logical_h,mode));
    }
    const auto displays=effects.displays();
    std::erase_if(impl_->loading,[&](const auto& item){return std::find(displays.begin(),displays.end(),item.first)==displays.end();});
    for(const auto& display:displays){auto [it,created]=impl_->loading.try_emplace(display);auto& slot=it->second;
        if(created)for(const auto& [id,image]:display->movie.textures){
            Texture texture(SDL_CreateTexture(impl_->renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,int(image.width),int(image.height)),SDL_DestroyTexture);check(bool(texture));
            check(SDL_UpdateTexture(texture.get(),nullptr,image.rgba.data(),int(image.width*4)));check(SDL_SetTextureBlendMode(texture.get(),SDL_BLENDMODE_BLEND));check(SDL_SetTextureScaleMode(texture.get(),SDL_SCALEMODE_LINEAR));slot.textures.emplace(id,std::move(texture));
        }
        for(const auto& draw:display->compose()){std::vector<SDL_Vertex> vertices;
            for(const auto& v:draw.vertices)vertices.push_back({{v.x,v.y},{v.color.x,v.color.y,v.color.z,v.color.w},{v.u,v.v}});
            check(SDL_RenderGeometry(impl_->renderer,draw.textured?slot.textures.at(draw.texture).get():nullptr,vertices.data(),int(vertices.size()),draw.indices.data(),int(draw.indices.size())));
        }
    }
}
void ScriptInterfaceDisplay::render_inventory(const InventoryPanel& panel){
    auto& slots=impl_->inventory.at(panel.mode()-3);
    for(unsigned i=0;i<slots.size();++i){auto& slot=slots[i];if(slot.generation==panel.generation())continue;slot.textures.clear();
        if(panel.panels()[i])for(const auto& [id,image]:panel.panels()[i]->movie.textures){
            Texture t(SDL_CreateTexture(impl_->renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,int(image.width),int(image.height)),SDL_DestroyTexture);check(bool(t));
            check(SDL_UpdateTexture(t.get(),nullptr,image.rgba.data(),int(image.width*4)));check(SDL_SetTextureBlendMode(t.get(),SDL_BLENDMODE_BLEND));check(SDL_SetTextureScaleMode(t.get(),SDL_SCALEMODE_LINEAR));slot.textures.emplace(id,std::move(t));
        }slot.generation=panel.generation();
    }
    GeometryBatch geometry(impl_->renderer);for(const auto& command:panel.compose()){const auto& draw=command.geometry;
        geometry.append(draw.textured?slots[command.panel].textures.at(draw.texture).get():nullptr,draw);
    }
    geometry.flush();
}
void ScriptInterfaceDisplay::render_control(const ControlPanel& panel){
    auto& slot=impl_->control;
    if(slot.generation!=panel.generation()){
        slot.textures.clear();
        if(panel.panel())for(const auto& [id,image]:panel.panel()->movie.textures){
            Texture t(SDL_CreateTexture(impl_->renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,int(image.width),int(image.height)),SDL_DestroyTexture);check(bool(t));
            check(SDL_UpdateTexture(t.get(),nullptr,image.rgba.data(),int(image.width*4)));
            check(SDL_SetTextureBlendMode(t.get(),SDL_BLENDMODE_BLEND));check(SDL_SetTextureScaleMode(t.get(),SDL_SCALEMODE_LINEAR));slot.textures.emplace(id,std::move(t));
        }
        slot.generation=panel.generation();
    }
    GeometryBatch geometry(impl_->renderer);for(const auto& draw:panel.compose()){
        geometry.append(draw.textured?slot.textures.at(draw.texture).get():nullptr,draw);
    }
    geometry.flush();
}
void ScriptInterfaceDisplay::render_combat(CombatPanel& panel,const SceneCamera& camera){
    if(impl_->combat_generation!=panel.generation()){
        for(unsigned i=0;i<impl_->combat.size();++i){
            auto& textures=impl_->combat[i].textures;textures.clear();
            if(!panel.panels()[i])continue;
            for(const auto& [id,image]:panel.panels()[i]->movie.textures){
                Texture t(SDL_CreateTexture(impl_->renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,int(image.width),int(image.height)),SDL_DestroyTexture);check(bool(t));
                check(SDL_UpdateTexture(t.get(),nullptr,image.rgba.data(),int(image.width*4)));
                check(SDL_SetTextureBlendMode(t.get(),SDL_BLENDMODE_BLEND));check(SDL_SetTextureScaleMode(t.get(),SDL_SCALEMODE_LINEAR));textures.emplace(id,std::move(t));
            }
        }
        impl_->combat_generation=panel.generation();
    }
    std::optional<WorldOverlayViewport> world;
    GeometryBatch geometry(impl_->renderer);
    for(const auto& command:panel.compose(camera)){
        // Only enemy health and number panels are world attached. The top
        // status panel, target list and selection cursor stay in HUD space.
        const bool attached=command.panel==1||command.panel>=4;
        if(attached!=world.has_value()){
            geometry.flush();
            if(attached)world.emplace(impl_->renderer);else world.reset();
        }
        const auto& draw=command.geometry;
        geometry.append(draw.textured?impl_->combat[command.panel].textures.at(draw.texture).get():nullptr,draw,[&](const FlashVertex& v){return world?world->point(v.x,v.y):SDL_FPoint{v.x,v.y};});
    }
    geometry.flush();
}
}
