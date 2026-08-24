#include "RenderingSystem.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

void RenderingSystem::Initialize(
    ID3D12Device* device,
    DXGI_FORMAT backBufferFormat,
    UINT width,
    UINT height)
{
    mBackBufferFormat = backBufferFormat;

    mGBuffer.Initialize(device, width, height);

    BuildShaders();
    BuildGeometryRootSignature(device);
    BuildLightingRootSignature(device);
    BuildPSOs(device, backBufferFormat);

    mLightingCB = std::make_unique<UploadBuffer<LightingPassConstants>>(
        device, 1, true);

    BuildLightingDescriptors(device);
}

void RenderingSystem::OnResize(
    ID3D12Device* device,
    UINT width,
    UINT height)
{
    mGBuffer.Resize(device, width, height);
    BuildLightingDescriptors(device);
}

void RenderingSystem::BeginGeometryPass(ID3D12GraphicsCommandList* cmdList)
{
    mGBuffer.Clear(cmdList);
    mGBuffer.SetAsRenderTargets(cmdList);

    cmdList->SetGraphicsRootSignature(mGeometryRootSignature.Get());
    cmdList->SetPipelineState(mGeometryPSO.Get());
}

void RenderingSystem::EndGeometryPass(ID3D12GraphicsCommandList* cmdList)
{
    mGBuffer.TransitionToShaderResource(cmdList);
}

void RenderingSystem::UpdateLights(
    const XMFLOAT3& eyePosW,
    const XMFLOAT3& ambientLight,
    const DeferredLight* lights,
    int numLights)
{
    LightingPassConstants passConstants;
    passConstants.EyePosW = eyePosW;
    passConstants.AmbientLight = ambientLight;
    passConstants.NumLights = MathHelper::Clamp(numLights, 0, MaxDeferredLights);

    for (int i = 0; i < passConstants.NumLights; ++i)
        passConstants.Lights[i] = lights[i];

    mLightingCB->CopyData(0, passConstants);
}

void RenderingSystem::ExecuteLightingPass(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv)
{
    cmdList->OMSetRenderTargets(1, &backBufferRtv, true, nullptr);

    ID3D12DescriptorHeap* heaps[] = { mLightingHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetGraphicsRootSignature(mLightingRootSignature.Get());
    cmdList->SetPipelineState(mLightingPSO.Get());

    CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle(
        mLightingHeap->GetGPUDescriptorHandleForHeapStart());
    cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);

    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(
        mLightingHeap->GetGPUDescriptorHandleForHeapStart(),
        1,
        mCbvSrvDescriptorSize);
    cmdList->SetGraphicsRootDescriptorTable(1, srvHandle);

    cmdList->IASetVertexBuffers(0, 0, nullptr);
    cmdList->IASetIndexBuffer(nullptr);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);

    mGBuffer.TransitionToRenderTarget(cmdList);
}

void RenderingSystem::BuildGeometryRootSignature(ID3D12Device* device)
{
    CD3DX12_ROOT_PARAMETER slotRootParameter[5];

    CD3DX12_DESCRIPTOR_RANGE cbvTable;
    cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

    CD3DX12_DESCRIPTOR_RANGE diffuseTable;
    diffuseTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    slotRootParameter[1].InitAsDescriptorTable(1, &diffuseTable);

    CD3DX12_DESCRIPTOR_RANGE normalTable;
    normalTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    slotRootParameter[2].InitAsDescriptorTable(1, &normalTable);

    CD3DX12_DESCRIPTOR_RANGE displacementTable;
    displacementTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
    slotRootParameter[3].InitAsDescriptorTable(1, &displacementTable);

    slotRootParameter[4].InitAsConstants(2, 1, 0);

    CD3DX12_STATIC_SAMPLER_DESC samplerDesc(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        5, slotRootParameter, 1, &samplerDesc,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());

    ThrowIfFailed(hr);

    ThrowIfFailed(device->CreateRootSignature(
        0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mGeometryRootSignature)));
}

void RenderingSystem::BuildLightingRootSignature(ID3D12Device* device)
{
    CD3DX12_ROOT_PARAMETER slotRootParameter[2];

    CD3DX12_DESCRIPTOR_RANGE cbvTable;
    cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

    CD3DX12_DESCRIPTOR_RANGE srvTable;
    srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);
    slotRootParameter[1].InitAsDescriptorTable(1, &srvTable);

    CD3DX12_STATIC_SAMPLER_DESC samplerDesc(
        0,
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        2,
        slotRootParameter,
        1,
        &samplerDesc,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(),
        errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());

    ThrowIfFailed(hr);

    ThrowIfFailed(device->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mLightingRootSignature)));
}

