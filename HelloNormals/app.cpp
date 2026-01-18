#include "stdafx.h"
#include <wrl/client.h>
#include "app.h"
#include "platform_win32.h"
#include "DXSampleHelper.h"

platform plat;

app::app(UINT width, UINT height, std::wstring name, HINSTANCE hInstance, int nCmdShow) : IApp(width, height, name), 
	m_width(width), 
	m_height(height),
	m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
	m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
	m_vertexBufferView{},
	m_indexBufferView{},
	m_constantDataGpuAddr{},
	m_mappedConstantData(nullptr),
	m_rtvDescriptorSize(0),
	m_frameIndex(0),
	m_fenceEvent(nullptr),
	m_fenceValues{},
	m_curRotationAngleRad(0)
{
	plat = platform(width, height, name, hInstance, nCmdShow, this);

	WCHAR assetsPath[512];
	GetAssetsPath(assetsPath, _countof(assetsPath));
	m_assetsPath = assetsPath;

	m_aspectRatio = static_cast<float>(width) / static_cast<float>(height);

	m_worldMatrix = XMMatrixIdentity();

	static const XMVECTORF32 c_eye = { 0.f, 3.f, -10.f, 0.f };
	static const XMVECTORF32 c_at = { 0.f, 0.f, 0.f, 0.f };
	static const XMVECTORF32 c_up = { 0.f, 1.f, 0.f, 0.f };
	m_viewMatrix = XMMatrixLookAtLH(c_eye, c_at, c_up);
	
	m_projectionMatrix = XMMatrixPerspectiveFovLH(XM_PIDIV4, m_aspectRatio, .01f, 100.f);

	// Initialize the lighting parameters
	m_lightDir = XMVectorSet(-0.577f, 0.577f, -0.577f, 0.0f);
	m_lightColor = XMVectorSet(0.9f, 0.9f, 0.9f, 1.0f);

	// Initialize the scene output color
	m_outputColor = XMVectorSet(0, 0, 0, 0);
}
app::~app() {}

void app::OnInit() 
{
	app::LoadPipeline();
	app::LoadAssets();
	plat.PlatShowWindow();
}
void app::OnDestroy() {}
void app::Run() 
{
	MSG msg = { 0 };

	while (msg.message != WM_QUIT)
	{
		int wParam = plat.PlatMessageDispatch(msg);
	}
}

void app::OnUpdate() 
{
	const float rotationSpeed = .01f;

	m_curRotationAngleRad += rotationSpeed;
	if (m_curRotationAngleRad >= XM_2PI)
	{
		m_curRotationAngleRad -= XM_2PI;
	}

	// Rotate the cube around the Y-axis
	m_worldMatrix = XMMatrixRotationY(m_curRotationAngleRad);
}
void app::OnRender() 
{
	// Record all the commands we need to render the scene into the command list.
	PopulateCommandList();

	// Execute the command list.
	ID3D12CommandList* ppCommandList[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandList), ppCommandList);

	// Present the frame.
	ThrowIfFailed(m_swapChain->Present(1, 0));

	MoveToNextFrame();
}

