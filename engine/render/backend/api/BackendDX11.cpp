#include "BackendDX11.h"
#include "../DXShared.h"

#include <cstddef>
#include <map>
#include <stack>
#include <string>

#include <imgui.h>
#include <ImGuizmo.h>

#include <SDL.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_dx11.h>

#include <core/Assertions.h>
#include <core/Logger.h>

//
// Framebuffer:		90%
// Mesh:			25%
// Shader:			0%
// Uniform:			0%
// Texture:			0%
//

using namespace chira;

CHIRA_CREATE_LOG(DX11);

enum class RenderMode {
	CULL_FACE,
	DEPTH_TEST,
	TEXTURE_CUBE_MAP_SEAMLESS,
};

struct DXVertex {
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT3 normal;
	DirectX::XMFLOAT4 color;
	DirectX::XMFLOAT2 texcoord;

	// Converts an existing vertex
	DXVertex(Vertex vtx)
		: position(vtx.position.x, vtx.position.y, vtx.position.z)
		// HACK: we cull front faces by default. no winding order reveral.
		, normal(vtx.normal.r, vtx.normal.g, vtx.normal.b)
		, color(vtx.color.r, vtx.color.g, vtx.color.b, 255.0f)
		, texcoord(vtx.uv.r, vtx.uv.g) {}

	// TODO: add other constructors
};

D3D11_RASTERIZER_DESC rasterizerDesc;
static void changeRenderMode(RenderMode mode, bool enable) {
	switch (mode) {
		case RenderMode::CULL_FACE:
			// HACK: cull front faces instead of back to maintain CCW vtx order
			rasterizerDesc.CullMode = enable ? D3D11_CULL_FRONT : D3D11_CULL_NONE;
			break;
		case RenderMode::DEPTH_TEST:
			rasterizerDesc.DepthClipEnable = enable;
			break;
		case RenderMode::TEXTURE_CUBE_MAP_SEAMLESS:
			// TODO: figure out what this is and what we'd do in DX
			return;
	}

	ID3D11RasterizerState* rasterizerState;
	g_d3dDevice->CreateRasterizerState(&rasterizerDesc, &rasterizerState);
	g_d3dDeviceContext->RSSetState(rasterizerState);
}

/// State controller to avoid redundant state changes: each state is false by default
std::map<RenderMode, std::stack<bool>> g_DXStates{
		{ RenderMode::CULL_FACE, {}, },
		{ RenderMode::DEPTH_TEST, {}, },
		{ RenderMode::TEXTURE_CUBE_MAP_SEAMLESS, {}, },
};
static void initStates() {
	// get the current render state here
	ID3D11RasterizerState* rasterizerState;
	g_d3dDeviceContext->RSGetState(&rasterizerState);
	rasterizerState->GetDesc(&rasterizerDesc); // build off our default state

	g_DXStates[RenderMode::CULL_FACE].push(true);
	g_DXStates[RenderMode::DEPTH_TEST].push(true);
	g_DXStates[RenderMode::TEXTURE_CUBE_MAP_SEAMLESS].push(true);

	for (const auto& [renderMode, stack] : g_DXStates) {
		changeRenderMode(renderMode, stack.top());
	}
}

static void pushState(RenderMode mode, bool enable) {
	static bool initedStates = false;
	if (!initedStates) {
		initStates();
		initedStates = true;
	}
	runtime_assert(g_DXStates.contains(mode), "This render mode was not added to initStates()!!");
	auto& stack = g_DXStates[mode];
	bool current = stack.top();
	stack.push(enable);
	if (enable != current) {
		changeRenderMode(mode, enable);
	}
}

static void popState(RenderMode mode) {
	if (!g_DXStates.contains(mode) || g_DXStates[mode].size() <= 1) {
		runtime_assert(false, "Attempted to pop render state without a corresponding push!");
	}
	auto& stack = g_DXStates[mode];
	bool old = stack.top();
	stack.pop();
	if (stack.top() != old) {
		changeRenderMode(mode, stack.top());
	}
}

