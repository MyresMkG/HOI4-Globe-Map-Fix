#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <vector>
#include "test_support.hpp"

template<class T>struct Com {
    T* p=nullptr;~Com(){if(p)p->Release();}
    T** addr(){return &p;}T* operator->(){return p;}
};
bool good(HRESULT hr,const char* what){if(FAILED(hr)){std::cerr<<what<<" HRESULT 0x"<<std::hex<<hr<<std::dec<<'\n';return false;}return true;}
struct Config {float width,height,water;unsigned count;gmf::Vec3 eye;float pad0;gmf::Vec3 direction;float pad1;};
struct Output {gmf::Vec3 position;float visibility;float normal[4];};
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    std::ifstream f(std::filesystem::path(argv[1]),std::ios::binary);
    std::string source((std::istreambuf_iterator<char>(f)),{});
    auto begin=source.find("[["),end=source.rfind("]]");
    if(begin==source.npos||end==source.npos||end<=begin)return 3;
    // Compile the user's actual shader as a compute oracle; never ship its text.
    std::string shader=R"(
cbuffer Params:register(b0){float2 Dimensions;float Water;uint Count;float3 vCamPos;float pad0;float3 vCamLookAtDir;float pad1;};
#define MAP_SIZE_X Dimensions.x
#define MAP_SIZE_Y Dimensions.y
#define WATER_HEIGHT Water
StructuredBuffer<float4> Points:register(t0);
struct Result{float4 position;float4 normal;};
RWStructuredBuffer<Result> Results:register(u0);
)"+source.substr(begin+2,end-begin-2)+R"(
[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){if(id.x>=Count)return;float3 p=Points[id.x].xyz;Result r;r.position=float4(GlobeWarp(p),GlobeVisible(p));r.normal=float4(GlobeNormal(p),0);Results[id.x]=r;}
)";
    Com<ID3DBlob> code,errors;
    HRESULT hr=D3DCompile(shader.data(),shader.size(),"local Globe Mod",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.addr(),errors.addr());
    if(FAILED(hr)){if(errors.p)std::cerr<<static_cast<const char*>(errors->GetBufferPointer());return 4;}
    Com<ID3D11Device> device;Com<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
    if(!good(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,device.addr(),&level,context.addr()),"WARP device"))return 5;
    Com<ID3D11ComputeShader> cs;if(!good(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,cs.addr()),"compute shader"))return 6;
    constexpr unsigned count=4096;
    struct Point {gmf::Vec3 p;float pad;};std::vector<Point> points(count);
    std::mt19937 rng(271828);std::uniform_real_distribution<float> x(-5600,11000),z(-10,2058),alt(0,40);
    for(auto& point:points)point={gmf::Vec3{x(rng),alt(rng),z(rng)},0};
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(Point)*count;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=sizeof(Point);
    D3D11_SUBRESOURCE_DATA init{};init.pSysMem=points.data();Com<ID3D11Buffer> input;
    if(!good(device->CreateBuffer(&desc,&init,input.addr()),"input"))return 7;
    Com<ID3D11ShaderResourceView> srv;if(!good(device->CreateShaderResourceView(input.p,nullptr,srv.addr()),"SRV"))return 8;
    desc.ByteWidth=sizeof(Output)*count;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;desc.StructureByteStride=sizeof(Output);Com<ID3D11Buffer> output;
    if(!good(device->CreateBuffer(&desc,nullptr,output.addr()),"output"))return 9;
    Com<ID3D11UnorderedAccessView> uav;if(!good(device->CreateUnorderedAccessView(output.p,nullptr,uav.addr()),"UAV"))return 10;
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;desc.StructureByteStride=0;Com<ID3D11Buffer> staging;
    if(!good(device->CreateBuffer(&desc,nullptr,staging.addr()),"staging"))return 11;
    desc={};desc.ByteWidth=sizeof(Config);desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;Com<ID3D11Buffer> uniform;
    if(!good(device->CreateBuffer(&desc,nullptr,uniform.addr()),"uniform"))return 12;
    context->CSSetShader(cs.p,nullptr,0);context->CSSetShaderResources(0,1,&srv.p);context->CSSetUnorderedAccessViews(0,1,&uav.p,nullptr);context->CSSetConstantBuffers(0,1,&uniform.p);
    gmf::Settings settings;double max_error=0,max_eye_error=0,max_pixel_error=0;unsigned comparisons=0,failures=0,visibility_failures=0,pixel_failures=0;
    for(float camera_height:{800.f,999.f,1000.f,1000.1f,1002.f,1010.f,1020.f,1050.f,1100.f,1200.f,1399.f,1400.f,1500.f,1700.f,1799.f,1800.f,2000.f,2400.f}) {
        for(auto direction:{gmf::Vec3{0,-1,0.15f},gmf::Vec3{0.4f,-1,-0.2f},gmf::Vec3{-0.6f,-1,0.8f}}) {
            gmf::Vec3 eye{2800,camera_height,1200};auto matrix=perspective(eye,direction);auto view=gmf::prepare(matrix.data(),settings);
            double eye_error=std::max({double(std::abs(view.eye.x-eye.x)),double(std::abs(view.eye.y-eye.y)),double(std::abs(view.eye.z-eye.z))});max_eye_error=std::max(max_eye_error,eye_error);
            Config cfg{settings.width,settings.height,settings.water,count,eye,0,direction,0};context->UpdateSubresource(uniform.p,0,nullptr,&cfg,0,0);
            context->Dispatch((count+63)/64,1,1);context->CopyResource(staging.p,output.p);
            D3D11_MAPPED_SUBRESOURCE mapped{};if(!good(context->Map(staging.p,0,D3D11_MAP_READ,0,&mapped),"map results"))return 13;
            const auto* gpu=static_cast<const Output*>(mapped.pData);
            double frame_max=0;
            for(unsigned i=0;i<count;++i) {
                auto cpu=gmf::warp(points[i].p,view,settings);
                double e=std::max({double(std::abs(cpu.position.x-gpu[i].position.x)),double(std::abs(cpu.position.y-gpu[i].position.y)),double(std::abs(cpu.position.z-gpu[i].position.z))});
                max_error=std::max(max_error,e);frame_max=std::max(frame_max,e);++comparisons;
                // CPU/GPU trig/rsqrt can differ by ULPs, magnified by the very
                // large radius at the start of the transition. Also verify the
                // actual screen-space error for visible, on-screen anchors.
                if(e>0.1||!std::isfinite(e))++failures;
                bool visible=gpu[i].visibility>=0;
                if(visible!=cpu.visible && std::abs(gpu[i].visibility)>0.0002f)++visibility_failures;
                if(visible && cpu.visible) {
                    auto p=gpu[i].position;float w=matrix[3]*p.x+matrix[7]*p.y+matrix[11]*p.z+matrix[15];
                    if(w>0.01f) {
                        int viewport[2]={1920,1080};gmf::Vec2 a,b;
                        project_reference(matrix.data(),&p,&a,viewport);project_reference(matrix.data(),&cpu.position,&b,viewport);
                        if(a.x>=0&&a.x<=1920&&a.y>=0&&a.y<=1080) {
                            double pe=std::max(double(std::abs(a.x-b.x)),double(std::abs(a.y-b.y)));
                            max_pixel_error=std::max(max_pixel_error,pe);if(pe>0.25)++pixel_failures;
                        }
                    }
                }
            }
            context->Unmap(staging.p,0);
            if(frame_max>0.1)std::cout<<"height="<<camera_height<<" max coordinate error="<<frame_max<<'\n';
        }
    }
    std::cout<<"Actual HLSL/WARP comparisons="<<comparisons<<" max_world_error="<<max_error<<" max_eye_recovery_error="<<max_eye_error<<" max_visible_pixel_error="<<max_pixel_error<<" position_failures="<<failures<<" visibility_failures="<<visibility_failures<<" pixel_failures="<<pixel_failures<<'\n';
    return failures||visibility_failures||pixel_failures?14:0;
}