void app::PopulateCommandList() 
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before re-recording.
	// Set PSO for drawing lambertian lit objects.
	ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_lambertPipelineState.Get()));

	// Set necessary state.
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	// Index into the available constant buffers based on the number
	// of draw calls. We've allocated enough for a known number of
	// draw calls per frame times the number of back buffers
	unsigned int constantBufferIndex = c_numDrawCalls * (m_frameIndex % FrameCount);

	// Bind the constants to the shader
	auto baseGpuAddress = m_constantDataGpuAddr + sizeof(PaddedConstantBuffer) * constantBufferIndex;
	m_commandList->SetGraphicsRootConstantBufferView(0, baseGpuAddress);

	// Indicate that the back buffer will be used as a render target.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Set render target and depth buffer in OM stage
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	// Record commands.
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

	// Set up the input assembler
	m_commandList->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	m_commandList->IASetIndexBuffer(&m_indexBufferView);

	// Set the per-frame constants
	ConstantBuffer cbParameters{};

	// Shaders compiled with default row-major matrices
	XMStoreFloat4x4(&cbParameters.worldMatrix, XMMatrixTranspose(m_worldMatrix));
	XMStoreFloat4x4(&cbParameters.viewMatrix, XMMatrixTranspose(m_viewMatrix));
	XMStoreFloat4x4(&cbParameters.projectionMatrix, XMMatrixTranspose(m_projectionMatrix));

	XMStoreFloat4(&cbParameters.lightDir, m_lightDir);
	XMStoreFloat4(&cbParameters.lightColor, m_lightColor);
	XMStoreFloat4(&cbParameters.outputColor, m_outputColor);

	// Set the constants for the first draw call
	memcpy(&m_mappedConstantData[constantBufferIndex], &cbParameters, sizeof(ConstantBuffer));

	// Draw the Lambert lit sphere
	m_commandList->DrawIndexedInstanced((UINT)sphereIndices.size(), 1, 0, 0, 0);
	baseGpuAddress += sizeof(PaddedConstantBuffer);
	++constantBufferIndex;

	// Set the PSO for drawing normals with a solid color
	m_commandList->SetPipelineState(m_normalsPipelineState.Get());

	// Set yellow as solid color
	m_outputColor = XMVectorSet(1, 1, 0, 0);
	XMStoreFloat4(&cbParameters.outputColor, m_outputColor);

	// Set the constants for the second draw call
	memcpy(&m_mappedConstantData[constantBufferIndex], &cbParameters, sizeof(ConstantBuffer));

	// Bind the constants to the shader
	baseGpuAddress = m_constantDataGpuAddr + sizeof(PaddedConstantBuffer) * constantBufferIndex;
	m_commandList->SetGraphicsRootConstantBufferView(0, baseGpuAddress);

	// Draw the normals of the sphere with the help of the GS.
	m_commandList->DrawIndexedInstanced((UINT)sphereIndices.size(), 1, 0, 0, 0);

	// Indicate that the back buffer will now be used to present.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	ThrowIfFailed(m_commandList->Close());
}

