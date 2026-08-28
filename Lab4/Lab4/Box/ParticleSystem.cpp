#include "ParticleSystem.h"
#include <vector>
#include <cmath>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{
    ComPtr<ID3D12Resource> CreateUavBuffer(ID3D12Device* device, UINT64 byteSize,
                                          D3D12_RESOURCE_STATES initialState)
    {
        ComPtr<ID3D12Resource> resource;
        CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(byteSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            initialState, nullptr, IID_PPV_ARGS(&resource)));
        return resource;
    }

    ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device, UINT64 byteSize)
    {
        ComPtr<ID3D12Resource> resource;
        CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
        ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource)));
        return resource;
    }
}

void ParticleSystem::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                                DXGI_FORMAT backBufferFormat, DXGI_FORMAT depthFormat)
{
    BuildResources(device, cmdList);
    BuildDescriptors(device);
    BuildRootSignatures(device);
    BuildShaders();
    BuildPSOs(device, backBufferFormat, depthFormat);
    mRenderCB = std::make_unique<UploadBuffer<ParticleRenderConstants>>(device, 1, true);
}

void ParticleSystem::BuildResources(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    const UINT64 particleBytes = UINT64(MaxParticles) * sizeof(ParticleGpu);
    mParticles[0] = CreateUavBuffer(device, particleBytes, D3D12_RESOURCE_STATE_COPY_DEST);
    mParticles[1] = CreateUavBuffer(device, particleBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    mCounters[0] = CreateUavBuffer(device, sizeof(UINT), D3D12_RESOURCE_STATE_COPY_DEST);
    mCounters[1] = CreateUavBuffer(device, sizeof(UINT), D3D12_RESOURCE_STATE_COPY_DEST);

    std::vector<ParticleGpu> initial(MaxParticles);
    for (UINT i = 0; i < MaxParticles; ++i)
    {
        const float t = float(i) / float(MaxParticles);
        const float a = 6.2831853f * (t * 37.0f - floorf(t * 37.0f));
        const float r = 0.15f + 0.85f * (float((i * 73u) % 997u) / 997.0f);
        ParticleGpu p;
        p.Position = { cosf(a) * r, 0.35f + 3.0f * t, sinf(a) * r };
        p.Velocity = { cosf(a) * (0.25f + r), 2.4f + 2.2f * (1.0f - t), sinf(a) * (0.25f + r) };
        p.Age = 4.5f * t;
        p.Lifetime = 3.0f + 2.0f * (float((i * 47u) % 251u) / 251.0f);
        p.Color = { 1.0f, 0.35f + 0.55f * t, 0.08f, 1.0f };
        p.Size = 0.12f + 0.18f * (float((i * 31u) % 127u) / 127.0f);
        p.Seed = float(i) + 0.123f;
        initial[i] = p;
    }

    mParticleUpload = CreateUploadBuffer(device, particleBytes);
    void* mapped = nullptr;
    ThrowIfFailed(mParticleUpload->Map(0, nullptr, &mapped));
    memcpy(mapped, initial.data(), size_t(particleBytes));
    mParticleUpload->Unmap(0, nullptr);
    cmdList->CopyBufferRegion(mParticles[0].Get(), 0, mParticleUpload.Get(), 0, particleBytes);

    UINT counters[2] = { MaxParticles, 0 };
    mCounterUpload = CreateUploadBuffer(device, sizeof(counters));
    ThrowIfFailed(mCounterUpload->Map(0, nullptr, &mapped));
    memcpy(mapped, counters, sizeof(counters));
    mCounterUpload->Unmap(0, nullptr);
    cmdList->CopyBufferRegion(mCounters[0].Get(), 0, mCounterUpload.Get(), 0, sizeof(UINT));
    cmdList->CopyBufferRegion(mCounters[1].Get(), 0, mCounterUpload.Get(), sizeof(UINT), sizeof(UINT));

    CD3DX12_RESOURCE_BARRIER barriers[3] =
    {
        CD3DX12_RESOURCE_BARRIER::Transition(mParticles[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(mCounters[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(mCounters[1].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    };
    cmdList->ResourceBarrier(_countof(barriers), barriers);
}

void ParticleSystem::BuildDescriptors(ID3D12Device* device)
{
    mDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.NumDescriptors = 4;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&mHeap)));

    for (int i = 0; i < 2; ++i)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE uav(mHeap->GetCPUDescriptorHandleForHeapStart(), i, mDescriptorSize);
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {};
        ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        ud.Format = DXGI_FORMAT_UNKNOWN;
        ud.Buffer.FirstElement = 0;
        ud.Buffer.NumElements = MaxParticles;
        ud.Buffer.StructureByteStride = sizeof(ParticleGpu);
        ud.Buffer.CounterOffsetInBytes = 0;
        device->CreateUnorderedAccessView(mParticles[i].Get(), mCounters[i].Get(), &ud, uav);

        CD3DX12_CPU_DESCRIPTOR_HANDLE srv(mHeap->GetCPUDescriptorHandleForHeapStart(), 2 + i, mDescriptorSize);
        D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        sd.Format = DXGI_FORMAT_UNKNOWN;
        sd.Buffer.FirstElement = 0;
        sd.Buffer.NumElements = MaxParticles;
        sd.Buffer.StructureByteStride = sizeof(ParticleGpu);
        device->CreateShaderResourceView(mParticles[i].Get(), &sd, srv);
    }
}

void ParticleSystem::BuildRootSignatures(ID3D12Device* device)
{
    CD3DX12_DESCRIPTOR_RANGE inputRange, outputRange;
    inputRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    outputRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
    CD3DX12_ROOT_PARAMETER cp[3];
    cp[0].InitAsDescriptorTable(1, &inputRange);
    cp[1].InitAsDescriptorTable(1, &outputRange);
    cp[2].InitAsConstants(12, 0);
    CD3DX12_ROOT_SIGNATURE_DESC csDesc(3, cp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ComPtr<ID3DBlob> blob, errors;
    ThrowIfFailed(D3D12SerializeRootSignature(&csDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors));
    ThrowIfFailed(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&mComputeRootSignature)));

    CD3DX12_DESCRIPTOR_RANGE srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_ROOT_PARAMETER rp[2];
    rp[0].InitAsDescriptorTable(1, &srvRange);
    rp[1].InitAsConstantBufferView(0);
    CD3DX12_ROOT_SIGNATURE_DESC rsDesc(2, rp, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    blob.Reset(); errors.Reset();
    ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors));
    ThrowIfFailed(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&mRenderRootSignature)));
}

