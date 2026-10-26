#include "ui/menus.hpp"

namespace lifeline {
namespace {
std::string short_device(std::string value){
    if(value.size()<=32)return value;
    std::size_t end=29;while(end&&((static_cast<unsigned char>(value[end])&0xc0)==0x80))--end;
    return value.substr(0,end)+"...";
}
std::string device_help(const std::string& value){
    std::string result;std::size_t column=0;
    for(const auto c:value){if(column>=48&&c==' '){result+='\n';column=0;}else{result+=c;++column;}}
    return result+"\nLeft / Right: choose device.";
}
}
std::vector<OptionsController::SettingsRow> OptionsController::settings_rows()const{
    const bool panel=layout_==Layout::side_panel;
    const auto back=panel?"Return to Settings.":"Return to Options.";
    if(page_==Page::main)return {
        {"AUDIO",{},"Choose the playback device and sound volumes.\nSet stereo or mono output."},
        {"CONTROLS",{},"Set keyboard and joystick inputs.\nChanges save immediately."},
        {"DISPLAY",{},"Set display mode, resolution and aspect.\nChanges apply immediately."},
        {"GRAPHICS",{},"Balance image quality and performance.\nCompare changes in the scene."},
        {"MICROPHONE",{},"Choose your microphone and check its live level.\nAdjust gain while speaking normally."},
        {panel?"RETURN TO GAME":"BACK",{},panel?"Close Settings and resume the game.":"Return to the title screen."}};
    if(page_==Page::display)return {
        {"DISPLAY MODE",pending_graphics_.mode==WindowMode::windowed?"Windowed":pending_graphics_.mode==WindowMode::borderless?"Borderless":"Fullscreen","Windowed, Borderless or Fullscreen.\nChanges apply and save immediately."},
        {"RESOLUTION",std::to_string(pending_graphics_.width)+" x "+std::to_string(pending_graphics_.height),pending_graphics_.mode==WindowMode::borderless?"Choose the game's rendering resolution.\nBorderless fills the monitor.":"Choose a resolution for this monitor.\nChanges apply immediately."},
        {"VSYNC",pending_graphics_.vsync?"On":"Off","Match the monitor's refresh timing.\nOff allows uncapped presentation."},
        {"ASPECT RATIO",pending_graphics_.aspect_ratio==AspectRatio::automatic?"Auto":aspect_ratio_name(pending_graphics_.aspect_ratio),"4:3, 16:9 or match the display.\nBlack bars preserve the selected view."},
        {"RESTORE DEFAULTS",{},"Restore display settings only.\nGraphics quality stays unchanged."},
        {"BACK",{},back}};
    if(page_==Page::graphics){
        constexpr const char* presets[]{"Low","Medium","High","Ultra","Custom"};
        constexpr const char* textures[]{"Low","Medium","High"};
        constexpr const char* shadows[]{"Off","Low","Medium","High","Ultra"};
        constexpr const char* styles[]{"Original","Faithful","Balanced","Modern","Enhanced"};
        constexpr const char* style_help[]{"Current colors and image detail.\nNo additional image processing.","Subtle texture clarity.\nKeeps the original colors and atmosphere.","Crisper detail and gentle color grading.\nPreserves dark areas and skin tones.","Stronger detail, contrast and color.\nNo added lights or higher resolution.","Character lighting and surface depth.\nAdds ambient shading and contact detail."};
        return {
            {"QUALITY PRESET",presets[graphics_preset(pending_graphics_)],"Low favors speed; Ultra favors detail.\nIndividual changes select Custom."},
            {"RENDER SCALE",std::to_string(pending_graphics_.render_scale)+"%","Set the 3D rendering resolution.\nAbove 100% adds supersampling.\nMenus and HUD keep their usual size."},
            {"ANTI-ALIASING",pending_graphics_.quality.anti_aliasing?"FXAA":"Off","Smooth jagged edges in the 3D scene.\nMenus and HUD remain sharp."},
            {"TEXTURE QUALITY",textures[pending_graphics_.quality.texture_quality],"High keeps all source texture detail.\nMedium uses half, Low quarter size."},
            {"TEXTURE FILTERING",pending_graphics_.texture_filter==0?"Original / sharp":pending_graphics_.texture_filter==1?"Nearest":"Linear","Choose sharp or smooth texture sampling.\nChanges are visible immediately."},
            {"SHADOW QUALITY",shadows[pending_graphics_.quality.shadow_quality],"Off, Low 256, Medium 512.\nHigh 1024, Ultra 2048.\nHigher values sharpen shadow detail."},
            {"REFLECTIONS",pending_graphics_.quality.reflections==1?"High":pending_graphics_.quality.reflections==2?"Low":"Off","High: smooth reflection sampling. Low: faster sampling.\nOff removes mirror copies; essential refraction uses Low."},
            {"VISUAL STYLE",styles[pending_graphics_.quality.visual_style],style_help[pending_graphics_.quality.visual_style]},
            {"RESTORE DEFAULTS",{},"Restore graphics quality only.\nDisplay settings stay unchanged."},
            {"BACK",{},back}};
    }
    if(page_==Page::sound)return {
        {"PLAYBACK DEVICE",short_device(audio_device_label(playback_devices_,pending_graphics_.devices.playback)),device_help(audio_device_label(playback_devices_,pending_graphics_.devices.playback))},
        {"OUTPUT MODE",settings_->sound?"Mono":"Stereo","Choose stereo or mono sound.\nChanges apply and save immediately."},
        {"FMV VOLUME",std::to_string(pending_graphics_.audio.fmv)+"%","Adjust the complete movie soundtrack.\nLeft / Right: 5% steps. 0%: mute.\nA movie sample previews the level."},
        {"DIALOGUE VOLUME",std::to_string(pending_graphics_.audio.dialogue)+"%","Adjust spoken dialogue and narration.\nLeft / Right: 5% steps. 0%: mute.\nA spoken line previews the level."},
        {"MUSIC VOLUME",std::to_string(pending_graphics_.audio.music)+"%","Adjust streamed and sequenced music.\nLeft / Right: 5% steps. 0%: mute.\nA music sample previews the level."},
        {"RESTORE DEFAULTS",{},"Restore playback device, output and volumes.\nMicrophone settings stay unchanged."},
        {"BACK",{},back}};
    if(page_==Page::microphone)return {
        {"MICROPHONE DEVICE",short_device(audio_device_label(microphone_devices_,pending_graphics_.devices.microphone)),device_help(audio_device_label(microphone_devices_,pending_graphics_.devices.microphone))},
        {"MICROPHONE GAIN",std::to_string(pending_graphics_.devices.microphone_gain)+"%","Speak normally and watch Input Level.\nLeft / Right: gain in 5% steps. 100%: normal.\nLower gain if the meter says Too Loud."},
        {"RESTORE DEFAULTS",{},"Use the system microphone at 100% gain."},
        {"BACK",{},back}};
    return {};
}
}
