#pragma once

#include "../../Common/d3dUtil.h"
#include "../../Common/UploadBuffer.h"

struct ParticleGpu
{
    DirectX::XMFLOAT3 Position = {0,0,0};
    float Age = 0.0f;
    DirectX::XMFLOAT3 Velocity = {0,0,0};
    float Lifetime = 4.0f;
    DirectX::XMFLOAT4 Color = {1,1,1,1};
    float Size = 0.25f;
    float Seed = 0.0f;
    DirectX::XMFLOAT2 Pad = {0,0};
};

struct ParticleRenderConstants
{
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 CameraRight = {1,0,0}; float Pad0 = 0.0f;
    DirectX::XMFLOAT3 CameraUp = {0,1,0}; float Pad1 = 0.0f;
};

class ParticleSystem
{
public:
    static const UINT MaxParticles = 2048;

    void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                    DXGI_FORMAT backBufferFormat, DXGI_FORMAT depthFormat);
    void Update(ID3D12GraphicsCommandList* cmdList, float deltaTime, float totalTime);
    void Render(ID3D12GraphicsCommandList* cmdList,
                const DirectX::XMFLOAT4X4& view,
                const DirectX::XMFLOAT4X4& proj,
                D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                D3D12_CPU_DESCRIPTOR_HANDLE dsv);

private:
    void BuildResources(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    void BuildDescriptors(ID3D12Device* device);
    void BuildRootSignatures(ID3D12Device* device);
    void BuildShaders();
    void BuildPSOs(ID3D12Device* device, DXGI_FORMAT backBufferFormat, DXGI_FORMAT depthFormat);
    void TransitionActiveToUav(ID3D12GraphicsCommandList* cmdList);
    void TransitionActiveToSrv(ID3D12GraphicsCommandList* cmdList);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> mParticles[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> mCounters[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> mParticleUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> mCounterUpload;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mHeap;
    UINT mDescriptorSize = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mComputeRootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRenderRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mComputePSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mRenderPSO;

    Microsoft::WRL::ComPtr<ID3DBlob> mCS;
    Microsoft::WRL::ComPtr<ID3DBlob> mVS;
    Microsoft::WRL::ComPtr<ID3DBlob> mGS;
    Microsoft::WRL::ComPtr<ID3DBlob> mPS;

    std::unique_ptr<UploadBuffer<ParticleRenderConstants>> mRenderCB;
    int mActiveBuffer = 0;
    bool mActiveIsSrv = false;
};
