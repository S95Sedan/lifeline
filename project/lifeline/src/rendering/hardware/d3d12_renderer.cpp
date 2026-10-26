#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "rendering/renderer.hpp"
#include "rendering/raster_scene.hpp"
#include "rendering/enhanced.hpp"
#include "d3d12_shader.hpp"
#include <bit>
#include <cstring>
#include <execution>
#include <iostream>
#include <sstream>

namespace lifeline {
namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT64 output_bytes=640*448*4;
void checked(HRESULT status,const char* action){if(FAILED(status)){std::ostringstream s;s<<"D3D12 "<<action<<" failed (HRESULT 0x"<<std::hex<<unsigned(status)<<")";throw std::runtime_error(s.str());}}
struct Event {
    HANDLE value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    Event(){if(!value)throw std::runtime_error("D3D12 fence event creation failed");}
    ~Event(){CloseHandle(value);}
};
class D3D12Renderer final:public SceneRenderer {
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> commands_;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12PipelineState> pipeline_,anti_aliasing_pipeline_,visual_style_pipeline_,enhanced_raster_pipeline_,ambient_pipeline_,enhanced_pipeline_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12Resource> frame_,depth_,capture_,resolved_,readback_,upload_;
    ComPtr<ID3D12Resource> enhanced_frame_,ambient_,surface_;
    Event event_;
    UINT64 serial_=0,upload_capacity_=0;
    unsigned char* upload_data_=nullptr;
    const unsigned char* readback_data_=nullptr;
    std::string label_;
    unsigned supersample_=2,width_=1280,height_=896;
    unsigned enhanced_width_=0,enhanced_height_=0;
    bool poisoned_=false;
    RasterScene scene_;
    ComPtr<ID3D12Resource> buffer(UINT64 size,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state,bool uav=false){
        D3D12_HEAP_PROPERTIES properties{};properties.Type=heap;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=size;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
        ComPtr<ID3D12Resource> resource;checked(device_->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&resource)),"buffer allocation");return resource;
    }
    void transition(ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};commands_->ResourceBarrier(1,&barrier);}
    void order(){D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;commands_->ResourceBarrier(1,&barrier);}
    void ensure_enhanced(){
        if(enhanced_width_==width_&&enhanced_height_==height_)return;
        wait();const UINT64 bytes=UINT64(width_)*height_*4;
        auto frame=buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
        auto surface=buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
        auto ambient=buffer(UINT64((width_+1)/2)*((height_+1)/2)*4,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
        enhanced_frame_=std::move(frame);surface_=std::move(surface);ambient_=std::move(ambient);enhanced_width_=width_;enhanced_height_=height_;
    }
    void wait(){
        if(!serial_||fence_->GetCompletedValue()>=serial_){checked(device_->GetDeviceRemovedReason(),"device status");return;}
        checked(fence_->SetEventOnCompletion(serial_,event_.value),"fence notification");
        const auto status=WaitForSingleObject(event_.value,10000);
        checked(device_->GetDeviceRemovedReason(),"device status");
        if(status!=WAIT_OBJECT_0)throw std::runtime_error("D3D12 GPU did not finish within 10 seconds; restart the game and check the display driver");
    }
    void resize(unsigned width,unsigned height){
        wait();
        if(readback_)readback_->Unmap(0,nullptr);
        readback_data_=nullptr;width_=width;height_=height;
        const UINT64 bytes=UINT64(width)*height*4;
        frame_=buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
        depth_=buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
        capture_=buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        readback_=buffer(std::max(bytes,output_bytes),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        void* mapped=nullptr;const D3D12_RANGE read{0,SIZE_T(std::max(bytes,output_bytes))};
        checked(readback_->Map(0,&read,&mapped),"readback resize map");readback_data_=static_cast<const unsigned char*>(mapped);
    }
public:
    explicit D3D12Renderer(bool debug,unsigned supersample):supersample_(supersample),width_(640*supersample),height_(448*supersample){
        if(supersample_<1||supersample_>4)throw std::runtime_error("D3D12 supersampling must be between 1x and 4x");
        if(debug){ComPtr<ID3D12Debug> layer;checked(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)),"debug layer (install Windows Graphics Tools)");layer->EnableDebugLayer();}
        ComPtr<IDXGIFactory6> factory;checked(CreateDXGIFactory2(debug?DXGI_CREATE_FACTORY_DEBUG:0,IID_PPV_ARGS(&factory)),"DXGI factory");
        for(UINT index=0;;++index){ComPtr<IDXGIAdapter1> adapter;const auto status=factory->EnumAdapterByGpuPreference(index,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter));if(status==DXGI_ERROR_NOT_FOUND)break;checked(status,"adapter enumeration");
            DXGI_ADAPTER_DESC1 desc{};checked(adapter->GetDesc1(&desc),"adapter description");if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
            if(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device_)))){
                const int count=WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,nullptr,0,nullptr,nullptr);std::string gpu(count,'\0');WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,gpu.data(),count,nullptr,nullptr);gpu.pop_back();label_="d3d12 / "+gpu;break;}
        }
        if(!device_)throw std::runtime_error("No D3D12-capable hardware adapter. Use run.bat for software rendering.");
        D3D12_COMMAND_QUEUE_DESC queue_desc{};queue_desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;checked(device_->CreateCommandQueue(&queue_desc,IID_PPV_ARGS(&queue_)),"command queue");
        checked(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator_)),"command allocator");
        checked(device_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator_.Get(),nullptr,IID_PPV_ARGS(&commands_)),"command list");checked(commands_->Close(),"initial command list close");
        checked(device_->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence_)),"fence");
        D3D12_ROOT_PARAMETER parameters[12]{};
        for(unsigned i=0;i<7;++i){parameters[i].ParameterType=i<4?D3D12_ROOT_PARAMETER_TYPE_SRV:D3D12_ROOT_PARAMETER_TYPE_UAV;parameters[i].Descriptor.ShaderRegister=i<4?i:i-4;parameters[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;}
        parameters[7].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[7].Constants={0,0,16};
        parameters[8].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[8].Descriptor.ShaderRegister=4;
        parameters[9].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[9].Descriptor.ShaderRegister=5;
        parameters[10].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;parameters[10].Descriptor.ShaderRegister=3;
        parameters[11].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[11].Descriptor.ShaderRegister=6;
        D3D12_ROOT_SIGNATURE_DESC root_desc{};root_desc.NumParameters=12;root_desc.pParameters=parameters;
        ComPtr<ID3DBlob> blob,errors;checked(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors),"root signature serialization");checked(device_->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root_)),"root signature");
        const auto pipeline=[&](const char* entry,ComPtr<ID3D12PipelineState>& state,bool enhanced_raster=false){
            const D3D_SHADER_MACRO defines[]{{"ENHANCED_RASTER","1"},{nullptr,nullptr}};
            ComPtr<ID3DBlob> shader;errors.Reset();const auto compile=D3DCompile(scene_shader,sizeof(scene_shader)-1,"d3d12_raster.hlsl",enhanced_raster?defines:nullptr,nullptr,entry,"cs_5_1",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&errors);
            if(FAILED(compile))throw std::runtime_error(std::string("D3D12 shader compilation (")+entry+"): "+(errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"unknown compiler failure"));
            D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=root_.Get();desc.CS={shader->GetBufferPointer(),shader->GetBufferSize()};checked(device_->CreateComputePipelineState(&desc,IID_PPV_ARGS(&state)),"compute pipeline");
        };
        pipeline("main",pipeline_);pipeline("antiAlias",anti_aliasing_pipeline_);pipeline("visualStyle",visual_style_pipeline_);
        pipeline("ambientLighting",ambient_pipeline_);pipeline("enhancedScene",enhanced_pipeline_);
        pipeline("main",enhanced_raster_pipeline_,true);
        const UINT64 raster_bytes=UINT64(width_)*height_*4;
        frame_=buffer(raster_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);depth_=buffer(raster_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
        capture_=buffer(raster_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);resolved_=buffer(output_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);readback_=buffer(raster_bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        void* mapped=nullptr;const D3D12_RANGE read{0,SIZE_T(raster_bytes)};checked(readback_->Map(0,&read,&mapped),"readback map");readback_data_=static_cast<const unsigned char*>(mapped);
        std::cout<<"Scene renderer: "<<label_<<" (drawable-resolution rasterization; "<<supersample_<<"x reference sampling)\n";
    }
    ~D3D12Renderer(){try{if(fence_&&queue_)wait();}catch(...){}const D3D12_RANGE no_write{0,0};if(readback_)readback_->Unmap(0,&no_write);if(upload_)upload_->Unmap(0,nullptr);}
    std::string_view name()const noexcept override{return label_;}
    AssetImage render(const SceneSubmission& submission)override{
        AssetImage result{};render_into(result,submission);return result;
    }
    void render_into(AssetImage& result,const SceneSubmission& submission)override{
        if(poisoned_)throw std::runtime_error("D3D12 renderer cannot continue after a GPU failure");
        auto size=scene_render_size();
        if(!submission.high_resolution||!size.first)size={640*supersample_,448*supersample_};
        if(width_!=size.first||height_!=size.second)resize(size.first,size.second);
        const auto style=scene_graphics_quality().visual_style;
        const bool enhanced=submission.high_resolution&&style==4;
        if(enhanced)ensure_enhanced();
        const auto& scene=scene_;
        // One reusable upload arena; no per-mesh resource/descriptor creation.
        struct Offsets{UINT64 triangles,tiles;};std::vector<Offsets> offsets;UINT64 bytes=0,texels=0;
        constexpr UINT64 limit=512*1024*1024;
        const auto reserve=[&](UINT64 size){
            if(size>limit)throw std::runtime_error("D3D12 scene upload exceeds 512 MiB safety limit");
            const auto padded=(std::max<UINT64>(size,256)+255)&~UINT64(255);
            if(bytes>limit-padded)throw std::runtime_error("D3D12 scene upload exceeds 512 MiB safety limit");
            const auto offset=bytes;bytes+=padded;return offset;
        };
        prepare_raster_scene(scene_,submission,std::min(float(width_)/640,float(height_)/448),width_,height_,[&](std::size_t words)->std::span<unsigned>{
            texels=reserve(words*4);
            for(const auto& batch:scene.batches)offsets.push_back({reserve(batch.triangles.size()*sizeof(RasterTriangle)),reserve(batch.tiles.size()*4)});
            wait();
            if(bytes>upload_capacity_){
                const auto capacity=std::min(limit,std::max<UINT64>(bytes,upload_capacity_*2));auto replacement=buffer(capacity,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
                void* mapped=nullptr;const D3D12_RANGE no_read{0,0};checked(replacement->Map(0,&no_read,&mapped),"upload map");
                if(upload_)upload_->Unmap(0,nullptr);upload_=std::move(replacement);upload_data_=static_cast<unsigned char*>(mapped);upload_capacity_=capacity;
            }
            return {reinterpret_cast<unsigned*>(upload_data_+texels),words};
        });
        for(std::size_t i=0;i<scene.batches.size();++i){const auto& batch=scene.batches[i];if(!batch.triangles.empty())std::memcpy(upload_data_+offsets[i].triangles,batch.triangles.data(),batch.triangles.size()*sizeof(RasterTriangle));std::memcpy(upload_data_+offsets[i].tiles,batch.tiles.data(),batch.tiles.size()*4);}
        checked(allocator_->Reset(),"allocator reset");checked(commands_->Reset(allocator_.Get(),enhanced?enhanced_raster_pipeline_.Get():pipeline_.Get()),"command reset");commands_->SetComputeRootSignature(root_.Get());
        const auto base=upload_->GetGPUVirtualAddress();commands_->SetComputeRootShaderResourceView(0,base+offsets.front().triangles);commands_->SetComputeRootShaderResourceView(1,base+texels);commands_->SetComputeRootShaderResourceView(2,base+offsets.front().tiles);commands_->SetComputeRootShaderResourceView(3,capture_->GetGPUVirtualAddress());commands_->SetComputeRootUnorderedAccessView(4,frame_->GetGPUVirtualAddress());commands_->SetComputeRootUnorderedAccessView(5,depth_->GetGPUVirtualAddress());commands_->SetComputeRootUnorderedAccessView(6,resolved_->GetGPUVirtualAddress());
        const bool initialize_first=!scene.batches.front().capture&&!scene.batches.front().triangles.empty();
        commands_->SetComputeRootUnorderedAccessView(10,(enhanced?surface_:frame_)->GetGPUVirtualAddress());
        unsigned constants[]{0,scene.clear,scene.shadow_offset,(submission.high_resolution?1u:0u)|(enhanced?2u:0u),scene.width,scene.height,scene.tile_columns,supersample_,0,0,0,0,0,0,0,0};commands_->SetComputeRoot32BitConstants(7,16,constants,0);
        if(!initialize_first){commands_->Dispatch(scene.tile_columns,scene.tile_rows,1);order();}
        for(std::size_t i=0;i<scene.batches.size();++i){const auto& batch=scene.batches[i];
            if(batch.capture){transition(frame_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);transition(capture_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);commands_->CopyResource(capture_.Get(),frame_.Get());transition(capture_.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);transition(frame_.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
            if(batch.triangles.empty())continue;
            constants[0]=i==0&&initialize_first?3:1;commands_->SetComputeRoot32BitConstants(7,1,constants,0);
            commands_->SetComputeRootShaderResourceView(0,base+offsets[i].triangles);commands_->SetComputeRootShaderResourceView(2,base+offsets[i].tiles);commands_->Dispatch(scene.tile_columns,scene.tile_rows,1);order();
        }
        const unsigned output_width=submission.high_resolution?width_:640,output_height=submission.high_resolution?height_:448;
        const UINT64 bytes_out=UINT64(output_width)*output_height*4;
        const bool anti_aliasing=scene_graphics_quality().anti_aliasing&&width_>=3&&height_>=3;
        const bool filtered=submission.high_resolution&&(anti_aliasing||style);
        if(filtered){
            // Raster depth is dead now. Filter the immutable frame into that
            // equally sized buffer, preserving captures without a frame copy.
            transition(frame_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            commands_->SetComputeRootShaderResourceView(3,frame_->GetGPUVirtualAddress());
            if(enhanced){
                transition(depth_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                transition(surface_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                commands_->SetComputeRootShaderResourceView(8,depth_->GetGPUVirtualAddress());commands_->SetComputeRootShaderResourceView(11,surface_->GetGPUVirtualAddress());
                commands_->SetComputeRootUnorderedAccessView(4,enhanced_frame_->GetGPUVirtualAddress());commands_->SetComputeRootUnorderedAccessView(6,ambient_->GetGPUVirtualAddress());
                const auto view=enhanced_view(submission.camera,width_,height_);constants[3]=style|(anti_aliasing?16u:0u);constants[7]=std::bit_cast<unsigned>(view.focal);
                const float lighting[]{view.key.x,view.key.y,view.key.z,.85f,view.fill.x,view.fill.y,view.fill.z,.16f};std::memcpy(constants+8,lighting,sizeof(lighting));commands_->SetComputeRoot32BitConstants(7,16,constants,0);
                commands_->SetPipelineState(ambient_pipeline_.Get());commands_->Dispatch(((width_+1)/2+15)/16,((height_+1)/2+15)/16,1);order();
                transition(ambient_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);commands_->SetComputeRootShaderResourceView(9,ambient_->GetGPUVirtualAddress());
                commands_->SetPipelineState(enhanced_pipeline_.Get());commands_->Dispatch(scene.tile_columns,scene.tile_rows,1);order();
                transition(ambient_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                transition(surface_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                transition(depth_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }else{
                commands_->SetComputeRootUnorderedAccessView(4,depth_->GetGPUVirtualAddress());
                if(style){const unsigned parameters=style|(anti_aliasing?16u:0u);commands_->SetComputeRoot32BitConstants(7,1,&parameters,3);}
                commands_->SetPipelineState(style?visual_style_pipeline_.Get():anti_aliasing_pipeline_.Get());commands_->Dispatch(scene.tile_columns,scene.tile_rows,1);order();
            }
            transition(frame_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        if(!submission.high_resolution){constants[0]=2;commands_->SetComputeRoot32BitConstants(7,8,constants,0);commands_->Dispatch(40,28,1);order();}
        auto* output=enhanced?enhanced_frame_.Get():filtered?depth_.Get():submission.high_resolution?frame_.Get():resolved_.Get();
        transition(output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);commands_->CopyBufferRegion(readback_.Get(),0,output,0,bytes_out);transition(output,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);checked(commands_->Close(),"command close");
        ID3D12CommandList* lists[]{commands_.Get()};queue_->ExecuteCommandLists(1,lists);
        try{checked(queue_->Signal(fence_.Get(),++serial_),"queue signal");wait();}catch(...){poisoned_=true;throw;}
        result.rgba.resize(std::size_t(bytes_out));
        if(bytes_out>=4*1024*1024){
            const std::array<unsigned,4> chunks{0,1,2,3};const auto chunk=(std::size_t(bytes_out)/chunks.size())&~std::size_t(63);
            std::for_each(std::execution::par,chunks.begin(),chunks.end(),[&](unsigned i){const auto start=i*chunk,end=i+1==chunks.size()?std::size_t(bytes_out):start+chunk;std::memcpy(result.rgba.data()+start,readback_data_+start,end-start);});
        }else std::memcpy(result.rgba.data(),readback_data_,std::size_t(bytes_out));
        result.name="D3D12 scene";result.width=output_width;result.height=output_height;
    }
};
}
std::unique_ptr<SceneRenderer> make_d3d12_renderer(bool debug,unsigned supersample){return std::make_unique<D3D12Renderer>(debug,supersample);}
}
