#pragma once

#include "Gbuffer.h"
#include "../../Common/UploadBuffer.h"

enum class LightType : int { Directional = 0, Point = 1, Spot = 2 };

struct DeferredLight {
    DirectX::XMFLOAT3 Strength = {0.5f,0.5f,0.5f}; float FalloffStart=1.0f;
    DirectX::XMFLOAT3 Direction = {0,-1,0}; float FalloffEnd=10.0f;
    DirectX::XMFLOAT3 Position = {0,0,0}; float SpotPower=64.0f;
    int Type=(int)LightType::Point; DirectX::XMFLOAT3 Pad={0,0,0};
};

#define MaxDeferredLights 16
#define CascadeCount 4

struct LightingPassConstants {
    DirectX::XMFLOAT3 EyePosW={0,0,0}; int NumLights=0;
    DirectX::XMFLOAT3 AmbientLight={0.08f,0.08f,0.10f}; float Pad0=0;
    DeferredLight Lights[MaxDeferredLights];
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ShadowTransform[CascadeCount];
    DirectX::XMFLOAT4 CascadeSplits={10,30,100,300};
    UINT PostEffectFlags = 3;
    DirectX::XMFLOAT3 PostPad = {0,0,0};
};

class RenderingSystem {
public:
    void Initialize(ID3D12Device*,DXGI_FORMAT,UINT,UINT);
    void OnResize(ID3D12Device*,UINT,UINT);
    Gbuffer& GetGBuffer(){return mGBuffer;}
    ID3D12RootSignature* GeometryRootSignature()const{return mGeometryRootSignature.Get();}
    ID3D12PipelineState* GeometryPSO()const{return mGeometryPSO.Get();}
    void BeginGeometryPass(ID3D12GraphicsCommandList*); void EndGeometryPass(ID3D12GraphicsCommandList*);
    void BeginShadowPass(ID3D12GraphicsCommandList*);
    void BeginShadowCascade(ID3D12GraphicsCommandList*,int);
    void SetShadowWorldLightMatrix(ID3D12GraphicsCommandList*,const DirectX::XMFLOAT4X4&);
    void EndShadowPass(ID3D12GraphicsCommandList*);
    void UpdateLights(const DirectX::XMFLOAT3&,const DirectX::XMFLOAT3&,const DeferredLight*,int,
        const DirectX::XMFLOAT4X4&,const DirectX::XMFLOAT4X4*,const float*,UINT postEffectFlags);
    void ExecuteLightingPass(ID3D12GraphicsCommandList*,D3D12_CPU_DESCRIPTOR_HANDLE);
private:
    void BuildGeometryRootSignature(ID3D12Device*); void BuildLightingRootSignature(ID3D12Device*); void BuildShadowRootSignature(ID3D12Device*);
    void BuildPSOs(ID3D12Device*,DXGI_FORMAT); void BuildLightingDescriptors(ID3D12Device*); void BuildShadowResources(ID3D12Device*); void BuildShaders();
    Gbuffer mGBuffer;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mGeometryRootSignature,mLightingRootSignature,mShadowRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mGeometryPSO,mLightingPSO,mShadowPSO;
    Microsoft::WRL::ComPtr<ID3DBlob> mGeometryVS,mGeometryHS,mGeometryDS,mGeometryPS,mLightingVS,mLightingPS,mShadowVS;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mLightingHeap,mShadowDsvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMap;
    std::unique_ptr<UploadBuffer<LightingPassConstants>> mLightingCB;
    UINT mCbvSrvDescriptorSize=0, mDsvDescriptorSize=0; DXGI_FORMAT mBackBufferFormat=DXGI_FORMAT_R8G8B8A8_UNORM;
    static const UINT ShadowMapSize=2048;
    bool mShadowReadable=false;
};