std::string_view Renderer::getHumanName() {
	return "DirectX 11";
}

bool Renderer::setupForDebugging() {
	// this does nothing in DX. Debugging is initialized at device creation time.
	// you can't toggle it on and off on a whim.
	return IMGUI_CHECKVERSION();
}

static float g_d3dClearColor[4] = { 0.0f,0.0f,0.0f,0.0f };
void Renderer::setClearColor(ColorRGBA color) {
	g_d3dClearColor[0] = color.r * color.a;
	g_d3dClearColor[1] = color.g * color.a;
	g_d3dClearColor[2] = color.b * color.a;
	g_d3dClearColor[3] = color.a;
}

std::stack<Renderer::FrameBufferHandle> g_DXFramebuffers{};

// creates a viewport of the defined size and depth if specified
static void dxViewport(float x, float y, float w, float h, bool hasDepth) {
	CD3D11_VIEWPORT viewport(x, y, w, h);
	if (hasDepth) {
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
	}
	g_d3dDeviceContext->RSSetViewports(1, &viewport);
}

Renderer::FrameBufferHandle Renderer::createFrameBuffer(int width, int height, WrapMode wrapS, WrapMode wrapT, FilterMode filter, bool hasDepth) {
	FrameBufferHandle handle{ .hasDepth = hasDepth, .width = width, .height = height, };
	// Describe the texture for our render target view
	// Describe our Depth/Stencil Buffer
	D3D11_TEXTURE2D_DESC frameBufferDesc{};

	frameBufferDesc.Width = width;
	frameBufferDesc.Height = height;
	frameBufferDesc.MipLevels = 1;
	frameBufferDesc.ArraySize = 1;
	frameBufferDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	frameBufferDesc.SampleDesc.Count = 1;
	frameBufferDesc.SampleDesc.Quality = 0;
	frameBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	frameBufferDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
	frameBufferDesc.CPUAccessFlags = 0;
	frameBufferDesc.MiscFlags = 0;

	// TODO: Filter modes & wrap modes

	g_d3dDevice->CreateTexture2D(&frameBufferDesc, NULL, &handle.renderTargetBuffer);
	g_d3dDevice->CreateRenderTargetView(handle.renderTargetBuffer, nullptr, &handle.renderTargetView);

	if (hasDepth) {
		// Credit to braynzarsoft for the below init code:
		// https://www.braynzarsoft.net/viewtutorial/q16390-7-depth

		// Describe our Depth/Stencil Buffer
		D3D11_TEXTURE2D_DESC depthStencilDesc{};

		depthStencilDesc.Width = width;
		depthStencilDesc.Height = height;
		depthStencilDesc.MipLevels = 1;
		depthStencilDesc.ArraySize = 1;
		depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthStencilDesc.SampleDesc.Count = 1;
		depthStencilDesc.SampleDesc.Quality = 0;
		depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
		depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		depthStencilDesc.CPUAccessFlags = 0;
		depthStencilDesc.MiscFlags = 0;

		g_d3dDevice->CreateTexture2D(&depthStencilDesc, NULL, &handle.depthStencilBuffer);
		g_d3dDevice->CreateDepthStencilView(handle.depthStencilBuffer, NULL, &handle.depthStencilView);
	}

	return handle;
}

