#pragma once

#include "napi.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <system_error>

class DesktopDuplication : public Napi::ObjectWrap<DesktopDuplication> {
	public:
		static Napi::Object Init(Napi::Env env, Napi::Object exports);
		
		DesktopDuplication(const Napi::CallbackInfo &info);
		void initialize(const Napi::CallbackInfo &info);
		Napi::Value getFrame(const Napi::CallbackInfo &info);
		~DesktopDuplication();

	private:
		static Napi::FunctionReference constructor;

		void cleanUp();

		ID3D11Device* m_Device;
		ID3D11DeviceContext* m_Context;
		IDXGIOutputDuplication* m_DesktopDup;
		UINT m_OutputNumber;
		DXGI_OUTPUT_DESC m_OutputDesc;
		ID3D11Texture2D* m_LastImage;
		ID3D11Texture2D* m_SharedImage;
};