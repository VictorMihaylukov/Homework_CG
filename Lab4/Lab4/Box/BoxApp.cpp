#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/DDSTextureLoader.h"
#include "RenderingSystem.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#include <vector>
#include <string>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

struct Vertex
{
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT2 TexC;
};

struct ObjectConstants
{
    XMFLOAT4X4 World = MathHelper::Identity4x4();
    XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
    XMFLOAT2 TexScale = XMFLOAT2(1.0f, 1.0f);
    XMFLOAT2 TexOffset = XMFLOAT2(0.0f, 0.0f);
};

class BoxApp : public D3DApp
{
public:
    BoxApp(HINSTANCE hInstance);
    BoxApp(const BoxApp& rhs) = delete;
    BoxApp& operator=(const BoxApp& rhs) = delete;
    ~BoxApp();

    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void BuildDescriptorHeaps();
    void BuildConstantBuffers();
    void BuildBoxGeometry();
    void LoadTexture();
    void BuildTextureSRV();
    void SetupLights();

private:
    std::unique_ptr<RenderingSystem> mRenderingSystem;

    ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

    std::vector<ComPtr<ID3D12Resource>> mTextures;
    std::vector<ComPtr<ID3D12Resource>> mTextureUploadHeaps;

    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
    std::unique_ptr<MeshGeometry> mBoxGeo = nullptr;
    std::vector<std::unique_ptr<Material>> mMaterials;

    XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f * XM_PI;
    float mPhi = 0.30f * XM_PI;
    float mRadius = 18.0f;

    XMFLOAT2 mTexOffset = XMFLOAT2(0.0f, 0.0f);
    XMFLOAT2 mTexSpeed = XMFLOAT2(0.0f, 0.0f);

    std::vector<DeferredLight> mLights;
    XMFLOAT3 mAmbientLight = { 0.06f, 0.06f, 0.08f };
    XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };

    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
                   PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        BoxApp theApp(hInstance);
        if (!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

BoxApp::BoxApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
    mMainWndCaption = L"HW3";
}

BoxApp::~BoxApp()
{
}

bool BoxApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    mRenderingSystem = std::make_unique<RenderingSystem>();
    mRenderingSystem->Initialize(
        md3dDevice.Get(),
        mBackBufferFormat,
        mClientWidth,
        mClientHeight);

    BuildDescriptorHeaps();
    BuildConstantBuffers();
    LoadTexture();
    BuildTextureSRV();
    BuildBoxGeometry();
    SetupLights();

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    FlushCommandQueue();

    return true;
}

void BoxApp::OnResize()
{
    D3DApp::OnResize();

    XMMATRIX P = XMMatrixPerspectiveFovLH(
        0.25f * MathHelper::Pi,
        AspectRatio(),
        0.1f,
        1000.0f);
    XMStoreFloat4x4(&mProj, P);

    if (mRenderingSystem)
    {
        mRenderingSystem->OnResize(
            md3dDevice.Get(),
            mClientWidth,
            mClientHeight);
    }
}

void BoxApp::Update(const GameTimer& gt)
{
    float x = mRadius * sinf(mPhi) * cosf(mTheta);
    float z = mRadius * sinf(mPhi) * sinf(mTheta);
    float y = mRadius * cosf(mPhi);

    mEyePos = { x, y, z };

    XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
    XMVECTOR target = XMVectorSet(0.0f, 2.0f, 0.0f, 0.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);

    XMMATRIX world = XMLoadFloat4x4(&mWorld);
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX worldViewProj = world * view * proj;

    ObjectConstants objConstants;
    XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
    XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));

    mTexOffset.x += mTexSpeed.x * gt.DeltaTime();
    mTexOffset.y += mTexSpeed.y * gt.DeltaTime();

    objConstants.TexScale = XMFLOAT2(1.0f, 1.0f);
    objConstants.TexOffset = mTexOffset;

    mObjectCB->CopyData(0, objConstants);

    mRenderingSystem->UpdateLights(
        mEyePos,
        mAmbientLight,
        mLights.data(),
        static_cast<int>(mLights.size()));
}

