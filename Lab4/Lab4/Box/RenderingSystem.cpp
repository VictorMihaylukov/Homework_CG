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

    BuildGeometryRootSignature(device);
    BuildLightingRootSignature(device);
    BuildShadowRootSignature(device);
    BuildShaders();
    BuildPSOs(device, backBufferFormat);

    mLightingCB = std::make_unique<UploadBuffer<LightingPassConstants>>(
        device, 1, true);

    BuildShadowResources(device);
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
    const XMFLOAT3& eyePosW, const XMFLOAT3& ambientLight,
    const DeferredLight* lights, int numLights, const XMFLOAT4X4& view,
    const XMFLOAT4X4* shadowTransforms, const float* cascadeSplits, UINT postEffectFlags)
{
    LightingPassConstants c; c.EyePosW=eyePosW; c.AmbientLight=ambientLight;
    c.NumLights=MathHelper::Clamp(numLights,0,MaxDeferredLights);
    for(int i=0;i<c.NumLights;++i)c.Lights[i]=lights[i];
    c.View=view;
    for(int i=0;i<CascadeCount;++i)c.ShadowTransform[i]=shadowTransforms[i];
    c.CascadeSplits=XMFLOAT4(cascadeSplits[0],cascadeSplits[1],cascadeSplits[2],cascadeSplits[3]);
    c.PostEffectFlags = postEffectFlags;
    mLightingCB->CopyData(0,c);
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

    cmdList->DrawInstanced(6, 1, 0, 0);

    mGBuffer.TransitionToRenderTarget(cmdList);
}

void RenderingSystem::BuildGeometryRootSignature(ID3D12Device* device)
{
    CD3DX12_ROOT_PARAMETER slotRootParameter[5];

    slotRootParameter[0].InitAsConstantBufferView(0);

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
    srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);
    slotRootParameter[1].InitAsDescriptorTable(1, &srvTable);

    CD3DX12_STATIC_SAMPLER_DESC samplers[2];
    samplers[0] = CD3DX12_STATIC_SAMPLER_DESC(0,D3D12_FILTER_MIN_MAG_MIP_POINT,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    samplers[1] = CD3DX12_STATIC_SAMPLER_DESC(1,D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,D3D12_TEXTURE_ADDRESS_MODE_BORDER,D3D12_TEXTURE_ADDRESS_MODE_BORDER,D3D12_TEXTURE_ADDRESS_MODE_BORDER,0,16,D3D12_COMPARISON_FUNC_LESS_EQUAL,D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        2,
        slotRootParameter,
        2,
        samplers,
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
    mShadowVS = d3dUtil::CompileShader(L"../../Assets/shaders/shadow.hlsl", nullptr, "VS", "vs_5_0");

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

    ThrowIfFailed(device->CreateGraphicsPipelineState(&lightPsoDesc, IID_PPV_ARGS(&mLightingPSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC sh={}; sh.InputLayout={mInputLayout.data(),(UINT)mInputLayout.size()}; sh.pRootSignature=mShadowRootSignature.Get();
    sh.VS={reinterpret_cast<BYTE*>(mShadowVS->GetBufferPointer()),mShadowVS->GetBufferSize()}; sh.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    sh.RasterizerState.DepthBias=1500; sh.RasterizerState.SlopeScaledDepthBias=1.5f; sh.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    sh.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); sh.SampleMask=UINT_MAX; sh.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    sh.NumRenderTargets=0; sh.SampleDesc.Count=1; sh.DSVFormat=DXGI_FORMAT_D32_FLOAT;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&sh,IID_PPV_ARGS(&mShadowPSO)));
}

void RenderingSystem::BuildLightingDescriptors(ID3D12Device* device)
{
    mCbvSrvDescriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 5; // CBV + GBuffer(3) + shadow array
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

    mGBuffer.BuildShaderResourceViews(device, positionSrv, normalSrv, albedoSrv);
    CD3DX12_CPU_DESCRIPTOR_HANDLE shadowSrv = albedoSrv; shadowSrv.Offset(1,mCbvSrvDescriptorSize);
    D3D12_SHADER_RESOURCE_VIEW_DESC sd={}; sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Format=DXGI_FORMAT_R32_FLOAT; sd.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    sd.Texture2DArray.MipLevels=1; sd.Texture2DArray.ArraySize=CascadeCount;
    device->CreateShaderResourceView(mShadowMap.Get(),&sd,shadowSrv);
}


void RenderingSystem::BuildShadowRootSignature(ID3D12Device* device){
    CD3DX12_ROOT_PARAMETER p; p.InitAsConstants(16,0);
    CD3DX12_ROOT_SIGNATURE_DESC d(1,&p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> b,e; ThrowIfFailed(D3D12SerializeRootSignature(&d,D3D_ROOT_SIGNATURE_VERSION_1,b.GetAddressOf(),e.GetAddressOf()));
    ThrowIfFailed(device->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&mShadowRootSignature)));
}
void RenderingSystem::BuildShadowResources(ID3D12Device* device){
    D3D12_RESOURCE_DESC r=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS,ShadowMapSize,ShadowMapSize,CascadeCount,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE cv={}; cv.Format=DXGI_FORMAT_D32_FLOAT; cv.DepthStencil.Depth=1.0f;
    auto hp=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_DEPTH_WRITE,&cv,IID_PPV_ARGS(&mShadowMap)));
    D3D12_DESCRIPTOR_HEAP_DESC hd={}; hd.NumDescriptors=CascadeCount; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV; ThrowIfFailed(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&mShadowDsvHeap)));
    UINT inc=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV); mDsvDescriptorSize=inc;
    for(int i=0;i<CascadeCount;++i){ CD3DX12_CPU_DESCRIPTOR_HANDLE h(mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart(),i,inc); D3D12_DEPTH_STENCIL_VIEW_DESC dv={}; dv.Format=DXGI_FORMAT_D32_FLOAT; dv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DARRAY; dv.Texture2DArray.ArraySize=1; dv.Texture2DArray.FirstArraySlice=i; device->CreateDepthStencilView(mShadowMap.Get(),&dv,h); }
}
void RenderingSystem::BeginShadowPass(ID3D12GraphicsCommandList* c){ if(mShadowReadable){auto b=CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);c->ResourceBarrier(1,&b);mShadowReadable=false;} c->SetGraphicsRootSignature(mShadowRootSignature.Get()); c->SetPipelineState(mShadowPSO.Get()); }
void RenderingSystem::BeginShadowCascade(ID3D12GraphicsCommandList* c,int i){
    D3D12_VIEWPORT v={0,0,(float)ShadowMapSize,(float)ShadowMapSize,0,1}; D3D12_RECT r={0,0,(LONG)ShadowMapSize,(LONG)ShadowMapSize};
    c->RSSetViewports(1,&v); c->RSSetScissorRects(1,&r);
    CD3DX12_CPU_DESCRIPTOR_HANDLE h(mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart(),i,mDsvDescriptorSize);
    c->OMSetRenderTargets(0,nullptr,false,&h); c->ClearDepthStencilView(h,D3D12_CLEAR_FLAG_DEPTH,1.0f,0,0,nullptr);
}
void RenderingSystem::SetShadowWorldLightMatrix(ID3D12GraphicsCommandList* c,const XMFLOAT4X4& m){ c->SetGraphicsRoot32BitConstants(0,16,&m,0); }
void RenderingSystem::EndShadowPass(ID3D12GraphicsCommandList* c){ auto b=CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); c->ResourceBarrier(1,&b);mShadowReadable=true; }
