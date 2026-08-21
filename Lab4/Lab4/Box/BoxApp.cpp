#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/DDSTextureLoader.h"

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
    //текстурная координа вместо цвета
    XMFLOAT2 TexC;
};

struct ObjectConstants
{
    XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();

    XMFLOAT2 TexScale = XMFLOAT2(4.0f, 4.0f);
    XMFLOAT2 TexOffset = XMFLOAT2(0.0f, 0.0f);
};

class BoxApp : public D3DApp
{
public:
	BoxApp(HINSTANCE hInstance);
    BoxApp(const BoxApp& rhs) = delete;
    BoxApp& operator=(const BoxApp& rhs) = delete;
	~BoxApp();

	virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void BuildDescriptorHeaps();
	void BuildConstantBuffers();
    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildBoxGeometry();
    void BuildPSO();
    void LoadTexture();
    void BuildTextureSRV();

private:
    
    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

    //добавляем текстуру в класс
    std::vector<ComPtr<ID3D12Resource>> mTextures;
    std::vector<ComPtr<ID3D12Resource>> mTextureUploadHeaps;

    ComPtr<ID3D12Resource> mTexture = nullptr;
    ComPtr<ID3D12Resource> mTextureUploadHeap = nullptr;

    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;

	std::unique_ptr<MeshGeometry> mBoxGeo = nullptr;

    //материалы
    std::vector<std::unique_ptr<Material>> mMaterials;

    ComPtr<ID3DBlob> mvsByteCode = nullptr;
    ComPtr<ID3DBlob> mpsByteCode = nullptr;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    ComPtr<ID3D12PipelineState> mPSO = nullptr;

    XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f*XM_PI;
    float mPhi = XM_PIDIV4;
    float mRadius = 5.0f;

    XMFLOAT2 mTexOffset = XMFLOAT2(0.0f, 0.0f);
    XMFLOAT2 mTexSpeed = XMFLOAT2(0.0f, 0.0f);

    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
				   PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

    try
    {
        BoxApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

BoxApp::BoxApp(HINSTANCE hInstance)
: D3DApp(hInstance) 
{
}

BoxApp::~BoxApp()
{
}

bool BoxApp::Initialize()
{
    if(!D3DApp::Initialize())
		return false;
		
    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
 
    BuildDescriptorHeaps();
    BuildConstantBuffers();
    LoadTexture();
    BuildTextureSRV();
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildBoxGeometry();
    BuildPSO();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

	return true;
}

void BoxApp::OnResize()
{
	D3DApp::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void BoxApp::Update(const GameTimer& gt)
{
    // Convert Spherical to Cartesian coordinates.
    float x = mRadius*sinf(mPhi)*cosf(mTheta);
    float z = mRadius*sinf(mPhi)*sinf(mTheta);
    float y = mRadius*cosf(mPhi);

    // Build the view matrix.
    XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);

    XMMATRIX world = XMLoadFloat4x4(&mWorld);
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX worldViewProj = world*view*proj;

    ObjectConstants objConstants;

    XMStoreFloat4x4(
        &objConstants.WorldViewProj,
        XMMatrixTranspose(worldViewProj));

    // Обновляем положение текстуры
    mTexOffset.x += mTexSpeed.x * gt.DeltaTime();
    mTexOffset.y += mTexSpeed.y * gt.DeltaTime();

    objConstants.TexScale = XMFLOAT2(4.0f, 4.0f);
    objConstants.TexOffset = mTexOffset;

    mObjectCB->CopyData(0, objConstants);
}

void BoxApp::Draw(const GameTimer& gt)
{
	ThrowIfFailed(mDirectCmdListAlloc->Reset());

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    auto barrier1 = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    mCommandList->ResourceBarrier(1, &barrier1);

    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	
    auto rtv = CurrentBackBufferView();
    auto dsv = DepthStencilView();

    mCommandList->OMSetRenderTargets(1, &rtv, true, &dsv);

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    auto vbv = mBoxGeo->VertexBufferView();
    auto ibv = mBoxGeo->IndexBufferView();

    mCommandList->IASetVertexBuffers(0, 1, &vbv);
    mCommandList->IASetIndexBuffer(&ibv);
    mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    // CBV находится в дескрипторе [0]
    CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle(
        mCbvHeap->GetGPUDescriptorHandleForHeapStart());

    mCommandList->SetGraphicsRootDescriptorTable(
        0,
        cbvHandle);

    // размер одного дескриптор
    UINT descriptorSize =
        md3dDevice->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // рисуем каждый материал отдельно
    for (size_t materialId = 0;
        materialId < mMaterials.size();
        ++materialId)
    {
        const auto& material = mMaterials[materialId];

        std::string submeshName =
            "material_" + std::to_string(materialId);

        auto it = mBoxGeo->DrawArgs.find(submeshName);

        if (it == mBoxGeo->DrawArgs.end())
            continue;

        const SubmeshGeometry& submesh = it->second;

        if (submesh.IndexCount == 0)
            continue;

        // Если у материала нет текстуры, используем первую загруженную текстуру как запасную
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

        // Root parameter 1 = SRV
        mCommandList->SetGraphicsRootDescriptorTable(
            1,
            srvHandle);

        mCommandList->DrawIndexedInstanced(
            submesh.IndexCount,
            1,
            submesh.StartIndexLocation,
            submesh.BaseVertexLocation,
            0);
    }
	
    auto barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);

    mCommandList->ResourceBarrier(1, &barrier2);

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
    if((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.005 unit in the scene.
        float dx = 0.005f*static_cast<float>(x - mLastMousePos.x);
        float dy = 0.005f*static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 3.0f, 15.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void BoxApp::LoadTexture()
{
    std::string inputfile = "../../Assets/sponza.obj";

    tinyobj::ObjReaderConfig reader_config;
    reader_config.triangulate = true;

    tinyobj::ObjReader reader;

    if (!reader.ParseFromFile(inputfile, reader_config))
    {
        if (!reader.Error().empty())
            OutputDebugStringA(reader.Error().c_str());

        throw std::runtime_error(
            "Failed to load sponza.obj while loading materials.");
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

        // материал без diffuse-текстуры
        if (srcMaterial.diffuse_texname.empty())
        {
            material->DiffuseSrvHeapIndex = -1;

            mMaterials.push_back(std::move(material));
            continue;
        }

        // путь из MTL
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

        OutputDebugStringA(
            ("Loaded material texture: " +
                srcMaterial.name +
                " -> " +
                srcMaterial.diffuse_texname +
                "\n").c_str());

        mMaterials.push_back(std::move(material));
    }
}

void BoxApp::BuildDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    //увеличиваем кол-во дескрипторов
    cbvHeapDesc.NumDescriptors = 32;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
        IID_PPV_ARGS(&mCbvHeap)));
}

void BoxApp::BuildConstantBuffers()
{
	mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(), 1, true);

	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();
    // Offset to the ith object constant buffer in the buffer.
    int boxCBufIndex = 0;
	cbAddress += boxCBufIndex*objCBByteSize;

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
	cbvDesc.BufferLocation = cbAddress;
	cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	md3dDevice->CreateConstantBufferView(
		&cbvDesc,
		mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void BoxApp::BuildRootSignature()
{
    CD3DX12_ROOT_PARAMETER slotRootParameter[2];

    CD3DX12_DESCRIPTOR_RANGE cbvTable;
    cbvTable.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
        1,
        0);

    slotRootParameter[0].InitAsDescriptorTable(
        1,
        &cbvTable);

    CD3DX12_DESCRIPTOR_RANGE srvTable;
    srvTable.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        1,
        0);

