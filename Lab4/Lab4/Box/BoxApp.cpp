#include "HeaderList.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#include <vector>
#include <string>
#include <stdexcept>
#include <numeric>
#include <sstream>
#include <cfloat>
#include <cmath>

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

    XMFLOAT3 EyePosW = XMFLOAT3(0.0f, 0.0f, 0.0f);
    float DisplacementScale = 0.08f;

    float TessNear = 5.0f;
    float TessFar = 30.0f;
    float TessMin = 2.0f;
    float TessMax = 16.0f;
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
    void BuildSceneObjects();
    void UpdateVisibility();
    void UpdateCullingInput();
    void UpdateCascades();

private:
    std::unique_ptr<RenderingSystem> mRenderingSystem;
    std::unique_ptr<ParticleSystem> mParticleSystem;

    ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

    std::vector<ComPtr<ID3D12Resource>> mTextures;
    std::vector<ComPtr<ID3D12Resource>> mTextureUploadHeaps;

    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
    std::unique_ptr<MeshGeometry> mBoxGeo = nullptr;

    DirectX::BoundingBox mLocalBounds;
    std::vector<SceneObject> mSceneObjects;
    std::vector<int> mVisibleObjects;
    Octree mOctree;

    bool mFrustumCullingEnabled = true;
    bool mOctreeEnabled = true;
    bool mPrevCKeyDown = false;
    bool mPrevOKeyDown = false;

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
    XMFLOAT4X4 mShadowTransforms[CascadeCount] = {};
    float mCascadeSplits[CascadeCount] = {};

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
    mMainWndCaption = L"HW6";
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

    mParticleSystem = std::make_unique<ParticleSystem>();
    mParticleSystem->Initialize(md3dDevice.Get(), mCommandList.Get(),
        mBackBufferFormat, Gbuffer::DepthFormat);

    BuildDescriptorHeaps();
    LoadTexture();
    BuildTextureSRV();
    BuildBoxGeometry();
    BuildSceneObjects();
    BuildConstantBuffers();
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
    UpdateCullingInput();

    float x = mRadius * sinf(mPhi) * cosf(mTheta);
    float z = mRadius * sinf(mPhi) * sinf(mTheta);
    float y = mRadius * cosf(mPhi);

    mEyePos = { x, y, z };

    XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
    XMVECTOR target = XMVectorSet(0.0f, 2.0f, 0.0f, 0.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);

    XMMATRIX proj = XMLoadFloat4x4(&mProj);

    mTexOffset.x += mTexSpeed.x * gt.DeltaTime();
    mTexOffset.y += mTexSpeed.y * gt.DeltaTime();

    for (size_t i = 0; i < mSceneObjects.size(); ++i)
    {
        XMMATRIX world = XMLoadFloat4x4(&mSceneObjects[i].World);
        XMMATRIX worldViewProj = world * view * proj;

        ObjectConstants objConstants;
        XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
        XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
        objConstants.TexScale = XMFLOAT2(1.0f, 1.0f);
        objConstants.TexOffset = mTexOffset;
        objConstants.EyePosW = mEyePos;
        objConstants.DisplacementScale = 0.08f;
        objConstants.TessNear = 5.0f;
        objConstants.TessFar = 30.0f;
        objConstants.TessMin = 2.0f;
        objConstants.TessMax = 16.0f;

        mObjectCB->CopyData(static_cast<int>(i), objConstants);
    }

    UpdateVisibility();

    UpdateCascades();
    XMFLOAT4X4 viewT; XMStoreFloat4x4(&viewT, XMMatrixTranspose(view));
    mRenderingSystem->UpdateLights(mEyePos, mAmbientLight, mLights.data(),
        static_cast<int>(mLights.size()), viewT, mShadowTransforms, mCascadeSplits);

    std::wostringstream caption;
    caption << L"HW6 | particles: " << ParticleSystem::MaxParticles
            << L" | objects: " << mSceneObjects.size()
            << L" | visible: " << mVisibleObjects.size()
            << L" | C: frustum " << (mFrustumCullingEnabled ? L"ON" : L"OFF")
            << L" | O: octree " << (mOctreeEnabled ? L"ON" : L"OFF");
    SetWindowText(mhMainWnd, caption.str().c_str());
}