void BoxApp::Draw(const GameTimer& gt)
{
    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // ---- Geometry pass -> G-Buffer ----
    mRenderingSystem->BeginGeometryPass(mCommandList.Get());

    ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    auto vbv = mBoxGeo->VertexBufferView();
    auto ibv = mBoxGeo->IndexBufferView();

    mCommandList->IASetVertexBuffers(0, 1, &vbv);
    mCommandList->IASetIndexBuffer(&ibv);
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle(
        mCbvHeap->GetGPUDescriptorHandleForHeapStart());
    mCommandList->SetGraphicsRootDescriptorTable(0, cbvHandle);

    UINT descriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (size_t materialId = 0; materialId < mMaterials.size(); ++materialId)
    {
        const auto& material = mMaterials[materialId];

        std::string submeshName = "material_" + std::to_string(materialId);
        auto it = mBoxGeo->DrawArgs.find(submeshName);
        if (it == mBoxGeo->DrawArgs.end())
            continue;

        const SubmeshGeometry& submesh = it->second;
        if (submesh.IndexCount == 0)
            continue;

        int srvIndex = material->DiffuseSrvHeapIndex;
        if (srvIndex < 0)
        {
            if (mTextures.empty())
                continue;
            srvIndex = 1;
        }

        CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(
            mCbvHeap->GetGPUDescriptorHandleForHeapStart(),
            srvIndex,
            descriptorSize);

        mCommandList->SetGraphicsRootDescriptorTable(1, srvHandle);

        mCommandList->DrawIndexedInstanced(
            submesh.IndexCount,
            1,
            submesh.StartIndexLocation,
            submesh.BaseVertexLocation,
            0);
    }

    mRenderingSystem->EndGeometryPass(mCommandList.Get());

    // Lighting pass -> back buffer
    auto barrierToRT = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &barrierToRT);

    mCommandList->ClearRenderTargetView(
        CurrentBackBufferView(),
        Colors::Black,
        0,
        nullptr);

    mRenderingSystem->ExecuteLightingPass(
        mCommandList.Get(),
        CurrentBackBufferView());

    auto barrierToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &barrierToPresent);

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();
}

void BoxApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    SetCapture(mhMainWnd);
}

void BoxApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void BoxApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)
    {
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        mTheta += dx;
        mPhi += dy;
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if ((btnState & MK_RBUTTON) != 0)
    {
        float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
        float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);

        mRadius += dx - dy;
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 50.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void BoxApp::SetupLights()
{
    mLights.clear();

    // Directional - солнце
    {
        DeferredLight sun;
        sun.Type = static_cast<int>(LightType::Directional);
        sun.Direction = { 0.3f, -1.0f, 0.2f };
        sun.Strength = { 0.55f, 0.50f, 0.40f };
        mLights.push_back(sun);
    }

    const XMFLOAT3 pointPositions[] =
    {
        {  0.0f, 2.5f,  0.0f },
        {  8.0f, 1.5f,  3.0f },
        { -8.0f, 1.5f,  3.0f },
        {  8.0f, 1.5f, -3.0f },
        { -8.0f, 1.5f, -3.0f },
        {  0.0f, 4.0f,  4.0f },
        {  0.0f, 4.0f, -4.0f },
    };

    const XMFLOAT3 pointColors[] =
    {
        { 1.0f, 0.85f, 0.6f },
        { 0.4f, 0.7f, 1.0f },
        { 1.0f, 0.4f, 0.4f },
        { 0.4f, 1.0f, 0.5f },
        { 1.0f, 0.5f, 1.0f },
        { 0.9f, 0.9f, 0.5f },
        { 0.5f, 0.9f, 0.9f },
    };

    for (int i = 0; i < _countof(pointPositions); ++i)
    {
        DeferredLight point;
        point.Type = static_cast<int>(LightType::Point);
        point.Position = pointPositions[i];
        point.Strength = pointColors[i];
        point.FalloffStart = 2.0f;
        point.FalloffEnd = 12.0f;
        mLights.push_back(point);
    }

    // Spot lights сверху
    {
        DeferredLight spot;
        spot.Type = static_cast<int>(LightType::Spot);
        spot.Position = { 5.0f, 8.0f, 0.0f };
        spot.Direction = { 0.0f, -1.0f, 0.0f };
        spot.Strength = { 2.0f, 1.8f, 1.2f };
        spot.FalloffStart = 3.0f;
        spot.FalloffEnd = 20.0f;
        spot.SpotPower = 32.0f;
        mLights.push_back(spot);
    }

    {
        DeferredLight spot;
        spot.Type = static_cast<int>(LightType::Spot);
        spot.Position = { -5.0f, 8.0f, 0.0f };
        spot.Direction = { 0.2f, -1.0f, 0.0f };
        spot.Strength = { 1.2f, 1.5f, 2.0f };
        spot.FalloffStart = 3.0f;
        spot.FalloffEnd = 20.0f;
        spot.SpotPower = 24.0f;
        mLights.push_back(spot);
    }
}

void BoxApp::LoadTexture()
{
    std::string inputfile = "../../Assets/house.obj";

    tinyobj::ObjReaderConfig reader_config;
    reader_config.triangulate = true;

    tinyobj::ObjReader reader;

    if (!reader.ParseFromFile(inputfile, reader_config))
    {
        if (!reader.Error().empty())
            OutputDebugStringA(reader.Error().c_str());

        throw std::runtime_error(
            "Failed to load house.obj while loading materials.");
    }

    const auto& materials = reader.GetMaterials();

    mMaterials.clear();
    mTextures.clear();
    mTextureUploadHeaps.clear();

    for (size_t i = 0; i < materials.size(); ++i)
    {
        const auto& srcMaterial = materials[i];

        auto material = std::make_unique<Material>();
        material->Name = srcMaterial.name;
        material->MatCBIndex = static_cast<int>(i);

        if (srcMaterial.diffuse_texname.empty())
        {
            material->DiffuseSrvHeapIndex = -1;
            mMaterials.push_back(std::move(material));
            continue;
        }

        std::wstring filename =
            L"../../assets/" +
            std::wstring(
                srcMaterial.diffuse_texname.begin(),
                srcMaterial.diffuse_texname.end());

        ComPtr<ID3D12Resource> texture = nullptr;
        ComPtr<ID3D12Resource> uploadHeap = nullptr;

        ThrowIfFailed(
            DirectX::CreateDDSTextureFromFile12(
                md3dDevice.Get(),
                mCommandList.Get(),
                filename.c_str(),
                texture,
                uploadHeap));

        material->DiffuseSrvHeapIndex =
            1 + static_cast<int>(mTextures.size());

        mTextures.push_back(texture);
        mTextureUploadHeaps.push_back(uploadHeap);

        mMaterials.push_back(std::move(material));
    }
}

void BoxApp::BuildDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = 64;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &cbvHeapDesc,
        IID_PPV_ARGS(&mCbvHeap)));
}