void RenderingSystem::BuildShaders()
{
    mGeometryVS = d3dUtil::CompileShader(
        L"../../Assets/shaders/gbuffer.hlsl", nullptr, "VS", "vs_5_0");

    mGeometryHS = d3dUtil::CompileShader(
        L"../../Assets/shaders/gbuffer.hlsl", nullptr, "HS", "hs_5_0");

    mGeometryDS = d3dUtil::CompileShader(
        L"../../Assets/shaders/gbuffer.hlsl", nullptr, "DS", "ds_5_0");

    mGeometryPS = d3dUtil::CompileShader(
        L"../../Assets/shaders/gbuffer.hlsl", nullptr, "PS", "ps_5_0");

    mLightingVS = d3dUtil::CompileShader(
        L"../../Assets/shaders/deferred_light.hlsl", nullptr, "VS", "vs_5_0");
    mLightingPS = d3dUtil::CompileShader(
        L"../../Assets/shaders/deferred_light.hlsl", nullptr, "PS", "ps_5_0");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void RenderingSystem::BuildPSOs(
    ID3D12Device* device,
    DXGI_FORMAT backBufferFormat)
{
    // Geometry pass -> MRT G-Buffer
    D3D12_GRAPHICS_PIPELINE_STATE_DESC geoPsoDesc;
    ZeroMemory(&geoPsoDesc, sizeof(geoPsoDesc));
    geoPsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    geoPsoDesc.pRootSignature = mGeometryRootSignature.Get();
    geoPsoDesc.VS = {
        reinterpret_cast<BYTE*>(mGeometryVS->GetBufferPointer()),
        mGeometryVS->GetBufferSize()
    };
    geoPsoDesc.HS = {
    reinterpret_cast<BYTE*>(mGeometryHS->GetBufferPointer()),
    mGeometryHS->GetBufferSize()
    };

    geoPsoDesc.DS = {
        reinterpret_cast<BYTE*>(mGeometryDS->GetBufferPointer()),
        mGeometryDS->GetBufferSize()
    };
    geoPsoDesc.PS = {
        reinterpret_cast<BYTE*>(mGeometryPS->GetBufferPointer()),
        mGeometryPS->GetBufferSize()
    };
    geoPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    geoPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    geoPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    geoPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    geoPsoDesc.SampleMask = UINT_MAX;
    geoPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    geoPsoDesc.NumRenderTargets = 3;
    geoPsoDesc.RTVFormats[0] = Gbuffer::PositionFormat;
    geoPsoDesc.RTVFormats[1] = Gbuffer::NormalFormat;
    geoPsoDesc.RTVFormats[2] = Gbuffer::AlbedoFormat;
    geoPsoDesc.SampleDesc.Count = 1;
    geoPsoDesc.SampleDesc.Quality = 0;
    geoPsoDesc.DSVFormat = Gbuffer::DepthFormat;

    ThrowIfFailed(device->CreateGraphicsPipelineState(
        &geoPsoDesc, IID_PPV_ARGS(&mGeometryPSO)));

    // Lighting pass -> back buffer (no depth)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC lightPsoDesc;
    ZeroMemory(&lightPsoDesc, sizeof(lightPsoDesc));
    lightPsoDesc.InputLayout = { nullptr, 0 };
    lightPsoDesc.pRootSignature = mLightingRootSignature.Get();
    lightPsoDesc.VS = {
        reinterpret_cast<BYTE*>(mLightingVS->GetBufferPointer()),
        mLightingVS->GetBufferSize()
    };
    lightPsoDesc.PS = {
        reinterpret_cast<BYTE*>(mLightingPS->GetBufferPointer()),
        mLightingPS->GetBufferSize()
    };
    lightPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    lightPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    lightPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    lightPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    lightPsoDesc.DepthStencilState.DepthEnable = FALSE;
    lightPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    lightPsoDesc.SampleMask = UINT_MAX;
    lightPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    lightPsoDesc.NumRenderTargets = 1;
    lightPsoDesc.RTVFormats[0] = backBufferFormat;
    lightPsoDesc.SampleDesc.Count = 1;
    lightPsoDesc.SampleDesc.Quality = 0;
    lightPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

    ThrowIfFailed(device->CreateGraphicsPipelineState(
        &lightPsoDesc, IID_PPV_ARGS(&mLightingPSO)));
}

void RenderingSystem::BuildLightingDescriptors(ID3D12Device* device)
{
    mCbvSrvDescriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 4; // CBV + Position + Normal + Albedo
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed(device->CreateDescriptorHeap(
        &heapDesc, IID_PPV_ARGS(&mLightingHeap)));

    // [0] Lighting CBV
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = mLightingCB->Resource()->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(
        sizeof(LightingPassConstants));

    device->CreateConstantBufferView(
        &cbvDesc,
        mLightingHeap->GetCPUDescriptorHandleForHeapStart());

    // [1..3] G-Buffer SRVs
    CD3DX12_CPU_DESCRIPTOR_HANDLE positionSrv(
        mLightingHeap->GetCPUDescriptorHandleForHeapStart(),
        1,
        mCbvSrvDescriptorSize);

    CD3DX12_CPU_DESCRIPTOR_HANDLE normalSrv = positionSrv;
    normalSrv.Offset(1, mCbvSrvDescriptorSize);

    CD3DX12_CPU_DESCRIPTOR_HANDLE albedoSrv = normalSrv;
    albedoSrv.Offset(1, mCbvSrvDescriptorSize);

    mGBuffer.BuildShaderResourceViews(
        device,
        positionSrv,
        normalSrv,
        albedoSrv);
}