void BoxApp::Draw(const GameTimer& gt)
{
    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    auto shadowVBV = mBoxGeo->VertexBufferView(); auto shadowIBV = mBoxGeo->IndexBufferView();
    mRenderingSystem->BeginShadowPass(mCommandList.Get());
    mCommandList->IASetVertexBuffers(0,1,&shadowVBV); mCommandList->IASetIndexBuffer(&shadowIBV);
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for(int cascade=0; cascade<CascadeCount; ++cascade)
    {
        mRenderingSystem->BeginShadowCascade(mCommandList.Get(),cascade);
        XMMATRIX lightVP = XMMatrixTranspose(XMLoadFloat4x4(&mShadowTransforms[cascade]));
        for(size_t oi=0; oi<mSceneObjects.size(); ++oi)
        {
            XMMATRIX world=XMLoadFloat4x4(&mSceneObjects[oi].World); XMFLOAT4X4 wlp;
            XMStoreFloat4x4(&wlp,XMMatrixTranspose(world*lightVP));
            mRenderingSystem->SetShadowWorldLightMatrix(mCommandList.Get(),wlp);
            for(size_t materialId=0; materialId<mMaterials.size(); ++materialId)
            {
                auto it=mBoxGeo->DrawArgs.find("material_"+std::to_string(materialId));
                if(it!=mBoxGeo->DrawArgs.end() && it->second.IndexCount)
                    mCommandList->DrawIndexedInstanced(it->second.IndexCount,1,it->second.StartIndexLocation,it->second.BaseVertexLocation,0);
            }
        }
    }
    mRenderingSystem->EndShadowPass(mCommandList.Get());

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    mRenderingSystem->BeginGeometryPass(mCommandList.Get());

    ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    auto vbv = mBoxGeo->VertexBufferView();
    auto ibv = mBoxGeo->IndexBufferView();

    mCommandList->IASetVertexBuffers(0, 1, &vbv);
    mCommandList->IASetIndexBuffer(&ibv);
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

    UINT descriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    const UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    const D3D12_GPU_VIRTUAL_ADDRESS objectCBBase =
        mObjectCB->Resource()->GetGPUVirtualAddress();

    for (int objectIndex : mVisibleObjects)
    {
        mCommandList->SetGraphicsRootConstantBufferView(
            0,
            objectCBBase + static_cast<UINT64>(objectIndex) * objCBByteSize);

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

            int diffuseIndex = material->DiffuseSrvHeapIndex;
            if (diffuseIndex < 1)
            {
                if (mTextures.empty())
                    continue;
                diffuseIndex = 1;
            }

            CD3DX12_GPU_DESCRIPTOR_HANDLE diffuseHandle(
                mCbvHeap->GetGPUDescriptorHandleForHeapStart(),
                diffuseIndex,
                descriptorSize);
            mCommandList->SetGraphicsRootDescriptorTable(1, diffuseHandle);

            int normalIndex = material->NormalSrvHeapIndex;
            if (normalIndex < 1)
                normalIndex = diffuseIndex;

            CD3DX12_GPU_DESCRIPTOR_HANDLE normalHandle(
                mCbvHeap->GetGPUDescriptorHandleForHeapStart(),
                normalIndex,
                descriptorSize);
            mCommandList->SetGraphicsRootDescriptorTable(2, normalHandle);

            int displacementIndex = material->DisplacementSrvHeapIndex;
            if (displacementIndex < 1)
                displacementIndex = diffuseIndex;

            CD3DX12_GPU_DESCRIPTOR_HANDLE displacementHandle(
                mCbvHeap->GetGPUDescriptorHandleForHeapStart(),
                displacementIndex,
                descriptorSize);
            mCommandList->SetGraphicsRootDescriptorTable(3, displacementHandle);

            UINT flags[2] =
            {
                material->NormalSrvHeapIndex >= 1 ? 1u : 0u,
                material->DisplacementSrvHeapIndex >= 1 ? 1u : 0u
            };

            mCommandList->SetGraphicsRoot32BitConstants(4, 2, flags, 0);
            mCommandList->DrawIndexedInstanced(
                submesh.IndexCount,
                1,
                submesh.StartIndexLocation,
                submesh.BaseVertexLocation,
                0);
        }
    }

    mRenderingSystem->EndGeometryPass(mCommandList.Get());

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

    mParticleSystem->Update(mCommandList.Get(), gt.DeltaTime(), gt.TotalTime());
    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);
    mParticleSystem->Render(mCommandList.Get(), mView, mProj,
        CurrentBackBufferView(), mRenderingSystem->GetGBuffer().DepthDSV());

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
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 350.0f);
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

    auto loadTexture = [&](const std::string& texName) -> int
    {
        if (texName.empty())
            return -1;

        std::wstring filename =
            L"../../assets/" +
            std::wstring(texName.begin(), texName.end());

        ComPtr<ID3D12Resource> texture = nullptr;
        ComPtr<ID3D12Resource> uploadHeap = nullptr;

        ThrowIfFailed(
            DirectX::CreateDDSTextureFromFile12(
                md3dDevice.Get(),
                mCommandList.Get(),
                filename.c_str(),
                texture,
                uploadHeap));

        int srvIndex = 1 + static_cast<int>(mTextures.size());
        mTextures.push_back(texture);
        mTextureUploadHeaps.push_back(uploadHeap);
        return srvIndex;
    };

    for (size_t i = 0; i < materials.size(); ++i)
    {
        const auto& srcMaterial = materials[i];

        auto material = std::make_unique<Material>();
        material->Name = srcMaterial.name;
        material->MatCBIndex = static_cast<int>(i);

        material->DiffuseSrvHeapIndex =
            loadTexture(srcMaterial.diffuse_texname);

        std::string normalName = !srcMaterial.normal_texname.empty()
            ? srcMaterial.normal_texname
            : srcMaterial.bump_texname;
        material->NormalSrvHeapIndex = loadTexture(normalName);

        material->DisplacementSrvHeapIndex =
            loadTexture(srcMaterial.displacement_texname);

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
    const UINT objectCount = static_cast<UINT>((std::max)(size_t(1), mSceneObjects.size()));
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(
        md3dDevice.Get(), objectCount, true);
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

    if (vertices.empty())
        throw std::runtime_error("house: vertex buffer is empty.");

    XMFLOAT3 minP = vertices[0].Pos;
    XMFLOAT3 maxP = vertices[0].Pos;
    for (const auto& v : vertices)
    {
        minP.x = (std::min)(minP.x, v.Pos.x);
        minP.y = (std::min)(minP.y, v.Pos.y);
        minP.z = (std::min)(minP.z, v.Pos.z);
        maxP.x = (std::max)(maxP.x, v.Pos.x);
        maxP.y = (std::max)(maxP.y, v.Pos.y);
        maxP.z = (std::max)(maxP.z, v.Pos.z);
    }
    BoundingBox::CreateFromPoints(
        mLocalBounds,
        XMLoadFloat3(&minP),
        XMLoadFloat3(&maxP));

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

void BoxApp::BuildSceneObjects()
{
    constexpr int GridSize = 1;
    constexpr float Spacing = 5.0f;
    constexpr float ObjectScale = 0.20f;

    mSceneObjects.clear();
    mSceneObjects.reserve(GridSize * GridSize);

    const float halfGrid = (GridSize - 1) * Spacing * 0.5f;

    for (int z = 0; z < GridSize; ++z)
    {
        for (int x = 0; x < GridSize; ++x)
        {
            const float worldX = x * Spacing - halfGrid;
            const float worldZ = z * Spacing - halfGrid;
            const float angle = ((x * 17 + z * 31) % 360) * (XM_PI / 180.0f);

            XMMATRIX world =
                XMMatrixScaling(ObjectScale, ObjectScale, ObjectScale) *
                XMMatrixRotationY(angle) *
                XMMatrixTranslation(worldX, 0.0f, worldZ);

            SceneObject object;
            XMStoreFloat4x4(&object.World, world);
            mLocalBounds.Transform(object.Bounds, world);
            mSceneObjects.push_back(object);
        }
    }

    mOctree.Build(mSceneObjects, 7, 16);
    mVisibleObjects.resize(mSceneObjects.size());
    std::iota(mVisibleObjects.begin(), mVisibleObjects.end(), 0);
}

void BoxApp::UpdateCullingInput()
{
    const bool cDown = (GetAsyncKeyState('C') & 0x8000) != 0;
    const bool oDown = (GetAsyncKeyState('O') & 0x8000) != 0;

    if (cDown && !mPrevCKeyDown)
        mFrustumCullingEnabled = !mFrustumCullingEnabled;

    if (oDown && !mPrevOKeyDown)
        mOctreeEnabled = !mOctreeEnabled;

    mPrevCKeyDown = cDown;
    mPrevOKeyDown = oDown;
}

void BoxApp::UpdateVisibility()
{
    if (!mFrustumCullingEnabled)
    {
        mVisibleObjects.resize(mSceneObjects.size());
        std::iota(mVisibleObjects.begin(), mVisibleObjects.end(), 0);
        return;
    }

    const XMMATRIX proj = XMLoadFloat4x4(&mProj);
    const XMMATRIX view = XMLoadFloat4x4(&mView);

    BoundingFrustum viewFrustum;
    BoundingFrustum::CreateFromMatrix(viewFrustum, proj);

    BoundingFrustum worldFrustum;
    const XMMATRIX invView = XMMatrixInverse(nullptr, view);
    viewFrustum.Transform(worldFrustum, invView);

    if (mOctreeEnabled)
    {
        mOctree.Query(worldFrustum, mVisibleObjects);
        return;
    }

    mVisibleObjects.clear();
    mVisibleObjects.reserve(mSceneObjects.size());
    for (size_t i = 0; i < mSceneObjects.size(); ++i)
    {
        if (worldFrustum.Contains(mSceneObjects[i].Bounds) != DISJOINT)
            mVisibleObjects.push_back(static_cast<int>(i));
    }
}

void BoxApp::UpdateCascades()
{
    const float nearZ=0.1f, farZ=180.0f, lambda=0.75f;
    float prev=nearZ;
    for(int i=0;i<CascadeCount;++i){ float p=float(i+1)/CascadeCount; float logZ=nearZ*powf(farZ/nearZ,p); float linZ=nearZ+(farZ-nearZ)*p; mCascadeSplits[i]=lambda*logZ+(1-lambda)*linZ; }
    XMVECTOR eye=XMLoadFloat3(&mEyePos), target=XMVectorSet(0,2,0,1);
    XMVECTOR forward=XMVector3Normalize(target-eye), right=XMVector3Normalize(XMVector3Cross(XMVectorSet(0,1,0,0),forward));
    XMVECTOR up=XMVector3Normalize(XMVector3Cross(forward,right));
    const float tanHalf=tanf(0.125f*MathHelper::Pi), aspect=AspectRatio();
    XMVECTOR lightDir=XMVector3Normalize(XMLoadFloat3(&mLights[0].Direction));
    for(int c=0;c<CascadeCount;++c){
        float n=prev, f=mCascadeSplits[c]; prev=f; XMVECTOR corners[8]; int k=0;
        for(int plane=0;plane<2;++plane){ float d=plane?f:n; float hh=d*tanHalf, hw=hh*aspect; XMVECTOR center=eye+forward*d;
            for(int sy=-1;sy<=1;sy+=2) for(int sx=-1;sx<=1;sx+=2) corners[k++]=center+right*(hw*(float)sx)+up*(hh*(float)sy); }
        XMVECTOR center=XMVectorZero(); for(auto& q:corners) center+=q; center/=8.0f;
        XMVECTOR lightPos=center-lightDir*120.0f; XMMATRIX lv=XMMatrixLookAtLH(lightPos,center,XMVectorSet(0,1,0,0));
        XMFLOAT3 mn(FLT_MAX,FLT_MAX,FLT_MAX),mx(-FLT_MAX,-FLT_MAX,-FLT_MAX);
        for(auto& q:corners){ XMFLOAT3 a; XMStoreFloat3(&a,XMVector3TransformCoord(q,lv)); mn.x=(std::min)(mn.x,a.x);mn.y=(std::min)(mn.y,a.y);mn.z=(std::min)(mn.z,a.z);mx.x=(std::max)(mx.x,a.x);mx.y=(std::max)(mx.y,a.y);mx.z=(std::max)(mx.z,a.z); }
        mn.z-=80.0f; mx.z+=80.0f; XMMATRIX lp=XMMatrixOrthographicOffCenterLH(mn.x,mx.x,mn.y,mx.y,mn.z,mx.z);
        XMStoreFloat4x4(&mShadowTransforms[c],XMMatrixTranspose(lv*lp));
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