void ParticleSystem::BuildShaders()
{
    mCS = d3dUtil::CompileShader(L"../../Assets/shaders/particles_compute.hlsl", nullptr, "CS", "cs_5_0");
    mVS = d3dUtil::CompileShader(L"../../Assets/shaders/particles_render.hlsl", nullptr, "VS", "vs_5_0");
    mGS = d3dUtil::CompileShader(L"../../Assets/shaders/particles_render.hlsl", nullptr, "GS", "gs_5_0");
    mPS = d3dUtil::CompileShader(L"../../Assets/shaders/particles_render.hlsl", nullptr, "PS", "ps_5_0");
}

void ParticleSystem::BuildPSOs(ID3D12Device* device, DXGI_FORMAT backBufferFormat, DXGI_FORMAT depthFormat)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC cs = {};
    cs.pRootSignature = mComputeRootSignature.Get();
    cs.CS = { mCS->GetBufferPointer(), mCS->GetBufferSize() };
    ThrowIfFailed(device->CreateComputePipelineState(&cs, IID_PPV_ARGS(&mComputePSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC ps = {};
    ps.pRootSignature = mRenderRootSignature.Get();
    ps.VS = { mVS->GetBufferPointer(), mVS->GetBufferSize() };
    ps.GS = { mGS->GetBufferPointer(), mGS->GetBufferSize() };
    ps.PS = { mPS->GetBufferPointer(), mPS->GetBufferSize() };
    ps.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    ps.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ps.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    ps.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    ps.SampleMask = UINT_MAX;
    ps.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    ps.NumRenderTargets = 1;
    ps.RTVFormats[0] = backBufferFormat;
    ps.DSVFormat = depthFormat;
    ps.SampleDesc.Count = 1;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&ps, IID_PPV_ARGS(&mRenderPSO)));
}