void BoxApp::BuildConstantBuffers()
{
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(
        md3dDevice.Get(), 1, true);

    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    D3D12_GPU_VIRTUAL_ADDRESS cbAddress =
        mObjectCB->Resource()->GetGPUVirtualAddress();

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = objCBByteSize;

    md3dDevice->CreateConstantBufferView(
        &cbvDesc,
        mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void BoxApp::BuildBoxGeometry()
{
    std::string inputfile = "../../Assets/house.obj";

    tinyobj::ObjReaderConfig reader_config;
    reader_config.triangulate = true;

    tinyobj::ObjReader reader;

    if (!reader.ParseFromFile(inputfile, reader_config))
    {
        if (!reader.Error().empty())
            OutputDebugStringA(reader.Error().c_str());

        throw std::runtime_error("Failed to load house.obj (tinyobj).");
    }

    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();

    std::vector<Vertex> vertices;
    vertices.reserve(500000);

    std::vector<std::vector<std::uint32_t>> materialIndices(mMaterials.size());

    if (materialIndices.empty())
        throw std::runtime_error("house: no materials were loaded.");

    for (const auto& shape : shapes)
    {
        const auto& shapeIndices = shape.mesh.indices;
        const auto& materialIds = shape.mesh.material_ids;

        for (size_t i = 0; i < shapeIndices.size(); i += 3)
        {
            int materialId = -1;

            if (i / 3 < materialIds.size())
                materialId = materialIds[i / 3];

            if (materialId < 0 ||
                materialId >= static_cast<int>(mMaterials.size()))
            {
                materialId = 0;
            }

            for (int v = 0; v < 3; ++v)
            {
                const auto& idx = shapeIndices[i + v];

                Vertex vertex = {};

                vertex.Pos.x = attrib.vertices[3 * idx.vertex_index + 0];
                vertex.Pos.y = attrib.vertices[3 * idx.vertex_index + 1];
                vertex.Pos.z = attrib.vertices[3 * idx.vertex_index + 2];

                if (idx.normal_index >= 0 && !attrib.normals.empty())
                {
                    vertex.Normal.x = attrib.normals[3 * idx.normal_index + 0];
                    vertex.Normal.y = attrib.normals[3 * idx.normal_index + 1];
                    vertex.Normal.z = attrib.normals[3 * idx.normal_index + 2];
                }
                else
                {
                    vertex.Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
                }

                if (idx.texcoord_index >= 0 && !attrib.texcoords.empty())
                {
                    vertex.TexC.x = attrib.texcoords[2 * idx.texcoord_index + 0];
                    vertex.TexC.y = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1];
                }
                else
                {
                    vertex.TexC = XMFLOAT2(0.0f, 0.0f);
                }

                std::uint32_t vertexIndex =
                    static_cast<std::uint32_t>(vertices.size());

                vertices.push_back(vertex);
                materialIndices[materialId].push_back(vertexIndex);
            }
        }
    }

    const UINT vbByteSize =
        static_cast<UINT>(vertices.size() * sizeof(Vertex));

    std::vector<std::uint32_t> finalIndices;
    finalIndices.reserve(vertices.size());

    for (const auto& materialList : materialIndices)
    {
        for (std::uint32_t vertexIndex : materialList)
            finalIndices.push_back(vertexIndex);
    }

    const UINT finalIbByteSize =
        static_cast<UINT>(finalIndices.size() * sizeof(std::uint32_t));

    mBoxGeo = std::make_unique<MeshGeometry>();
    mBoxGeo->Name = "houseGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &mBoxGeo->VertexBufferCPU));
    CopyMemory(mBoxGeo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(finalIbByteSize, &mBoxGeo->IndexBufferCPU));
    CopyMemory(mBoxGeo->IndexBufferCPU->GetBufferPointer(), finalIndices.data(), finalIbByteSize);

    mBoxGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        vertices.data(),
        vbByteSize,
        mBoxGeo->VertexBufferUploader);

    if (finalIndices.empty())
        throw std::runtime_error("house: final index buffer is empty.");

    mBoxGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        finalIndices.data(),
        finalIbByteSize,
        mBoxGeo->IndexBufferUploader);

    mBoxGeo->VertexByteStride = sizeof(Vertex);
    mBoxGeo->VertexBufferByteSize = vbByteSize;
    mBoxGeo->IndexFormat = DXGI_FORMAT_R32_UINT;
    mBoxGeo->IndexBufferByteSize = finalIbByteSize;

    UINT startIndex = 0;
    for (size_t materialId = 0; materialId < materialIndices.size(); ++materialId)
    {
        const auto& materialList = materialIndices[materialId];
        if (materialList.empty())
            continue;

        SubmeshGeometry submesh;
        submesh.IndexCount = static_cast<UINT>(materialList.size());
        submesh.StartIndexLocation = startIndex;
        submesh.BaseVertexLocation = 0;

        mBoxGeo->DrawArgs["material_" + std::to_string(materialId)] = submesh;
        startIndex += submesh.IndexCount;
    }
}

void BoxApp::BuildTextureSRV()
{
    UINT descriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (size_t i = 0; i < mTextures.size(); ++i)
    {
        auto texture = mTextures[i];

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = texture->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = texture->GetDesc().MipLevels;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
            mCbvHeap->GetCPUDescriptorHandleForHeapStart(),
            1 + static_cast<INT>(i),
            descriptorSize);

        md3dDevice->CreateShaderResourceView(
            texture.Get(),
            &srvDesc,
            hDescriptor);
    }
}
