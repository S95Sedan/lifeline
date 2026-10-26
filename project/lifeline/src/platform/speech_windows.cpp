#include "speech/task.hpp"
#include "speech/audio.hpp"
#define NOMINMAX
#include <windows.h>
#include <sapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <string_view>
#include <set>
namespace lifeline {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT hr,const char* operation){if(FAILED(hr)){std::ostringstream s;s<<operation<<" failed (0x"<<std::hex<<unsigned(hr)<<")";throw std::runtime_error(s.str());}}
std::string utf8(const wchar_t* text){if(!text)return {};int n=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);if(n<=1)return {};std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,text,-1,s.data(),n,nullptr,nullptr);s.pop_back();return s;}
std::wstring lexical_text(std::string_view source){
    // Disc spellings carry punctuation for display. Supply lexical words to
    // SAPI and retain the disc command identity as a semantic property.
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,source.data(),int(source.size()),nullptr,0);
    if(!count)throw std::runtime_error("Invalid UTF-8 command");
    std::wstring text(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,source.data(),int(source.size()),text.data(),count);
    for(auto& c:text){if(c==L'.'||c==L','||c==L'!'||c==L'?'||c==L'-')c=L' ';else if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';}
    std::wstring result;for(auto c:text){if(c==L' ' && (result.empty()||result.back()==L' '))continue;result+=c;}
    if(!result.empty()&&result.back()==L' ')result.pop_back();return result;
}
void command_properties(const SPPHRASEPROPERTY* property,std::vector<unsigned>& ids){
    for(auto* p=property;p;p=p->pNextSibling){
        if(p->pszName && std::wstring_view(p->pszName)==L"command" && p->vValue.vt==VT_I4 && p->vValue.lVal>=0)ids.push_back(unsigned(p->vValue.lVal));
        else command_properties(p->pFirstChild,ids);
    }
}
}
static SpeechResult recognize_windows_pass(std::span<const std::int16_t> pcm,const SpeechVocabulary& words,std::stop_token stop,SpeechMode mode){
    SpeechResult output;bool validated_sequence=false;
    if(stop.stop_requested()){output.error="Recognition cancelled";return output;}
    if(pcm.size()<1600 || pcm.size()>160000){output.error="Hold T / R1 while saying the word, then release";return output;}
    double energy=0;for(auto s:pcm)energy+=double(s)*s;
    if(energy/pcm.size()<64){output.diagnostic="No speech detected";output.rejection=SpeechRejection::silence;return output;}
    const auto conditioned=prepare_speech_audio(pcm);pcm=conditioned;
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(initialized)){output.error="Cannot initialize native speech worker";return output;}
    struct Quit{~Quit(){CoUninitialize();}}quit;
    try{
        ComPtr<ISpRecognizer> recognizer;check(CoCreateInstance(CLSID_SpInprocRecognizer,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&recognizer)),"Create Windows recognizer");
        ComPtr<ISpObjectTokenCategory> category;check(CoCreateInstance(CLSID_SpObjectTokenCategory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&category)),"Open recognizer category");
        check(category->SetId(SPCAT_RECOGNIZERS,FALSE),"Find recognizer category");
        ComPtr<IEnumSpObjectTokens> tokens;check(category->EnumTokens(L"Language=409",nullptr,&tokens),"Enumerate English recognizers");
        // This adapter's confidence scale is for Windows' built-in English-US
        // desktop engine. Do not silently select an unrelated third-party engine.
        ComPtr<ISpObjectToken> token;
        for(;;){
            ComPtr<ISpObjectToken> candidate;ULONG count=0;check(tokens->Next(1,&candidate,&count),"Find installed English recognizer");if(!count)break;
            wchar_t* id=nullptr;const auto hr=candidate->GetId(&id);const bool builtin=SUCCEEDED(hr) && id && std::wstring_view(id).ends_with(L"MS-1033-80-DESK");CoTaskMemFree(id);
            if(builtin){token=std::move(candidate);break;}
        }
        if(!token)throw std::runtime_error("Enable English (United States) speech recognition in Windows to use voice input");
        check(recognizer->SetRecognizer(token.Get()),"Select built-in English recognizer");
        // The desktop default can discard commands before the game's 4000-point
        // passing threshold is evaluated. Configure only this private recognizer.
        check(recognizer->SetPropertyNum(L"CFGConfidenceRejectionThreshold",40),"Set command rejection threshold");
        check(recognizer->SetPropertyNum(L"NormalConfidenceThreshold",40),"Set word acceptance threshold");
        check(recognizer->SetPropertyNum(L"ResourceUsage",100),"Set speech search effort");
        check(recognizer->SetRecoState(SPRST_INACTIVE),"Pause recognizer during setup");
        ComPtr<ISpRecoContext> context;check(recognizer->CreateRecoContext(&context),"Create speech context");
        check(context->SetNotifyWin32Event(),"Set recognition notification");
        const auto interests=SPFEI(SPEI_RECOGNITION)|SPFEI(SPEI_FALSE_RECOGNITION)|SPFEI(SPEI_END_SR_STREAM);
        check(context->SetInterest(interests,interests),"Select speech events");
        ComPtr<IStream> memory;check(CreateStreamOnHGlobal(nullptr,TRUE,&memory),"Create audio memory stream");
        // Padding lets the engine finish an utterance released close to the word.
        std::vector<std::int16_t> padded(4000,0);padded.insert(padded.end(),pcm.begin(),pcm.end());padded.resize(padded.size()+8000,0);
        ULONG written=0;check(memory->Write(padded.data(),ULONG(padded.size()*2),&written),"Write captured audio");if(written!=padded.size()*2)throw std::runtime_error("Incomplete speech audio write");
        LARGE_INTEGER beginning{};check(memory->Seek(beginning,STREAM_SEEK_SET,nullptr),"Rewind captured audio");
        ComPtr<ISpStream> stream;check(CoCreateInstance(CLSID_SpStream,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&stream)),"Create formatted audio stream");
        WAVEFORMATEX format{WAVE_FORMAT_PCM,1,16000,32000,2,16,0};check(stream->SetBaseStream(memory.Get(),SPDFID_WaveFormatEx,&format),"Set captured PCM format");
        check(recognizer->SetInput(stream.Get(),FALSE),"Set captured speech input");
        // Each lesson includes all its competing commands, independently of
        // the expected answer. Later gameplay can select the complete table.
        ComPtr<ISpRecoGrammar> vocabulary;check(context->CreateGrammar(2,&vocabulary),"Create vocabulary grammar");
        check(vocabulary->ResetGrammar(0x409),"Set vocabulary language");
        SPSTATEHANDLE rule=nullptr;
        check(vocabulary->GetRule(L"tutorialWords",1,SPRAF_TopLevel|SPRAF_Active|SPRAF_Dynamic,TRUE,&rule),"Create Tutorial vocabulary");
        // Gameplay's recovered grammar consumes up to eight components (626).
        // A terminal-only host grammar discarded normal compound commands.
        std::vector<SPSTATEHANDLE> states{rule};
        if(mode==SpeechMode::commands)for(unsigned i=1;i<8;++i){SPSTATEHANDLE state=nullptr;check(vocabulary->CreateNewState(rule,&state),"Create command sequence state");states.push_back(state);}
        for(unsigned i=1;i<states.size();++i)check(vocabulary->AddWordTransition(states[i],nullptr,nullptr,nullptr,SPWT_LEXICAL,1,nullptr),"Allow completed command sequence");
        unsigned added=0;std::set<std::wstring> lexical_words;
        const auto add=[&](const std::wstring& text,unsigned id){
            if(text.empty()||!lexical_words.insert(text).second)return;
            SPPROPERTYINFO property{};property.pszName=L"command";property.vValue.vt=VT_I4;property.vValue.lVal=LONG(id);
            for(unsigned i=0;i<states.size();++i)check(vocabulary->AddWordTransition(states[i],i+1<states.size()?states[i+1]:nullptr,text.c_str(),L" ",SPWT_LEXICAL_NO_SPECIAL_CHARS,1,&property),"Add command word");++added;
        };
        for(const auto& word:words.words())if(!word.control && (mode==SpeechMode::commands || (mode==SpeechMode::microphone_practice && word.id<3) || (mode==SpeechMode::movement_practice && word.id>=3 && word.id<=5))){
            add(lexical_text(word.text),word.id);
            if(normalize_spoken_word(word.text)=="playstation")add(L"play station",word.id);
            if(mode==SpeechMode::microphone_practice && normalize_spoken_word(word.text)=="lifeline")add(L"life line",word.id);
        }
        if(!added)throw std::runtime_error("Installed speech vocabulary has no words for this phase");
        check(vocabulary->Commit(0),"Commit Tutorial vocabulary");check(vocabulary->SetRuleState(L"tutorialWords",nullptr,SPRS_ACTIVE),"Activate Tutorial vocabulary");

        check(recognizer->SetRecoState(SPRST_ACTIVE_ALWAYS),"Start recognition");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);bool ended=false,rejected=false;
        unsigned phrases=0;float confidence=-1,rejected_confidence=-1;
        while(!ended && !stop.stop_requested() && std::chrono::steady_clock::now()<deadline){
            WaitForSingleObject(context->GetNotifyEventHandle(),50);
            SPEVENT event{};ULONG fetched=0;
            while(context->GetEvents(1,&event,&fetched)==S_OK && fetched){
                if(event.eEventId==SPEI_RECOGNITION){
                    auto* result=reinterpret_cast<ISpRecoResult*>(event.lParam);wchar_t* text=nullptr;
                    const auto hr=result->GetText(SP_GETWHOLEPHRASE,SP_GETWHOLEPHRASE,TRUE,&text,nullptr);
                    if(SUCCEEDED(hr)){if(!output.text.empty())output.text+=' ';output.text+=utf8(text);}CoTaskMemFree(text);
                    SPPHRASE* phrase=nullptr;
                    if(SUCCEEDED(result->GetPhrase(&phrase))){
                        std::vector<unsigned> ids;command_properties(phrase->pProperties,ids);
                        validated_sequence=!ids.empty()&&ids.size()<=8&&std::all_of(ids.begin(),ids.end(),[&](unsigned id){return id<words.words().size()&&!words.words()[id].control;});
                        if(validated_sequence){output.text.clear();output.components.clear();for(auto id:ids){const auto& word=words.words()[id].text;output.components.push_back(word);if(!output.text.empty())output.text+=' ';output.text+=word;}if(ids.size()==1)output.word_id=ids.front();}
                        confidence=phrase->Rule.SREngineConfidence;CoTaskMemFree(phrase);
                    }else rejected=true;
                    ++phrases;
                }
                if(event.eEventId==SPEI_FALSE_RECOGNITION){
                    rejected=true;
                    if(event.lParam){SPPHRASE* phrase=nullptr;auto* result=reinterpret_cast<ISpRecoResult*>(event.lParam);
                        if(SUCCEEDED(result->GetPhrase(&phrase)) && phrase){rejected_confidence=phrase->Rule.SREngineConfidence;CoTaskMemFree(phrase);}}
                }
                if(event.eEventId==SPEI_END_SR_STREAM)ended=true;
                if(event.elParamType==SPET_LPARAM_IS_OBJECT || event.elParamType==SPET_LPARAM_IS_TOKEN){if(event.lParam)reinterpret_cast<IUnknown*>(event.lParam)->Release();}
                else if(event.elParamType==SPET_LPARAM_IS_POINTER || event.elParamType==SPET_LPARAM_IS_STRING)CoTaskMemFree(reinterpret_cast<void*>(event.lParam));
                event={};fetched=0;
            }
        }
        recognizer->SetRecoState(SPRST_INACTIVE_WITH_PURGE);
        if(stop.stop_requested()){output.text.clear();output.error="Recognition cancelled";}
        else if(!ended){output.text.clear();output.error="Speech recognition timed out; release and retry";}
        else if(rejected){
            output.rejection=output.text.empty()?SpeechRejection::engine_rejected:SpeechRejection::extra_speech;
            output.diagnostic=output.text.empty()?"Speech was not recognized":"Speech engine rejected part of the recording; heard: "+output.text;
            if(std::isfinite(rejected_confidence) && rejected_confidence>=0 && rejected_confidence<=1)output.diagnostic+=" (confidence "+std::to_string(int(std::lround(rejected_confidence*100)))+"%)";
            output.text.clear();
        }
        else if(phrases!=1){output.rejection=phrases?SpeechRejection::extra_speech:SpeechRejection::no_word;output.diagnostic=phrases?"More than one phrase detected; heard: "+output.text:"No word recognized";output.text.clear();}
        // SAPI already reports separate/rejected phrases above. Signal energy
        // outside its word endpoints also includes breaths and room noise and
        // cannot independently establish that another word was spoken.
        else if(!output.text.empty())output.pc_score=pc_recognition_score(confidence);
    }catch(const std::exception& e){output.text.clear();output.pc_score.reset();output.error=e.what();}
    if(!output.text.empty()){
        if(!validated_sequence){output.diagnostic="Recognized phrase has no active command identity: "+output.text;output.text.clear();}
    }
    if(output.text.empty()){output.pc_score.reset();output.word_id.reset();output.components.clear();}
    return output;
}
SpeechResult recognize_speech(std::span<const std::int16_t> pcm,const SpeechVocabulary& words,std::stop_token stop,SpeechMode mode){
    auto first=recognize_windows_pass(pcm,words,stop,mode);
    if(mode!=SpeechMode::movement_practice || !first.error.empty() || !first.text.empty() || stop.stop_requested() || pcm.size()>106666 ||
       (first.rejection!=SpeechRejection::no_word && first.rejection!=SpeechRejection::engine_rejected))return first;
    // Short commands can finish before this engine forms a word hypothesis.
    // A second, pitch-preserving duration pass uses the same competing words
    // and acceptance threshold. Never retry a valid different command or an
    // explicit additional-speech rejection against the expected answer.
    const auto expanded=stretch_speech_audio(pcm,1.5f);
    auto second=recognize_windows_pass(expanded,words,stop,mode);
    if(!second.error.empty())return second;
    if(second.text.empty()){first.diagnostic+="; duration retry: "+second.diagnostic;return first;}
    // Time expansion can also make non-commands resemble a command. Only use
    // a strong secondary result automatically; retain weaker candidates in
    // diagnostics so they can be investigated with the actual recording.
    if(!second.pc_score || *second.pc_score<9000){
        first.diagnostic+="; duration candidate: "+second.text+" ("+std::to_string(second.pc_score.value_or(0)/100)+"%, needs verification)";
        return first;
    }
    second.duration_adjusted=true;
    second.diagnostic="Duration-adjusted recognition; initial pass: "+first.diagnostic;
    return second;
}
}