void app::LoadPipeline() {
	UINT dxgiFactoryFlags = 0;

	ComPtr<ID3D12Debug> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
	{
		debugController->EnableDebugLayer();

		dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}

	ComPtr<IDXGIFactory7> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	// Seeking a device that supports DX12
	{
		ComPtr<IDXGIAdapter1> adapter;

		for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				continue;
			}

			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_2, _uuidof(ID3D12Device), nullptr)))
			{
				break;
			}
		}
		ThrowIfFailed(D3D12CreateDevice(
			adapter.Get(),
			D3D_FEATURE_LEVEL_12_2,
			IID_PPV_ARGS(&m_device)
		));
	}

	// Describe and create the command queue.
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		ThrowIfFailed(m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_commandQueue)));
	}

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = m_width;
	swapChainDesc.Height = m_height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	ThrowIfFailed(factory->CreateSwapChainForHwnd(
		m_commandQueue.Get(),
		plat.GetHwnd(),
		&swapChainDesc,
		nullptr,	
		nullptr,
		&swapChain
	));

	ThrowIfFailed(factory->MakeWindowAssociation(plat.GetHwnd(), DXGI_MWA_NO_ALT_ENTER));
	ThrowIfFailed(swapChain.As(&m_swapChain));
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

		// Describe and create a depth stencil view (DSV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	// Create frame resources
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV and a command allocator for each frame.
		for (UINT n = 0; n < FrameCount; n++)
		{
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorSize);
			
			ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
		}
	}

	// Create the depth stencil view.
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
		dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE),
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&CD3DX12_CLEAR_VALUE(DXGI_FORMAT_D32_FLOAT, 1.f, 0),
			IID_PPV_ARGS(&m_depthStencil)
		));

		m_device->CreateDepthStencilView(m_depthStencil.Get(), &dsvDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	}
}
void app::LoadAssets() {

	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData{};
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

	if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	{
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}

	// Root signature
	{
		CD3DX12_ROOT_PARAMETER1 rp[1]{};
		rp[0].InitAsConstantBufferView(0, 0);

		// Allow input layout and deny uneccessary access to certain pipeline stages.
		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};
		rootSignatureDesc.Init_1_1(_countof(rp), rp, 0, nullptr, rootSignatureFlags);

		ComPtr<ID3D10Blob> signature;
		ComPtr<ID3D10Blob> error;
		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
	}

	// Create the constant buffer memory and map the resource
	{
		const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		size_t cbSize = c_numDrawCalls * FrameCount * sizeof(PaddedConstantBuffer);

		const D3D12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);
		ThrowIfFailed(m_device->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&constantBufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(m_perFrameConstants.ReleaseAndGetAddressOf())
		));
		ThrowIfFailed(m_perFrameConstants->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedConstantData)));
	
		m_constantDataGpuAddr = m_perFrameConstants->GetGPUVirtualAddress();
	}

	// Create the pipeline state objects, which includes compiling and loading shaders.
	{
		ComPtr<ID3D10Blob> mainVS, passThroughVS, mainGS, lambertPS, solidColorPS;

		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

		ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "MainVS", "vs_5_0", compileFlags, 0, &mainVS, nullptr));
		ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "PassThroughVS", "vs_5_0", compileFlags, 0, &passThroughVS, nullptr));
		ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "MainGS", "gs_5_0", compileFlags, 0, &mainGS, nullptr));
		ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "LambertPS", "ps_5_0", compileFlags, 0, &lambertPS, nullptr));
		ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "SolidColorPS", "ps_5_0", compileFlags, 0, &solidColorPS, nullptr));

		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = 
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
		};

		// Create the Pipeline State Objects
		{
			// Describe and create the graphics pipeline state object (PSO).
			D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc {};

			//
			// PSO for drawing lambertian lit objects
			//
			psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
			psoDesc.pRootSignature = m_rootSignature.Get();
			psoDesc.VS = CD3DX12_SHADER_BYTECODE(mainVS.Get());
			psoDesc.PS = CD3DX12_SHADER_BYTECODE(lambertPS.Get());
			psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			psoDesc.SampleMask = UINT_MAX;
			psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			psoDesc.NumRenderTargets = 1;
			psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			psoDesc.SampleDesc.Count = 1;
			ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_lambertPipelineState)));

			//
			// PSO for drawing normals with a solid color
			//
			psoDesc.VS = CD3DX12_SHADER_BYTECODE(passThroughVS.Get());
			psoDesc.GS = CD3DX12_SHADER_BYTECODE(mainGS.Get());
			psoDesc.PS = CD3DX12_SHADER_BYTECODE(solidColorPS.Get());
			ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_normalsPipelineState)));
		}
	}

	// Create the command list.
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)));

	// Command lists are created in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now.
	ThrowIfFailed(m_commandList->Close());

	// Create the vertex and index buffers.
	{
		// Define the geometry for a sphere.
		ComputeSphere(sphereVertices, sphereIndices, 5, 20);

		// Note: using upload heaps to transfer static data like vert buffers is not 
		// recommended. Every time the GPU needs it, the upload heap will be marshalled 
		// over. Please read up on Default Heap usage. An upload heap is used here for 
		// code simplicity and because there are very few verts to actually transfer.
		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(sphereVertices.size() * sizeof(Vertex)),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_vertexBuffer)
		));
		// Copy the triangle data to the vertex buffer.
		UINT8* pVertexDataBegin = nullptr;
		CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
		ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, sphereVertices.data(), sphereVertices.size() * sizeof(Vertex));
		m_vertexBuffer->Unmap(0, nullptr);

		// Initialize the vertex buffer view.
		m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferView.StrideInBytes = sizeof(Vertex);
		m_vertexBufferView.SizeInBytes = (UINT) sphereVertices.size() * sizeof(Vertex);

		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(sphereIndices.size() * sizeof(UINT16)),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_indexBuffer)
		));

		ThrowIfFailed(m_indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, sphereIndices.data(), sphereIndices.size() * sizeof(UINT16));
		m_indexBuffer->Unmap(0, nullptr);

		m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
		m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
		m_indexBufferView.SizeInBytes = (UINT) sphereIndices.size() * sizeof(UINT16);	
	}

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
		m_fenceValues[m_frameIndex]++;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForGPU();
	}
}

