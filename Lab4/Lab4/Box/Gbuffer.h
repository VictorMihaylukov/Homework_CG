#pragma once

#include "../../Common/d3dUtil.h"

class Gbuffer
{
public:
    Gbuffer() = default;
    ~Gbuffer() = default;

    Gbuffer(const Gbuffer&) = delete;
    Gbuffer& operator=(const Gbuffer&) = delete;

    void Initialize(
        ID3D12Device* device,
        UINT width,
        UINT height);

    void Resize(
        ID3D12Device* device,
        UINT width,
        UINT height);

    ID3D12Resource* Position() const
    {
        return mPosition.Get();
    }

    ID3D12Resource* Normal() const
    {
        return mNormal.Get();
    }

    ID3D12Resource* Albedo() const
    {
        return mAlbedo.Get();
    }

    ID3D12Resource* Depth() const
    {
        return mDepth.Get();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE PositionRTV() const
    {
        return mPositionRTV;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE NormalRTV() const
    {
        return mNormalRTV;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE AlbedoRTV() const
    {
        return mAlbedoRTV;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DepthDSV() const
    {
        return mDepthDSV;
    }

private:
    void BuildResources(
        ID3D12Device* device,
        UINT width,
        UINT height);

    void BuildDescriptors(
        ID3D12Device* device);

private:
    UINT mWidth = 0;
    UINT mHeight = 0;

    // Текстуры G-Buffer
    Microsoft::WRL::ComPtr<ID3D12Resource> mPosition;
    Microsoft::WRL::ComPtr<ID3D12Resource> mNormal;
    Microsoft::WRL::ComPtr<ID3D12Resource> mAlbedo;
    Microsoft::WRL::ComPtr<ID3D12Resource> mDepth;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;

    D3D12_CPU_DESCRIPTOR_HANDLE mPositionRTV{};
    D3D12_CPU_DESCRIPTOR_HANDLE mNormalRTV{};
    D3D12_CPU_DESCRIPTOR_HANDLE mAlbedoRTV{};
    D3D12_CPU_DESCRIPTOR_HANDLE mDepthDSV{};
};