void ParticleSystem::TransitionActiveToUav(ID3D12GraphicsCommandList* cmdList)
{
    if (!mActiveIsSrv) return;
    auto b = CD3DX12_RESOURCE_BARRIER::Transition(mParticles[mActiveBuffer].Get(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->ResourceBarrier(1, &b);
    mActiveIsSrv = false;
}

void ParticleSystem::TransitionActiveToSrv(ID3D12GraphicsCommandList* cmdList)
{
    if (mActiveIsSrv) return;
    auto b = CD3DX12_RESOURCE_BARRIER::Transition(mParticles[mActiveBuffer].Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &b);
    mActiveIsSrv = true;
}

void ParticleSystem::Update(ID3D12GraphicsCommandList* cmdList, float deltaTime, float totalTime)
{
    TransitionActiveToUav(cmdList);
    const int input = mActiveBuffer;
    const int output = 1 - input;

    ID3D12DescriptorHeap* heaps[] = { mHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetComputeRootSignature(mComputeRootSignature.Get());
    cmdList->SetPipelineState(mComputePSO.Get());

    CD3DX12_GPU_DESCRIPTOR_HANDLE inHandle(mHeap->GetGPUDescriptorHandleForHeapStart(), input, mDescriptorSize);
    CD3DX12_GPU_DESCRIPTOR_HANDLE outHandle(mHeap->GetGPUDescriptorHandleForHeapStart(), output, mDescriptorSize);
    cmdList->SetComputeRootDescriptorTable(0, inHandle);
    cmdList->SetComputeRootDescriptorTable(1, outHandle);

    struct ComputeConstants
    {
        float DeltaTime, TotalTime;
        XMFLOAT3 Emitter; float Drag;
        XMFLOAT3 Gravity; float Speed;
        float Pad[2];
    } c = { deltaTime, totalTime, {0.0f, 0.45f, 0.0f}, 0.12f, {0.0f,-1.9f,0.0f}, 1.0f, {0,0} };
    cmdList->SetComputeRoot32BitConstants(2, 12, &c, 0);

    const UINT groupSize = 128;
    cmdList->Dispatch((MaxParticles + groupSize - 1) / groupSize, 1, 1);

    D3D12_RESOURCE_BARRIER uav[2] =
    {
        CD3DX12_RESOURCE_BARRIER::UAV(mParticles[input].Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(mParticles[output].Get())
    };
    cmdList->ResourceBarrier(2, uav);

    mActiveBuffer = output;
    TransitionActiveToSrv(cmdList);
}

void ParticleSystem::Render(ID3D12GraphicsCommandList* cmdList,
                            const XMFLOAT4X4& view, const XMFLOAT4X4& proj,
                            D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                            D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
    XMMATRIX V = XMLoadFloat4x4(&view);
    XMMATRIX P = XMLoadFloat4x4(&proj);
    XMMATRIX invV = XMMatrixInverse(nullptr, V);

    ParticleRenderConstants c;
    XMStoreFloat4x4(&c.ViewProj, XMMatrixTranspose(V * P));
    XMStoreFloat3(&c.CameraRight, invV.r[0]);
    XMStoreFloat3(&c.CameraUp, invV.r[1]);
    mRenderCB->CopyData(0, c);

    ID3D12DescriptorHeap* heaps[] = { mHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRootSignature(mRenderRootSignature.Get());
    cmdList->SetPipelineState(mRenderPSO.Get());
    CD3DX12_GPU_DESCRIPTOR_HANDLE srv(mHeap->GetGPUDescriptorHandleForHeapStart(), 2 + mActiveBuffer, mDescriptorSize);
    cmdList->SetGraphicsRootDescriptorTable(0, srv);
    cmdList->SetGraphicsRootConstantBufferView(1, mRenderCB->Resource()->GetGPUVirtualAddress());

    cmdList->OMSetRenderTargets(1, &rtv, true, &dsv);
    cmdList->IASetVertexBuffers(0, 0, nullptr);
    cmdList->IASetIndexBuffer(nullptr);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    cmdList->DrawInstanced(MaxParticles, 1, 0, 0);
}