void app::OnKeyDown(UINT8 key) 
{

}
void app::OnKeyUp(UINT8 key) 
{
	if (key == VK_ESCAPE)
	{
		PostQuitMessage(0);
	}
}

void app::MoveToNextFrame() 
{
	const UINT64 currentFrameValue = m_fenceValues[m_frameIndex];
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFrameValue));

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Wait until the previous frame is finished.
	if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
		WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
	}

	m_fenceValues[m_frameIndex] = currentFrameValue + 1;
}
void app::WaitForGPU() 
{
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));

	ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
	WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
	
	m_fenceValues[m_frameIndex]++;
}

void app::ComputeSphere(std::vector<Vertex>& vertices, std::vector<UINT16>& indices, FLOAT diameter, UINT16 tessellation) 
{
	vertices.clear();
	indices.clear();

	if (tessellation < 3) 
		throw std::invalid_argument("tesselation parameter must be at least 3");
	
	//
	// Fill the array of vertices using spherical coordinates
	//

	CONST UINT16 stackCount = tessellation;
	CONST UINT16 sliceCount = tessellation * 2;

	CONST FLOAT radius = diameter / 2.f;

	// Create rings of vertices at progressively higher latitudes.
	// We have stackCount + 1 vertical rings of vertices (the first and last vert. rings degenerate to points).
	for (UINT16 i = 0; i <= stackCount; i++)
	{
		// -90° < latitude < +90°
		CONST FLOAT latitude = (FLOAT(i) * XM_PI / FLOAT(stackCount)) - XM_PIDIV2;
		FLOAT dy, dxz;

		// dy = sin(phi),  dxz = cos(phi)
		XMScalarSinCos(&dy, &dxz, latitude);

		// Create the vertices of the vertical ring at the i-th latitude.
		// We have sliceCount vertices for each vertical ring. However, we need to
		// distinguish between the first and last vertices, which are at the same
		// position but with different normals, texture coordinates, and so on.
		for (UINT16 j = 0; j <= sliceCount; j++)
		{
			// 0° < longitude < 360°
			CONST FLOAT longitude = FLOAT(j) * XM_2PI / FLOAT(sliceCount);
			FLOAT dx, dz;

			// dx = cos(theta),  dz = sin(theta)
			XMScalarSinCos(&dz, &dx, longitude);

			// dx = cos(phi)cos(theta)
			// dy = sin(phi)
			// dz = cos(phi)sin(theta)
			dx *= dxz;
			dz *= dxz;

			// normal = (dx, dy, dz)
			// position = r * (dx, dy, dz)
			XMFLOAT3 position, normal;
			XMStoreFloat3(&position, XMVectorScale(XMVectorSet(dx, dy, dz, 0), radius));
			XMStoreFloat3(&normal, XMVectorSet(dx, dy, dz, 0));

			vertices.push_back({ position, normal });
		}
	}

	//
	// Fill the array of indices of triangles joining each pair of vertical rings.
	//

	// As noted above, we distinguish sliceCount + 1 vertices for each rings, 
	// so we must skip (sliceCount + 1) vertices every time we need to build 
	// the indices of the triangles that compose the sliceCount quads of each 
	// of the stackCount stacks.
	CONST UINT16 stride = sliceCount + 1;

	for (UINT16 i = 0; i < stackCount; i++)
	{
		for (UINT16 j = 0; j < sliceCount; j++)
		{
			CONST UINT16 nextI = (i + 1);
			CONST UINT16 nextJ = (j + 1) % stride;

			indices.push_back(i * stride + j);
			indices.push_back(nextI * stride + j);
			indices.push_back(nextI * stride + nextJ);

			indices.push_back(i * stride + j);
			indices.push_back(nextI * stride + nextJ);
			indices.push_back(i * stride + nextJ);
		}
	}
}
