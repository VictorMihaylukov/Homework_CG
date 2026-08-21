#pragma once

#include "Gbuffer.h"
#include "../../Common/UploadBuffer.h"

enum class LightType : int
{
    Directional = 0,
    Point = 1,
    Spot = 2
};

struct DeferredLight
{
    DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
    float FalloffStart = 1.0f;
    DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };
    float FalloffEnd = 10.0f;
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
    float SpotPower = 64.0f;
    int Type = static_cast<int>(LightType::Point);
    DirectX::XMFLOAT3 Pad = { 0.0f, 0.0f, 0.0f };
};

#define MaxDeferredLights 16

struct LightingPassConstants
{
    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    int NumLights = 0;
    DirectX::XMFLOAT3 AmbientLight = { 0.08f, 0.08f, 0.10f };
    float Pad0 = 0.0f;
    DeferredLight Lights[MaxDeferredLights];
};

class RenderingSystem
{
public:
    RenderingSystem() = default;
    ~RenderingSystem() = default;

    RenderingSystem(const RenderingSystem&) = delete;
    RenderingSystem& operator=(const RenderingSystem&) = delete;

    void Initialize(
        ID3D12Device* device,
        DXGI_FORMAT backBufferFormat,
        UINT width,
        UINT height);

    void OnResize(
        ID3D12Device* device,
        UINT width,
        UINT height);

    Gbuffer& GetGBuffer() { return mGBuffer; }

    ID3D12RootSignature* GeometryRootSignature() const { return mGeometryRootSignature.Get(); }
    ID3D12PipelineState* GeometryPSO() const { return mGeometryPSO.Get(); }

    ID3D12RootSignature* LightingRootSignature() const { return mLightingRootSignature.Get(); }
    ID3D12PipelineState* LightingPSO() const { return mLightingPSO.Get(); }

    ID3D12DescriptorHeap* LightingHeap() const { return mLightingHeap.Get(); }

    void BeginGeometryPass(ID3D12GraphicsCommandList* cmdList);
    void EndGeometryPass(ID3D12GraphicsCommandList* cmdList);

    void UpdateLights(
        const DirectX::XMFLOAT3& eyePosW,
        const DirectX::XMFLOAT3& ambientLight,
        const DeferredLight* lights,
        int numLights);

    void ExecuteLightingPass(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv);

private:
    void BuildGeometryRootSignature(ID3D12Device* device);
    void BuildLightingRootSignature(ID3D12Device* device);
    void BuildPSOs(ID3D12Device* device, DXGI_FORMAT backBufferFormat);
    void BuildLightingDescriptors(ID3D12Device* device);
    void BuildShaders();

private:
    Gbuffer mGBuffer;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mGeometryRootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mLightingRootSignature;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> mGeometryPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mLightingPSO;

    Microsoft::WRL::ComPtr<ID3DBlob> mGeometryVS;
    Microsoft::WRL::ComPtr<ID3DBlob> mGeometryPS;
    Microsoft::WRL::ComPtr<ID3DBlob> mLightingVS;
    Microsoft::WRL::ComPtr<ID3DBlob> mLightingPS;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mLightingHeap;
    std::unique_ptr<UploadBuffer<LightingPassConstants>> mLightingCB;

    UINT mCbvSrvDescriptorSize = 0;
    DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
};