void Renderer::pushFrameBuffer(Renderer::FrameBufferHandle handle) {
	auto old = g_DXFramebuffers.empty() ? nullptr : g_DXFramebuffers.top().renderTargetView;
	g_DXFramebuffers.push(handle);
	if (old != (g_DXFramebuffers.empty() ? nullptr : g_DXFramebuffers.top().renderTargetView)) {
		dxViewport(0.0f, 0.0f,
				   static_cast<float>(g_DXFramebuffers.top().width), static_cast<float>(g_DXFramebuffers.top().height),
				   g_DXFramebuffers.top().hasDepth);

		if (g_DXFramebuffers.top().hasDepth) {
			g_d3dDeviceContext->OMSetRenderTargets(1,
												   &g_DXFramebuffers.top().renderTargetView, g_DXFramebuffers.top().depthStencilView);
		} else {
			// no depth so no depth buffer
			g_d3dDeviceContext->OMSetRenderTargets(1, &g_DXFramebuffers.top().renderTargetView, nullptr);
		}
		pushState(RenderMode::DEPTH_TEST, g_DXFramebuffers.top().hasDepth);
	}

	if (g_DXFramebuffers.top().hasDepth) {
		g_d3dDeviceContext->ClearDepthStencilView(handle.depthStencilView,
												  D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		g_d3dDeviceContext->ClearRenderTargetView(handle.renderTargetView, g_d3dClearColor);
	} else {
		g_d3dDeviceContext->ClearRenderTargetView(handle.renderTargetView, g_d3dClearColor);
	}
}

void Renderer::popFrameBuffer() {
	runtime_assert(!g_DXFramebuffers.empty(), "Attempted to pop framebuffer without a corresponding push!");
	auto old = g_DXFramebuffers.top().renderTargetView;

	g_DXFramebuffers.pop();
	if (old != (g_DXFramebuffers.empty() ? nullptr : g_DXFramebuffers.top().renderTargetView)) {
		if (!g_DXFramebuffers.empty()) {
			dxViewport(0.0f, 0.0f,
					   static_cast<float>(g_DXFramebuffers.top().width), static_cast<float>(g_DXFramebuffers.top().height),
					   g_DXFramebuffers.top().hasDepth);

			if (g_DXFramebuffers.top().hasDepth) {
				g_d3dDeviceContext->OMSetRenderTargets(1,
													   &g_DXFramebuffers.top().renderTargetView, g_DXFramebuffers.top().depthStencilView);
			} else {
				// no depth so no depth buffer
				g_d3dDeviceContext->OMSetRenderTargets(1, &g_DXFramebuffers.top().renderTargetView, nullptr);
			}
		} else {
			// TODO: does this work?
			g_d3dDeviceContext->OMSetRenderTargets(1, nullptr, nullptr);
		}
		popState(RenderMode::DEPTH_TEST);
	}
}

void Renderer::useFrameBufferTexture(const Renderer::FrameBufferHandle handle, TextureUnit activeTextureUnit) {
	if (handle.renderTargetView) {

	} else if (!handle) {
		LOG_DX11.error("useFrameBufferTexture called with 0 parameters!");
		// TODO: openGL binds a framebuffer of 0 here. what does that do?
	}
}

void* Renderer::getImGuiFrameBufferHandle(Renderer::FrameBufferHandle handle) {
	return;
	// TODO: figure out what this is used for if anything.
	//return reinterpret_cast<void*>(static_cast<unsigned long long>(handle.colorHandle));
}

void Renderer::destroyFrameBuffer(Renderer::FrameBufferHandle handle) {
	if (!handle) {
		return;
	}
	if (handle.hasDepth) {
		dxRelease(handle.depthStencilView);
		dxRelease(handle.depthStencilBuffer);
	}
	dxRelease(handle.renderTargetView);
	dxRelease(handle.renderTargetBuffer);
}

int Renderer::getFrameBufferWidth(Renderer::FrameBufferHandle handle) {
	return handle.width;
}

int Renderer::getFrameBufferHeight(Renderer::FrameBufferHandle handle) {
	return handle.height;
}

D3D11_INPUT_ELEMENT_DESC layout[] = {
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(DXVertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(DXVertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(DXVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(DXVertex, texcoord), D3D11_INPUT_PER_VERTEX_DATA}
};

Renderer::MeshHandle Renderer::createMesh(const std::vector<Vertex>& vertices, const std::vector<Index>& indices, MeshDrawMode drawMode) {
	MeshHandle handle{ .numIndices = static_cast<int>(indices.size()) };

	// And now we create a new vertex array of DXVertex
	std::vector<DXVertex> dxVertices;
	for (Vertex oVtx : vertices) {
		DXVertex nVtx(oVtx);
		dxVertices.push_back(nVtx);
	}

	// Fill in a buffer description.
	D3D11_BUFFER_DESC vtxBufferDesc{};
	vtxBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	vtxBufferDesc.ByteWidth = sizeof(dxVertices.data());
	vtxBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vtxBufferDesc.CPUAccessFlags = 0;
	vtxBufferDesc.MiscFlags = 0;

	// Fill in the subresource data.
	D3D11_SUBRESOURCE_DATA vtxInitData{};
	vtxInitData.pSysMem = dxVertices.data();
	vtxInitData.SysMemPitch = 0;
	vtxInitData.SysMemSlicePitch = 0;

	// Create the vertex buffer.
	g_d3dDevice->CreateBuffer(&vtxBufferDesc, &vtxInitData, &handle.vertexBuffer);

	// Fill in a buffer description.
	D3D11_BUFFER_DESC idxBufferDesc{};
	idxBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	idxBufferDesc.ByteWidth = sizeof(indices.data());
	idxBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	idxBufferDesc.CPUAccessFlags = 0;
	idxBufferDesc.MiscFlags = 0;

	// Fill in the subresource data.
	D3D11_SUBRESOURCE_DATA idxInitData{};
	idxInitData.pSysMem = indices.data();
	idxInitData.SysMemPitch = 0;
	idxInitData.SysMemSlicePitch = 0;

	// Create the vertex buffer
	g_d3dDevice->CreateBuffer(&idxBufferDesc, &idxInitData, &handle.indexBuffer);
	return handle;
}

// TODO: Convert this function to use DX11
void Renderer::updateMesh(MeshHandle* handle, const std::vector<Vertex>& vertices, const std::vector<Index>& indices, MeshDrawMode drawMode) {
	runtime_assert(static_cast<bool>(*handle), "Invalid mesh handle given to DX11 renderer!");
	const auto glDrawMode = getMeshDrawModeGL(drawMode);
	glBindBuffer(GL_ARRAY_BUFFER, handle->vboHandle);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), glDrawMode);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, handle->eboHandle);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(Index)), indices.data(), glDrawMode);
	handle->numIndices = static_cast<int>(indices.size());
}

