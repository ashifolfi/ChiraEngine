#pragma once
//
// Shared DX Content
//
// The code that both device and backend use
//
#include <d3d11.h>
// TODO: see if we actually end up needing windows.h
//#include <windows.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
	
// Shortcut macros
#define dxRelease(x) if (x) { x->Release(); x = nullptr ; }
#define dxCreate(_type, _name) _type _name{}; memset(&_name, 0,sizeof(_type));

ID3D11Device* g_d3dDevice = nullptr;
ID3D11DeviceContext* g_d3dDeviceContext = nullptr;
