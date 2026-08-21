#include "Gbuffer.h"

using Microsoft::WRL::ComPtr;

void Gbuffer::Initialize(
    ID3D12Device* device,
    UINT width,
    UINT height)
{
    mWidth = width;
    mHeight = height;

    BuildResources(device, width, height);
    BuildDescriptors(device);
}

void Gbuffer::Resize(
    ID3D12Device* device,
    UINT width,
    UINT height)
{
    if (width == 0 || height == 0)
        return;

    mWidth = width;
    mHeight = height;

    mPosition.Reset();
    mNormal.Reset();
    mAlbedo.Reset();
    mDepth.Reset();

    BuildResources(device, width, height);
    BuildDescriptors(device);
}

void Gbuffer::BuildResources(
    ID3D12Device* device,
    UINT width,
    UINT height)
{
    // Heap, в котором будут находиться текстуры G-Buffer
    CD3DX12_HEAP_PROPERTIES heapProperties(
        D3D12_HEAP_TYPE_DEFAULT);

    // position
    auto positionDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        width,
        height,
        1,
        1,
        1,
        0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    D3D12_CLEAR_VALUE positionClear = {};
    positionClear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    positionClear.Color[0] = 0.0f;
    positionClear.Color[1] = 0.0f;
    positionClear.Color[2] = 0.0f;
    positionClear.Color[3] = 0.0f;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &positionDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &positionClear,
        IID_PPV_ARGS(&mPosition)));

    // normal
    auto normalDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        width,
        height,
        1,
        1,
        1,
        0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    D3D12_CLEAR_VALUE normalClear = {};
    normalClear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    normalClear.Color[0] = 0.0f;
    normalClear.Color[1] = 0.0f;
    normalClear.Color[2] = 0.0f;
    normalClear.Color[3] = 0.0f;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &normalDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &normalClear,
        IID_PPV_ARGS(&mNormal)));

    // albedo
    auto albedoDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        width,
        height,
        1,
        1,
        1,
        0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    D3D12_CLEAR_VALUE albedoClear = {};
    albedoClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    albedoClear.Color[0] = 0.0f;
    albedoClear.Color[1] = 0.0f;
    albedoClear.Color[2] = 0.0f;
    albedoClear.Color[3] = 0.0f;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &albedoDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &albedoClear,
        IID_PPV_ARGS(&mAlbedo)));

    // depth
    auto depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        width,
        height,
        1,
        1,
        1,
        0,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthClear.DepthStencil.Depth = 1.0f;
    depthClear.DepthStencil.Stencil = 0;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClear,
        IID_PPV_ARGS(&mDepth)));
}

void Gbuffer::BuildDescriptors(
    ID3D12Device* device)
{

    // rtv
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 3;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ThrowIfFailed(device->CreateDescriptorHeap(
        &rtvHeapDesc,
        IID_PPV_ARGS(&mRtvHeap)));

    UINT rtvSize =
        device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    mPositionRTV =
        mRtvHeap->GetCPUDescriptorHandleForHeapStart();

    mNormalRTV = mPositionRTV;
    mNormalRTV.ptr += rtvSize;

    mAlbedoRTV = mNormalRTV;
    mAlbedoRTV.ptr += rtvSize;

    device->CreateRenderTargetView(
        mPosition.Get(),
        nullptr,
        mPositionRTV);

    device->CreateRenderTargetView(
        mNormal.Get(),
        nullptr,
        mNormalRTV);

    device->CreateRenderTargetView(
        mAlbedo.Get(),
        nullptr,
        mAlbedoRTV);

    // dsv
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ThrowIfFailed(device->CreateDescriptorHeap(
        &dsvHeapDesc,
        IID_PPV_ARGS(&mDsvHeap)));

    mDepthDSV =
        mDsvHeap->GetCPUDescriptorHandleForHeapStart();

    device->CreateDepthStencilView(
        mDepth.Get(),
        nullptr,
        mDepthDSV);
}