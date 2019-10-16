#pragma once

#include "napi.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <system_error>

#include "types.h"
#include "getframeasyncworker.h"

class DesktopDuplication : public Napi::ObjectWrap<DesktopDuplication> {
	public:
		static Napi::Object Init(Napi::Env env, Napi::Object exports);
		
		static Napi::Number getMonitorCount(const Napi::CallbackInfo &info);

		DesktopDuplication(const Napi::CallbackInfo &info);
		std::string initialize();
		void wrap_initialize(const Napi::CallbackInfo &info);
		FRAME_DATA getFrame(UINT timeout);
		FRAME_DATA getFrameThread(UINT timeout);
		Napi::Value wrap_getFrame(const Napi::CallbackInfo &info);
		void getFrameAsync(const Napi::CallbackInfo &info);
		Napi::Value startAutoCapture(const Napi::CallbackInfo &info);
		bool stopAutoCapture();
		Napi::Value wrap_stopAutoCapture(const Napi::CallbackInfo &info);

		~DesktopDuplication();

	private:
		static Napi::FunctionReference constructor;
		static void autoCaptureFnJsCallback(Napi::Env env, Napi::Function fn, FRAME_DATA* frame);

		void cleanUp();
		void autoCaptureFn(int delay);
		FRAME_DATA getFrameData(ID3D11Texture2D* texture, D3D11_TEXTURE2D_DESC& textureDesc);

		ID3D11Device* m_Device;
		ID3D11DeviceContext* m_Context;
		IDXGIOutputDuplication* m_DesktopDup;
		UINT m_OutputNumber;
		DXGI_OUTPUT_DESC m_OutputDesc;
		ID3D11Texture2D* m_LastImage;

		ID3D11Texture2D* m_LastImageThread;
		ID3D11Texture2D* m_stagingTextureThread;

		std::thread m_autoCaptureThread;
		bool m_autoCaptureThreadStarted;
		std::promise<void> m_autoCaptureThreadSignal;
		Napi::ThreadSafeFunction m_autoCaptureThreadCallback;
};