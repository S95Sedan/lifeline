#include "rendering/raster_scene.hpp"
#include "rendering/enhanced.hpp"
#include "opengl_scene_shader.hpp"
#include "opengl_plain_shader.hpp"
#include "opengl_aa_shader.hpp"
#include "opengl_style_shader.hpp"
#include "opengl_ambient_shader.hpp"
#include "opengl_enhanced_shader.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace lifeline {
namespace {
void checked(bool ok){if(!ok)throw std::runtime_error(std::string("OpenGL renderer: ")+SDL_GetError());}
struct GL {
    decltype(&glGetString) GetString=nullptr;
    decltype(&glGetError) GetError=nullptr;
    PFNGLGENBUFFERSPROC GenBuffers=nullptr;
    PFNGLDELETEBUFFERSPROC DeleteBuffers=nullptr;
    PFNGLBINDBUFFERPROC BindBuffer=nullptr;
    PFNGLBINDBUFFERBASEPROC BindBufferBase=nullptr;
    PFNGLBUFFERDATAPROC BufferData=nullptr;
    PFNGLBUFFERSUBDATAPROC BufferSubData=nullptr;
    PFNGLGETBUFFERSUBDATAPROC GetBufferSubData=nullptr;
    PFNGLCOPYBUFFERSUBDATAPROC CopyBufferSubData=nullptr;
    PFNGLCREATESHADERPROC CreateShader=nullptr;
    PFNGLSHADERSOURCEPROC ShaderSource=nullptr;
    PFNGLCOMPILESHADERPROC CompileShader=nullptr;
    PFNGLGETSHADERIVPROC GetShaderiv=nullptr;
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog=nullptr;
    PFNGLDELETESHADERPROC DeleteShader=nullptr;
    PFNGLCREATEPROGRAMPROC CreateProgram=nullptr;
    PFNGLATTACHSHADERPROC AttachShader=nullptr;
    PFNGLLINKPROGRAMPROC LinkProgram=nullptr;
    PFNGLGETPROGRAMIVPROC GetProgramiv=nullptr;
    PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog=nullptr;
    PFNGLDELETEPROGRAMPROC DeleteProgram=nullptr;
    PFNGLUSEPROGRAMPROC UseProgram=nullptr;
    PFNGLDISPATCHCOMPUTEPROC DispatchCompute=nullptr;
    PFNGLMEMORYBARRIERPROC MemoryBarrier=nullptr;
    PFNGLGETINTEGER64VPROC GetInteger64v=nullptr;
    void load(){
#define LOAD(name) name=reinterpret_cast<decltype(name)>(SDL_GL_GetProcAddress("gl" #name));if(!name)throw std::runtime_error("OpenGL 4.3 function unavailable: gl" #name)
        LOAD(GetString);LOAD(GetError);LOAD(GenBuffers);LOAD(DeleteBuffers);LOAD(BindBuffer);LOAD(BindBufferBase);
        LOAD(BufferData);LOAD(BufferSubData);LOAD(GetBufferSubData);LOAD(CopyBufferSubData);
        LOAD(CreateShader);LOAD(ShaderSource);LOAD(CompileShader);LOAD(GetShaderiv);LOAD(GetShaderInfoLog);LOAD(DeleteShader);
        LOAD(CreateProgram);LOAD(AttachShader);LOAD(LinkProgram);LOAD(GetProgramiv);LOAD(GetProgramInfoLog);LOAD(DeleteProgram);
        LOAD(UseProgram);LOAD(DispatchCompute);LOAD(MemoryBarrier);LOAD(GetInteger64v);
#undef LOAD
    }
};
// SDL's window compositor owns another GL context. Always restore it, including
// on exceptions, so scene rendering cannot invalidate its textures or state.
struct ContextGuard {
    SDL_Window* previous_window=SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context=SDL_GL_GetCurrentContext();
    ContextGuard(SDL_Window* window,SDL_GLContext context){checked(SDL_GL_MakeCurrent(window,context));}
    ~ContextGuard(){SDL_GL_MakeCurrent(previous_window,previous_context);}
};
class OpenGLRenderer final:public SceneRenderer {
    GL gl_;
    SDL_Window* window_=nullptr;
    SDL_GLContext context_=nullptr;
    bool video_initialized_=false;
    enum Pipeline {raster,plain_raster,anti_aliasing,visual_style,ambient,enhanced,pipeline_count};
    std::array<GLuint,pipeline_count> programs_{};
    GLuint settings_=0;
    // Bindings match the shared raster shader: triangles, texels, tiles,
    // captured frame, output frame, reciprocal depth, resolve scratch, surface.
    std::array<GLuint,8> buffers_{};
    std::array<std::size_t,8> capacities_{};
    GLint64 block_limit_=0;
    RasterScene scene_;
    // Exact previous upload contents, not a pointer/hash identity: actor scenes
    // can replace or mutate texture pages and shadow maps between submissions.
    std::vector<unsigned> uploaded_texels_;
    std::string label_="opengl";
    void gpu_checked(const char* action){
        const auto error=gl_.GetError();if(error!=GL_NO_ERROR)throw std::runtime_error(std::string("OpenGL ")+action+" failed (error "+std::to_string(error)+")");
    }
    void buffer(unsigned slot,std::size_t bytes){
        bytes=std::max<std::size_t>(bytes,256);
        if(bytes>512*1024*1024||bytes>std::size_t(block_limit_))throw std::runtime_error("Scene buffer exceeds the OpenGL device storage limit");
        if(capacities_[slot]>=bytes)return;
        gl_.BindBuffer(GL_SHADER_STORAGE_BUFFER,buffers_[slot]);gl_.BufferData(GL_SHADER_STORAGE_BUFFER,GLsizeiptr(bytes),nullptr,GL_DYNAMIC_DRAW);
        gpu_checked("buffer allocation");capacities_[slot]=bytes;
    }
    void upload(unsigned slot,const void* data,std::size_t bytes){
        buffer(slot,bytes);if(!bytes)return;
        gl_.BindBuffer(GL_SHADER_STORAGE_BUFFER,buffers_[slot]);gl_.BufferSubData(GL_SHADER_STORAGE_BUFFER,0,GLsizeiptr(bytes),data);
    }
    void download(unsigned slot,void* data,std::size_t bytes){
        gl_.BindBuffer(GL_SHADER_STORAGE_BUFFER,buffers_[slot]);gl_.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,GLsizeiptr(bytes),data);gpu_checked("scene readback");
    }
    void upload_texels(){
        const auto bytes=scene_.texels.size()*sizeof(unsigned);
        // Limit extra CPU storage. Larger scenes retain the full upload path;
        // this is a cache budget, never a texture/shadow quality limit.
        constexpr std::size_t cache_limit=64*1024*1024,chunk=64*1024;
        if(bytes>cache_limit){upload(1,scene_.texels.data(),bytes);std::vector<unsigned>().swap(uploaded_texels_);return;}
        // A previous oversized scene may have left packing capacity above the
        // budget. Compact once on re-entry, before retaining it as a cache.
        if(scene_.texels.capacity()>cache_limit/sizeof(unsigned))std::vector<unsigned>(scene_.texels).swap(scene_.texels);
        if(uploaded_texels_.size()!=scene_.texels.size())upload(1,scene_.texels.data(),bytes);
        else {
            const auto* next=reinterpret_cast<const unsigned char*>(scene_.texels.data());
            const auto* previous=reinterpret_cast<const unsigned char*>(uploaded_texels_.data());
            // Coalesce adjacent dirty chunks. Static room/actor textures cost
            // no GL upload, while animated shadow regions retain exact bytes.
            std::size_t begin=bytes;
            const auto flush=[&](std::size_t end){if(begin==bytes)return;
                gl_.BindBuffer(GL_SHADER_STORAGE_BUFFER,buffers_[1]);
                gl_.BufferSubData(GL_SHADER_STORAGE_BUFFER,GLintptr(begin),GLsizeiptr(end-begin),next+begin);begin=bytes;
            };
            for(std::size_t offset=0;offset<bytes;offset+=chunk){
                const auto count=std::min(chunk,bytes-offset);
                if(std::memcmp(next+offset,previous+offset,count)){if(begin==bytes)begin=offset;}
                else flush(offset);
            }
            flush(bytes);
        }
        // Reuse both packing allocations instead of copying the CPU cache.
        uploaded_texels_.swap(scene_.texels);
    }
    GLuint program(const char* source){
        const GLuint shader=gl_.CreateShader(GL_COMPUTE_SHADER);gl_.ShaderSource(shader,1,&source,nullptr);gl_.CompileShader(shader);
        GLint success=0;gl_.GetShaderiv(shader,GL_COMPILE_STATUS,&success);
        if(!success){std::array<char,8192> message{};gl_.GetShaderInfoLog(shader,GLsizei(message.size()),nullptr,message.data());gl_.DeleteShader(shader);throw std::runtime_error("OpenGL shader: "+std::string(message.data()));}
        const auto result=gl_.CreateProgram();gl_.AttachShader(result,shader);gl_.LinkProgram(result);gl_.DeleteShader(shader);gl_.GetProgramiv(result,GL_LINK_STATUS,&success);
        if(!success){std::array<char,8192> message{};gl_.GetProgramInfoLog(result,GLsizei(message.size()),nullptr,message.data());gl_.DeleteProgram(result);throw std::runtime_error("OpenGL program: "+std::string(message.data()));}
        return result;
    }
    void release()noexcept{
        if(context_){
            auto* previous_window=SDL_GL_GetCurrentWindow();const auto previous_context=SDL_GL_GetCurrentContext();
            if(SDL_GL_MakeCurrent(window_,context_)){
                if(gl_.DeleteBuffers){gl_.DeleteBuffers(8,buffers_.data());if(settings_)gl_.DeleteBuffers(1,&settings_);}
                if(gl_.DeleteProgram)for(auto program:programs_)if(program)gl_.DeleteProgram(program);
            }
            SDL_GL_MakeCurrent(previous_context==context_?nullptr:previous_window,previous_context==context_?nullptr:previous_context);
            SDL_GL_DestroyContext(context_);context_=nullptr;
        }
        if(window_){SDL_DestroyWindow(window_);window_=nullptr;}
        if(video_initialized_){SDL_QuitSubSystem(SDL_INIT_VIDEO);video_initialized_=false;}
    }
public:
    OpenGLRenderer(){
        checked(SDL_InitSubSystem(SDL_INIT_VIDEO));video_initialized_=true;
        auto* previous_window=SDL_GL_GetCurrentWindow();const auto previous_context=SDL_GL_GetCurrentContext();
        int major=2,minor=1,profile=0;
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,&major);SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,&minor);SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,&profile);
        const auto restore=[&]{SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,major);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,minor);SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,profile);SDL_GL_MakeCurrent(previous_window,previous_context);};
        try{
            checked(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4));checked(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3));checked(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE));
            window_=SDL_CreateWindow("Lifeline scene context",16,16,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);checked(window_!=nullptr);
            context_=SDL_GL_CreateContext(window_);checked(context_!=nullptr);gl_.load();
            label_="opengl / "+std::string(reinterpret_cast<const char*>(gl_.GetString(GL_RENDERER)));
            gl_.GetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&block_limit_);gpu_checked("capability query");
            programs_[raster]=program(opengl_scene_shader);
            programs_[plain_raster]=program(opengl_plain_shader);
            programs_[anti_aliasing]=program(opengl_aa_shader);
            programs_[visual_style]=program(opengl_style_shader);
            programs_[ambient]=program(opengl_ambient_shader);
            programs_[enhanced]=program(opengl_enhanced_shader);
            gl_.GenBuffers(8,buffers_.data());gl_.GenBuffers(1,&settings_);gl_.BindBuffer(GL_UNIFORM_BUFFER,settings_);gl_.BufferData(GL_UNIFORM_BUFFER,64,nullptr,GL_DYNAMIC_DRAW);gpu_checked("pipeline initialization");
            restore();std::cout<<"Scene renderer: "<<label_<<'\n';
        }catch(...){restore();release();throw;}
    }
    ~OpenGLRenderer(){release();}
    std::string_view name()const noexcept override{return label_;}
    AssetImage render(const SceneSubmission& submission)override{AssetImage image;render_into(image,submission);return image;}
    void render_into(AssetImage& image,const SceneSubmission& submission)override{
        ContextGuard context(window_,context_);
        auto size=scene_render_size();if(!submission.high_resolution||!size.first)size={640,448};
        prepare_raster_scene(scene_,submission,std::min(float(size.first)/640,float(size.second)/448),size.first,size.second);
        const auto pixels=std::size_t(size.first)*size.second,bytes=pixels*4;
        const auto& quality=scene_graphics_quality();
        const bool enhanced_scene=submission.high_resolution&&quality.visual_style==4;
        for(unsigned slot=3;slot<6;++slot)buffer(slot,bytes);
        // Only Enhanced consumes a surface mask and half-resolution lighting.
        // Retain existing allocations across style changes, but do not grow
        // unused buffers with the drawable or reserve a full frame for ambient.
        buffer(6,enhanced_scene?std::size_t((size.first+1)/2)*((size.second+1)/2)*4:256);
        buffer(7,enhanced_scene?bytes:256);
        upload_texels();
        std::size_t triangle_bytes=256,tile_bytes=256;
        for(const auto& batch:scene_.batches){triangle_bytes=std::max(triangle_bytes,batch.triangles.size()*sizeof(RasterTriangle));tile_bytes=std::max(tile_bytes,batch.tiles.size()*4);}
        buffer(0,triangle_bytes);buffer(2,tile_bytes);
        gl_.UseProgram(programs_[enhanced_scene?raster:plain_raster]);
        for(unsigned slot=0;slot<8;++slot)gl_.BindBufferBase(GL_SHADER_STORAGE_BUFFER,slot,buffers_[slot]);
        gl_.BindBufferBase(GL_UNIFORM_BUFFER,8,settings_);
        unsigned constants[]{0,scene_.clear,scene_.shadow_offset,submission.high_resolution?1u:0u,size.first,size.second,scene_.tile_columns,1,0,0,0,0,0,0,0,0};
        const auto dispatch=[&](unsigned operation,unsigned columns,unsigned rows){
            constants[0]=operation;
            gl_.BindBuffer(GL_UNIFORM_BUFFER,settings_);gl_.BufferSubData(GL_UNIFORM_BUFFER,0,sizeof(constants),constants);
            gl_.DispatchCompute(columns,rows,1);gl_.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);gpu_checked("scene dispatch");
        };
        bool initialized=false;
        for(const auto& batch:scene_.batches){
            if(batch.capture){
                // An empty leading batch still has to supply cleared capture
                // pixels before the first refractive/effect draw.
                if(!initialized){dispatch(0,scene_.tile_columns,scene_.tile_rows);initialized=true;}
                gl_.BindBuffer(GL_COPY_READ_BUFFER,buffers_[4]);gl_.BindBuffer(GL_COPY_WRITE_BUFFER,buffers_[3]);gl_.CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,0,GLsizeiptr(bytes));gl_.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            }
            if(batch.triangles.empty())continue;
            upload(0,batch.triangles.data(),batch.triangles.size()*sizeof(RasterTriangle));upload(2,batch.tiles.data(),batch.tiles.size()*4);
            // Operation 3 initializes color/depth/mask in registers and also
            // clears uncovered tiles. Avoid a separate full-frame clear pass.
            dispatch(initialized?1:3,scene_.tile_columns,scene_.tile_rows);initialized=true;
        }
        if(!initialized)dispatch(0,scene_.tile_columns,scene_.tile_rows);
        unsigned output=4;
        const bool aa=quality.anti_aliasing&&size.first>=3&&size.second>=3;
        if(submission.high_resolution&&(aa||quality.visual_style)){
            // Keep the immutable source frame on the GPU and filter into the
            // no-longer-needed capture buffer. Only the finished color is read back.
            gl_.BindBufferBase(GL_SHADER_STORAGE_BUFFER,3,buffers_[4]);
            gl_.BindBufferBase(GL_SHADER_STORAGE_BUFFER,4,buffers_[3]);output=3;
            constants[3]=(quality.visual_style==4?2:quality.visual_style)|(aa?16u:0u);
            if(quality.visual_style==4){
                const auto view=enhanced_view(submission.camera,size.first,size.second);
                std::memcpy(constants+7,&view.focal,sizeof(float));
                const float lighting[]{view.key.x,view.key.y,view.key.z,.85f,view.fill.x,view.fill.y,view.fill.z,.16f};std::memcpy(constants+8,lighting,sizeof(lighting));
                gl_.UseProgram(programs_[ambient]);
                dispatch(0,((size.first+1)/2+15)/16,((size.second+1)/2+15)/16);
                gl_.UseProgram(programs_[enhanced]);
            }else gl_.UseProgram(programs_[quality.visual_style?visual_style:anti_aliasing]);
            dispatch(0,scene_.tile_columns,scene_.tile_rows);
        }
        image.name="OpenGL scene";image.width=size.first;image.height=size.second;image.rgba.resize(bytes);download(output,image.rgba.data(),bytes);
    }
};
}
std::unique_ptr<SceneRenderer> make_opengl_renderer(){return std::make_unique<OpenGLRenderer>();}
}
