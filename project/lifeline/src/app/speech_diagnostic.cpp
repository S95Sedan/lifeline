#include "app/speech_diagnostic.hpp"
#include "platform/microphone.hpp"
#include "media/audio_output.hpp"
#include "speech/recognizer.hpp"
#include "assets/installed_paths.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <iostream>

namespace lifeline {
namespace {
void checked(bool ok){if(!ok)throw std::runtime_error(SDL_GetError());}
}

void show_speech_diagnostic(const AssetDirectory& assets,const std::filesystem::path& output,const std::string& screenshot,bool vent,bool door,bool bomb){
    constexpr int vent_threshold=3800; // Observed os_4801.ctx script parameter in save-2 replay.
    checked(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO));struct Quit{~Quit(){SDL_Quit();}}quit;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Lifeline - Microphone diagnostic",960,640,SDL_WINDOW_RESIZABLE|(screenshot.empty()?0:SDL_WINDOW_HIDDEN)),SDL_DestroyWindow);checked(bool(window));
    std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRenderer(window.get(),nullptr),SDL_DestroyRenderer);checked(bool(renderer));
    checked(SDL_SetRenderLogicalPresentation(renderer.get(),480,320,SDL_LOGICAL_PRESENTATION_LETTERBOX));
    MicrophoneInput mic;
    const std::string context=bomb?"os_6539":door?"os_5711":"os_4801";
    const std::string context_path="speech/contexts/"+context+"/"+context;
    SpeechVocabulary vocabulary(assets.read(vent?context_path+".tsv":installed::tutorial_vocabulary));
    std::optional<CommandGrammar> grammar;
    if(vent)grammar.emplace(assets.read(context_path+".vgp"));
    const auto engine=std::make_shared<SpeechRecognizer>(assets.read("speech/engine/acoustics.bin"),
        assets.read(vent?context_path+".ctx":installed::tutorial_commands),assets.read(installed::speech_normalization),vent?vent_threshold:4000);
    SpeechMode mode=vent?SpeechMode::commands:SpeechMode::movement_practice;
    auto task=std::make_unique<SpeechTask>(vocabulary,mode,engine,grammar,speech_backend_configuration(),1,1,vent?context+".ctx":"tutorial");
    AudioStream playback;
    constexpr const char* bomb_phrase="Enter the sum of the last digits of Prime Minister Takayama's birth date";
    const std::array<const char*,3> colors=bomb?std::array<const char*,3>{bomb_phrase,bomb_phrase,bomb_phrase}:door?std::array<const char*,3>{"Ready, set","Get ready set","I need some time"}:std::array<const char*,3>{"Red","Blue","Green"};
    unsigned expected=0,auto_save_left=0,attempts=0,correct=0;
    std::vector<std::int16_t> last;std::string last_expected=colors[0],status=vent?"Ready: say Red, Blue, Green in order.":"Ready: hold T and say Run, then release.",metrics,detail,saved;bool running=true,focused=true;
    if(door)status="Ready: say Ready set, Get ready set, I need some time.";
    if(bomb)status="Press F8, then record the phrase below 3 times.";
    if(bomb)std::cout<<"BOMB TEST: os_6539.ctx + VGP; repeat: "<<bomb_phrase<<'\n';
    std::cout<<(door?"Exhibit door test (os_5711.ctx + VGP): T hold/release, 1/2/3 phrase, F8 save next 3, F9 save last, Esc exit.\n":vent?"Vent voice test (real os_4801.ctx + VGP): T hold/release, 1/2/3 expected color, P playback, F8 save next 3, F9 save last, Esc exit.\n":"Speech diagnostic: T hold/release, P playback, F9 save last recording, 1/2/3 vocabulary, Esc exit.\n")
        <<(vent?"Nothing is saved until F8 or F9 is pressed. Output folder: ":"Nothing is saved until F9 is pressed. Output folder: ")<<output.string()<<'\n';
    const auto submit=[&]{if(!last.empty()&&!task->busy()){task->submit(last);status="Recognizing...";detail.clear();saved.clear();}};
    while(running){
        SDL_Event event{};while(SDL_PollEvent(&event)){
            audio_device_event(event);
            if(event.type==SDL_EVENT_QUIT)running=false;
            if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST){focused=false;mic.cancel();task->cancel();status="Capture cancelled: focus lost.";}
            if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED)focused=true;
            if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat){
                const auto key=event.key.key;if(key==SDLK_ESCAPE)running=false;
                if(vent&&key==SDLK_F8&&!mic.recording()&&!task->busy()){auto_save_left=auto_save_left?0:3;saved=auto_save_left?"Next 3 attempts will be saved under dev.":"Automatic saving off.";}
                if(key==SDLK_F9&&!last.empty()&&!mic.recording()&&!task->busy()){
                    try{const auto wav=save_speech_report(output,last,"Mode: "+std::to_string(int(mode))+"\n"+(vent?"Context: "+context+".ctx\nExpected: "+last_expected+"\n":"")+status+"\n"+detail);saved="Saved locally. See the console for the file path.";std::cout<<"Saved microphone recording: "<<wav.string()<<'\n';}
                    catch(const std::exception& e){saved=e.what();}
                }
                if(key==SDLK_P&&!last.empty()&&!mic.recording()&&!task->busy()){
                    mic.cancel();const SDL_AudioSpec spec{SDL_AUDIO_S16,1,16000};
                    if(!playback)playback=open_output_stream(spec);
                    if(playback){checked(SDL_ClearAudioStream(playback.get()));checked(SDL_PutAudioStreamData(playback.get(),last.data(),int(last.size()*2)));checked(SDL_FlushAudioStream(playback.get()));checked(SDL_ResumeAudioStreamDevice(playback.get()));}
                }
                const auto& speak=input_bindings().keyboard[unsigned(InputAction::speak)];
                if(speak.kind==BindingKind::key&&event.key.scancode==speak.code&&!task->busy()){if(playback)checked(SDL_ClearAudioStream(playback.get()));saved.clear();}
                if(key>=SDLK_1&&key<=SDLK_3&&!task->busy()&&!mic.recording()){
                    if(vent){expected=unsigned(key-SDLK_1);status=std::string("Next: ")+colors[expected];}
                    else{mode=key==SDLK_1?SpeechMode::microphone_practice:key==SDLK_2?SpeechMode::movement_practice:SpeechMode::commands;
                        task=std::make_unique<SpeechTask>(vocabulary,mode,engine);submit();}
                }
            }
            if(focused&&(!task->busy()||(vent&&mic.recording())))mic.event(event,0,task->ready()&&!task->busy());
        }
        if(!running)break;
        const bool playing=playback&&SDL_GetAudioStreamQueued(playback.get())>0;
        if(focused&&(!task->busy()||(vent&&mic.recording()))&&!playing&&screenshot.empty())mic.prepare();
        // Configuration/removal invalidation must run even while recognition
        // is busy, and before either diagnostic consumes a published result.
        mic.update();
        while(auto audio=mic.take_event()){
            if(audio->type==MicrophoneUtteranceEvent::Type::cancel){task->cancel();continue;}
            if(!vent)continue;
            switch(audio->type){
            case MicrophoneUtteranceEvent::Type::begin:
                if(task->begin()&&!audio->samples.empty())task->accept_audio(audio->samples);
                break;
            case MicrophoneUtteranceEvent::Type::audio:task->accept_audio(audio->samples);break;
            case MicrophoneUtteranceEvent::Type::end:task->finish_input();status="Recognizing...";break;
            case MicrophoneUtteranceEvent::Type::cancel:task->cancel();break;
            }
        }
        if(auto pcm=mic.take_capture()){last=std::move(*pcm);if(vent)last_expected=colors[expected];metrics=speech_recording_levels(last);if(!vent)submit();}
        if(auto result=task->take()){
            const auto score=speech_score(*result);
            if(vent){
                const auto parsed=grammar->parse(result->components.empty()?result->text:result->components.front());
                const bool accepted=result->status==SpeechResultStatus::final&&result->error.empty()&&result->rejection==SpeechRejection::none&&score&&*score>=vent_threshold&&parsed.command>=0;
                const auto expected_command=bomb?"Enter the sum of the last digits of PM Takayama's birth date":door&&expected==1?"Ready, set":colors[expected];
                const bool match=accepted&&normalize_spoken_word(result->text)==normalize_spoken_word(expected_command);
                const std::string reason=!result->error.empty()?"engine error":result->rejection!=SpeechRejection::none?"recognizer rejected":!score?"no score":*score<vent_threshold?"below scene threshold":parsed.command<0?"no active command":match?"correct":"wrong command";
                ++attempts;if(match)++correct;
                status=std::string(match?"PASS":"MISS")+" expected "+colors[expected]+" | heard "+(result->text.empty()?"<none>":result->text)+" | score "+std::to_string(score.value_or(0));
                detail=reason+" | command "+std::to_string(parsed.command)+" | rejection "+std::to_string(int(result->rejection))+" | "+(result->error.empty()?result->diagnostic:result->error);
                std::cout<<(bomb?"BOMB ":door?"DOOR ":"VENT ")<<attempts<<" expected="<<colors[expected]<<" heard="<<(result->text.empty()?"<none>":result->text)
                    <<" score="<<score.value_or(0)<<" command="<<parsed.command<<" rejection="<<int(result->rejection)
                    <<" duration_ms="<<last.size()/16<<" result="<<(match?"PASS":"MISS")<<" reason="<<reason<<'\n'<<metrics<<'\n'<<detail<<'\n';
                if(auto_save_left&& !last.empty()){
                    try{const auto path=save_speech_report(output,last,"Context: "+context+".ctx\nExpected: "+std::string(colors[expected])+"\n"+status+"\n"+detail);saved="Saved: "+path.filename().string();std::cout<<"Saved voice test: "<<path.string()<<'\n';}
                    catch(const std::exception& e){saved=e.what();}
                    --auto_save_left;
                }
                expected=(expected+1)%colors.size();
            }else{
                status=!result->error.empty()?result->error:result->text.empty()?"No accepted word.":"Heard: "+result->text+" | score "+std::to_string(score.value_or(0));
                detail=result->diagnostic;std::cout<<metrics<<'\n'<<status<<'\n'<<detail<<'\n';
            }
        }
        checked(SDL_SetRenderDrawColor(renderer.get(),10,18,24,255));checked(SDL_RenderClear(renderer.get()));
        const auto text=[&](float y,const std::string& s){checked(SDL_RenderDebugText(renderer.get(),12,y,s.c_str()));};
        checked(SDL_SetRenderDrawColor(renderer.get(),40,230,200,255));text(12,bomb?"LIFELINE - BOMB THIRD CLUE VOICE TEST":door?"LIFELINE - EXHIBIT DOOR VOICE TEST":vent?"LIFELINE - VENT VOICE TEST":"LIFELINE - MICROPHONE DIAGNOSTIC");
        checked(SDL_SetRenderDrawColor(renderer.get(),230,235,240,255));
        text(34,"Hold "+input_prompt(InputAction::speak)+": speak | Release: recognize | P: replay");text(47,"F9: save this recording locally | Esc: close");
        text(69,bomb?"F8: save next 3 attempts of the SAME phrase":door?"1/2/3: select door phrase | F8: save next 3":vent?"1/2/3: set expected Red/Blue/Green | F8: save next 3":"1: Hello/PlayStation/Lifeline | 2: Walk/Run/Stop");
        text(82,vent?"Same mic, disc model, grammar; scene threshold 3800":"3: full vocabulary (also retries the last recording)");
        if(bomb){text(102,"Enter the sum of the last digits of");text(114,"Prime Minister Takayama's birth date");}
        else text(102,vent?"NEXT: "+std::string(colors[expected])+" | passed "+std::to_string(correct)+"/"+std::to_string(attempts):mode==SpeechMode::movement_practice?"ACTIVE: Walk / Run / Stop":mode==SpeechMode::microphone_practice?"ACTIVE: Hello / PlayStation / Lifeline":"ACTIVE: full command vocabulary");
        text(130,mic.recording()||mic.unavailable()?mic.status():bomb&&attempts?status.substr(0,4)+" | passed "+std::to_string(correct)+"/"+std::to_string(attempts):status);
        for(std::size_t i=0;i<metrics.size();i+=56)text(145+float(i/56)*12,metrics.substr(i,56));
        for(std::size_t i=0;i<detail.size()&&i<280;i+=56)text(179+float(i/56)*12,detail.substr(i,56));
        text(258,saved.empty()?(vent?"Recordings stay in memory unless you press F8 or F9.":"Recordings stay in memory unless you press F9."):saved);
        checked(SDL_SetRenderDrawColor(renderer.get(),40,230,200,255));
        for(unsigned i=0;i<16;++i){const float h=std::clamp(mic.spectrum()[i],0.f,1.f)*30;SDL_FRect bar{12+float(i)*28,308-h,24,h};checked(SDL_RenderFillRect(renderer.get(),&bar));}
        if(!screenshot.empty()){
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> image(SDL_RenderReadPixels(renderer.get(),nullptr),SDL_DestroySurface);checked(bool(image));checked(SDL_SaveBMP(image.get(),screenshot.c_str()));running=false;
        }
        checked(SDL_RenderPresent(renderer.get()));SDL_Delay(8);
    }
}
}
