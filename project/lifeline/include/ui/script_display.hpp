#pragma once
#include <memory>
struct SDL_Renderer;
namespace lifeline {
class CombatPanel;
class ControlPanel;
class InventoryPanel;
struct SceneCamera;
namespace script {class Interface;class Effects;}
class ScriptInterfaceDisplay {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    ScriptInterfaceDisplay(SDL_Renderer*,const script::Interface&);
    ~ScriptInterfaceDisplay();
    enum class Layer { all, background, foreground };
    void render(Layer layer=Layer::all);
    void render_effects(const script::Effects&);
    void render_combat(CombatPanel&,const SceneCamera&);
    void render_control(const ControlPanel&);
    void render_inventory(const InventoryPanel&);
};
}
