#include "Gbuffer.h"

using Microsoft::WRL::ComPtr;

void Gbuffer::Initialize(ID3D12Device* device, UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;

    BuildResources(device, width, height);
    BuildDescriptors(device);
}

void Gbuffer::Resize(ID3D12Device* device, UINT width, UINT height)
{
    if (width == 0 || height == 0)
        return;

    if (width == mWidth && height == mHeight)
        return;

    mWidth = width;
    mHeight = height;

    mNormal.Reset();
    mAlbedo.Reset();
    mDepth.Reset();

    BuildResources(device, width, height);
    BuildDescriptors(device);
}

void Gbuffer::BuildShaderResourceViews(
    ID3D12Device* device,
    D3D12_CPU_DESCRIPTOR_HANDLE depthSrv,
    D3D12_CPU_DESCRIPTOR_HANDLE normalSrv,
    D3D12_CPU_DESCRIPTOR_HANDLE albedoSrv)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    srvDesc.Format = DepthSrvFormat;
    device->CreateShaderResourceView(mDepth.Get(), &srvDesc, depthSrv);

    srvDesc.Format = NormalFormat;
    device->CreateShaderResourceView(mNormal.Get(), &srvDesc, normalSrv);

    srvDesc.Format = AlbedoFormat;
    device->CreateShaderResourceView(mAlbedo.Get(), &srvDesc, albedoSrv);
}

void Gbuffer::Clear(ID3D12GraphicsCommandList* cmdList)
{
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };

    cmdList->ClearRenderTargetView(mNormalRTV, clearColor, 0, nullptr);
    cmdList->ClearRenderTargetView(mAlbedoRTV, clearColor, 0, nullptr);
    cmdList->ClearDepthStencilView( mDepthDSV, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
}

void Gbuffer::SetAsRenderTargets(ID3D12GraphicsCommandList* cmdList)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] =
    {
        mNormalRTV,
        mAlbedoRTV
    };

    cmdList->OMSetRenderTargets(2, rtvs, false, &mDepthDSV);
}

void Gbuffer::TransitionToShaderResource(ID3D12GraphicsCommandList* cmdList)
{
    CD3DX12_RESOURCE_BARRIER barriers[3] =
    {
        CD3DX12_RESOURCE_BARRIER::Transition(
            mDepth.Get(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),

        CD3DX12_RESOURCE_BARRIER::Transition(
            mNormal.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),

        CD3DX12_RESOURCE_BARRIER::Transition(
            mAlbedo.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    };

    cmdList->ResourceBarrier(3, barriers);
}

void Gbuffer::TransitionToRenderTarget(ID3D12GraphicsCommandList* cmdList)
{
    CD3DX12_RESOURCE_BARRIER barriers[3] =
    {
        CD3DX12_RESOURCE_BARRIER::Transition(
            mDepth.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE),

        CD3DX12_RESOURCE_BARRIER::Transition(
            mNormal.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET),

        CD3DX12_RESOURCE_BARRIER::Transition(
            mAlbedo.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET)
    };

    cmdList->ResourceBarrier(3, barriers);
}

void Gbuffer::BuildResources(
    ID3D12Device* device,
    UINT width,
    UINT height)
{
    CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);

    auto normalDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        NormalFormat,
        width,
        height,
        1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    D3D12_CLEAR_VALUE normalClear = {};

    normalClear.Format = NormalFormat;
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

    auto albedoDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        AlbedoFormat,
        width,
        height,
        1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    D3D12_CLEAR_VALUE albedoClear = {};
    albedoClear.Format = AlbedoFormat;
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

    auto depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DepthResourceFormat,
        width,
        height,
        1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = DepthFormat;
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

void Gbuffer::BuildDescriptors(ID3D12Device* device)
{

    // rtv
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 2;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ThrowIfFailed(device->CreateDescriptorHeap(
        &rtvHeapDesc,
        IID_PPV_ARGS(&mRtvHeap)));

    UINT rtvSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    mNormalRTV = mRtvHeap->GetCPUDescriptorHandleForHeapStart();

    mAlbedoRTV = mNormalRTV;
    mAlbedoRTV.ptr += rtvSize;

    device->CreateRenderTargetView(mNormal.Get(), nullptr, mNormalRTV);
    device->CreateRenderTargetView(mAlbedo.Get(), nullptr, mAlbedoRTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ThrowIfFailed(device->CreateDescriptorHeap(
        &dsvHeapDesc,
        IID_PPV_ARGS(&mDsvHeap)));

    mDepthDSV = mDsvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DepthFormat;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(mDepth.Get(), &dsvDesc, mDepthDSV);
}