    slotRootParameter[1].InitAsDescriptorTable(
        1,
        &srvTable);

    CD3DX12_STATIC_SAMPLER_DESC samplerDesc(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        2,
        slotRootParameter,
        1,
        &samplerDesc,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if(errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)));
}

void BoxApp::BuildShadersAndInputLayout()
{
    HRESULT hr = S_OK;
    
    mvsByteCode = d3dUtil::CompileShader(L"../../Assets/shaders/color.hlsl", nullptr, "VS", "vs_5_0");
    mpsByteCode = d3dUtil::CompileShader(L"../../Assets/shaders/color.hlsl", nullptr, "PS", "ps_5_0");

    mInputLayout =
    {
        {
            "POSITION",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            0,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0
        },

        {
            "TEXCOORD",
            0,
            DXGI_FORMAT_R32G32_FLOAT,
            0,
            12,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0
        }
    };
}

void BoxApp::BuildBoxGeometry()
{
    std::string inputfile = "../../Assets/sponza.obj";

    tinyobj::ObjReaderConfig reader_config;
    reader_config.triangulate = true;

    tinyobj::ObjReader reader;

    if (!reader.ParseFromFile(inputfile, reader_config))
    {
        if (!reader.Error().empty())
            OutputDebugStringA(reader.Error().c_str());

        throw std::runtime_error(
            "Failed to load sponza.obj (tinyobj).");
    }

    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();

    std::vector<Vertex> vertices;

    vertices.reserve(500000);

    // для каждого материала будем хранить индексы его треугольников
    std::vector<std::vector<std::uint32_t>> materialIndices(
        mMaterials.size());

    if (materialIndices.empty())
    {
        throw std::runtime_error(
            "Sponza: no materials were loaded.");
    }

    for (const auto& shape : shapes)
    {
        const auto& shapeIndices = shape.mesh.indices;
        const auto& materialIds = shape.mesh.material_ids;

        for (size_t i = 0; i < shapeIndices.size(); i += 3)
        {
            int materialId = -1;

            if (i / 3 < materialIds.size())
                materialId = materialIds[i / 3];

            // Если OBJ не указал материал или material_id некорректный,
            // отправляем треугольник в материал 0.
            if (materialId < 0 ||
                materialId >= static_cast<int>(mMaterials.size()))
            {
                materialId = 0;
            }

            for (int v = 0; v < 3; ++v)
            {
                const auto& idx = shapeIndices[i + v];

                Vertex vertex = {};

                // POSITION
                vertex.Pos.x =
                    attrib.vertices[3 * idx.vertex_index + 0];

                vertex.Pos.y =
                    attrib.vertices[3 * idx.vertex_index + 1];

                vertex.Pos.z =
                    attrib.vertices[3 * idx.vertex_index + 2];

                // масштаб Sponza
                vertex.Pos.x *= 0.01f;
                vertex.Pos.y *= 0.01f;
                vertex.Pos.z *= 0.01f;

                // TEXCOORD
                if (idx.texcoord_index >= 0 &&
                    !attrib.texcoords.empty())
                {
                    vertex.TexC.x =
                        attrib.texcoords[
                            2 * idx.texcoord_index + 0];

                    vertex.TexC.y =
                        1.0f -
                        attrib.texcoords[
                            2 * idx.texcoord_index + 1];
                }
                else
                {
                    vertex.TexC =
                        XMFLOAT2(0.0f, 0.0f);
                }

                std::uint32_t vertexIndex =
                    static_cast<std::uint32_t>(vertices.size());

                vertices.push_back(vertex);

                if (materialId >= 0 &&
                    materialId < static_cast<int>(materialIndices.size()))
                {
                    materialIndices[materialId].push_back(vertexIndex);
                }
            }
        }
    }

    const UINT vbByteSize =
        static_cast<UINT>(
            vertices.size() * sizeof(Vertex));

    // создаём общий индексный буфер. вместо отдельного index buffer для каждого материала, собираем все индексы последовательно
    std::vector<std::uint32_t> finalIndices;

    finalIndices.reserve(vertices.size());

    for (const auto& materialList : materialIndices)
    {
        for (std::uint32_t vertexIndex : materialList)
        {
            finalIndices.push_back(vertexIndex);
        }
    }

    const UINT finalIbByteSize =
        static_cast<UINT>(
            finalIndices.size() * sizeof(std::uint32_t));

    // создаём MeshGeometry
    mBoxGeo = std::make_unique<MeshGeometry>();
    mBoxGeo->Name = "sponzaGeo";

    ThrowIfFailed(
        D3DCreateBlob(vbByteSize,
            &mBoxGeo->VertexBufferCPU));

    CopyMemory(
        mBoxGeo->VertexBufferCPU->GetBufferPointer(),
        vertices.data(),
        vbByteSize);

    ThrowIfFailed(
        D3DCreateBlob(finalIbByteSize,
            &mBoxGeo->IndexBufferCPU));

    CopyMemory(
        mBoxGeo->IndexBufferCPU->GetBufferPointer(),
        finalIndices.data(),
        finalIbByteSize);

    mBoxGeo->VertexBufferGPU =
        d3dUtil::CreateDefaultBuffer(
            md3dDevice.Get(),
            mCommandList.Get(),
            vertices.data(),
            vbByteSize,
            mBoxGeo->VertexBufferUploader);

    if (finalIndices.empty())
    {
        throw std::runtime_error(
            "Sponza: final index buffer is empty.");
    }

    mBoxGeo->IndexBufferGPU =
        d3dUtil::CreateDefaultBuffer(
            md3dDevice.Get(),
            mCommandList.Get(),
            finalIndices.data(),
            finalIbByteSize,
            mBoxGeo->IndexBufferUploader);

    mBoxGeo->VertexByteStride = sizeof(Vertex);
    mBoxGeo->VertexBufferByteSize = vbByteSize;

    mBoxGeo->IndexFormat = DXGI_FORMAT_R32_UINT;
    mBoxGeo->IndexBufferByteSize = finalIbByteSize;

    // создаём Submesh для каждого материала
    UINT startIndex = 0;

    for (size_t materialId = 0;
        materialId < materialIndices.size();
        ++materialId)
    {
        const auto& materialList =
            materialIndices[materialId];

        if (materialList.empty())
            continue;

        SubmeshGeometry submesh;

        submesh.IndexCount =
            static_cast<UINT>(materialList.size());

        submesh.StartIndexLocation = startIndex;

        submesh.BaseVertexLocation = 0;

        std::string name =
            "material_" +
            std::to_string(materialId);

        mBoxGeo->DrawArgs[name] = submesh;

        startIndex += submesh.IndexCount;
    }
}

void BoxApp::BuildPSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS = 
	{ 
		reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()), 
		mvsByteCode->GetBufferSize() 
	};
    psoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()), 
		mpsByteCode->GetBufferSize() 
	};
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    psoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}

void BoxApp::BuildTextureSRV()
{
    UINT descriptorSize =
        md3dDevice->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (size_t i = 0; i < mTextures.size(); ++i)
    {
        auto texture = mTextures[i];

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        srvDesc.Format = texture->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels =
            texture->GetDesc().MipLevels;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        // первый дескриптор 0 уже занят CBV, поэтому текстуры начинаются с 1
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