// TODO: Convert this function to use DX11
void Renderer::drawMesh(MeshHandle handle, MeshDepthFunction depthFunction, MeshCullType cullType) {
	runtime_assert(static_cast<bool>(handle), "Invalid mesh handle given to DX11 renderer!");
	// create an input array and bind it to the buffer

	pushState(RenderMode::CULL_FACE, true);
	glDepthFunc(getMeshDepthFunctionGL(depthFunction));
	glCullFace(getMeshCullTypeGL(cullType));
	glBindVertexArray(handle.vaoHandle);
	glDrawElements(GL_TRIANGLES, handle.numIndices, GL_UNSIGNED_INT, nullptr);
	popState(RenderMode::CULL_FACE);
}

// TODO: Convert this function to use DX11
void Renderer::destroyMesh(MeshHandle handle) {
	runtime_assert(static_cast<bool>(handle), "Invalid mesh handle given to DX11 renderer!");
	glDeleteVertexArrays(1, &handle.vaoHandle);
	glDeleteBuffers(1, &handle.vboHandle);
	glDeleteBuffers(1, &handle.eboHandle);
}

void Renderer::initImGui(SDL_Window* window) {
	ImGui_ImplSDL2_InitForD3D(window);
	// we pass over our global device and device context
	ImGui_ImplDX11_Init(g_d3dDevice, g_d3dDeviceContext);
}

void Renderer::startImGuiFrame() {
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();
	ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_AutoHideTabBar | ImGuiDockNodeFlags_PassthruCentralNode);
}

void Renderer::endImGuiFrame() {
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void Renderer::destroyImGui() {
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplSDL2_Shutdown();
}
