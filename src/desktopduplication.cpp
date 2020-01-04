#include "desktopduplication.h"

Napi::Number DesktopDuplication::getMonitorCount(const Napi::CallbackInfo &info) {
	Napi::Env env = info.Env();

	int monitors = GetSystemMetrics(SM_CMONITORS);

	return Napi::Number::New(env, (double)monitors);
}

DesktopDuplication::DesktopDuplication(const Napi::CallbackInfo &info) : 
	Napi::ObjectWrap<DesktopDuplication>(info), 
	m_Device(nullptr), 
	m_Context(nullptr), 
	m_DesktopDup(nullptr), 
	m_LastImage(nullptr),
	m_LastImageThread(nullptr),
	m_stagingTextureThread(nullptr),
	m_autoCaptureThreadStarted(false)
{
	UINT outputNum = info[0].As<Napi::Number>().Uint32Value();
	m_OutputNumber = outputNum;

	RtlZeroMemory(&m_OutputDesc, sizeof(m_OutputDesc));
}

std::string DesktopDuplication::initialize() {
	// call cleanup so we can call this function multiple times without memory leaks
	cleanUp();

	HRESULT hr = S_OK;

	// Driver types supported
	D3D_DRIVER_TYPE DriverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

	// Feature levels supported
	D3D_FEATURE_LEVEL FeatureLevels[] = {
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_1
	};
	UINT NumFeatureLevels = ARRAYSIZE(FeatureLevels);

	D3D_FEATURE_LEVEL FeatureLevel;

	// Create device
	for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex) {
		hr = D3D11CreateDevice(nullptr, DriverTypes[DriverTypeIndex], nullptr, 0, FeatureLevels, NumFeatureLevels, D3D11_SDK_VERSION, &m_Device, &FeatureLevel, &m_Context);
		if (SUCCEEDED(hr)) {
			// Device creation success, no need to loop anymore
			break;
		}
	}
	if (FAILED(hr)) {
		return "Failed to create device: " + std::system_category().message(hr);
	}

	 // Get DXGI device
	IDXGIDevice* DxgiDevice = nullptr;
	hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&DxgiDevice));
	if (FAILED(hr)) {
		cleanUp();
		return "Failed to query interface for DXGI Device: " + std::system_category().message(hr);
	}

	// Get DXGI adapter
	IDXGIAdapter* DxgiAdapter = nullptr;
	hr = DxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&DxgiAdapter));
	DxgiDevice->Release();
	DxgiDevice = nullptr;
	if (FAILED(hr)) {
		cleanUp();
		return "Failed to get parent DXGI Adapter: " + std::system_category().message(hr);
	}

	// Get output
	IDXGIOutput* DxgiOutput = nullptr;
	hr = DxgiAdapter->EnumOutputs(m_OutputNumber, &DxgiOutput);
	DxgiAdapter->Release();
	DxgiAdapter = nullptr;
	if (FAILED(hr)) {
		cleanUp();
		return "Failed to get specified output: " + std::system_category().message(hr);
	}

	DxgiOutput->GetDesc(&m_OutputDesc);

	// QI for Output 1
	IDXGIOutput1* DxgiOutput1 = nullptr;
	hr = DxgiOutput->QueryInterface(__uuidof(DxgiOutput1), reinterpret_cast<void**>(&DxgiOutput1));
	DxgiOutput->Release();
	DxgiOutput = nullptr;
	if (FAILED(hr)) {
		cleanUp();
		return "Failed to query interface for DxgiOutput1: " + std::system_category().message(hr);
	}

	// Create desktop duplication
	hr = DxgiOutput1->DuplicateOutput(m_Device, &m_DesktopDup);
	DxgiOutput1->Release();
	DxgiOutput1 = nullptr;
	if (FAILED(hr)) {
		if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
			return "There is already the maximum number of applications using the Desktop Duplication API running, please close one of those applications and then try again.";
		}
		cleanUp();
		return "Failed to get duplicate output: " + std::system_category().message(hr);
	}

	// throw away one frame which seems to always be empty
	FRAME_DATA throwaway_frame = getFrame(1000);
	if (throwaway_frame.result == RESULT_SUCCESS) {
		free(throwaway_frame.data);
	}

	return "";
}

void DesktopDuplication::wrap_initialize(const Napi::CallbackInfo &info) {
	Napi::Env env = info.Env();

	std::string error = initialize();

	if (error != "") {
		Napi::Error::New(env, error).ThrowAsJavaScriptException();
	}
}

FRAME_DATA DesktopDuplication::getFrame(UINT timeout) {
	FRAME_DATA result;	

	IDXGIResource* DesktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO FrameInfo;

    // Get new frame
    HRESULT hr = m_DesktopDup->AcquireNextFrame(timeout, &FrameInfo, &DesktopResource);

	if (hr == DXGI_ERROR_ACCESS_LOST) {
		result.result = RESULT_ACCESSLOST;
		m_DesktopDup->ReleaseFrame();
		return result;
    }

	if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
		result.result = RESULT_TIMEOUT;
		m_DesktopDup->ReleaseFrame();
		return result;
    }

	if (FAILED(hr)) {
		result.result = RESULT_ERROR;
		result.error = "Failed to aquire next frame: " + std::system_category().message(hr);
		m_DesktopDup->ReleaseFrame();
		return result;
	}

	// If still holding old frame, destroy it
	if (m_LastImage) {
		m_LastImage->Release();
		m_LastImage = nullptr;
	}

	// QI for IDXGIResource
	hr = DesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&m_LastImage));
	DesktopResource->Release();
	DesktopResource = nullptr;

	if (FAILED(hr)) {
		result.result = RESULT_ERROR;
		result.error = "Failed to QI for ID3D11Texture2D from acquired IDXGIResource: " + std::system_category().message(hr);
		m_DesktopDup->ReleaseFrame();
		return result;
	}

	D3D11_TEXTURE2D_DESC frameDesc;
	m_LastImage->GetDesc(&frameDesc);

	D3D11_TEXTURE2D_DESC stagingTextureDesc;
	stagingTextureDesc.Width = frameDesc.Width;
	stagingTextureDesc.Height = frameDesc.Height;
	stagingTextureDesc.MipLevels = frameDesc.MipLevels;
	stagingTextureDesc.ArraySize = 1;
	stagingTextureDesc.Format = frameDesc.Format;
	stagingTextureDesc.SampleDesc = frameDesc.SampleDesc;
	stagingTextureDesc.Usage = D3D11_USAGE_STAGING;
	stagingTextureDesc.BindFlags = 0;
	stagingTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingTextureDesc.MiscFlags = 0;

	ID3D11Texture2D* stagingTexture = nullptr;

	// create shared surface to copy aquired image to
	hr = m_Device->CreateTexture2D(&stagingTextureDesc, nullptr, &stagingTexture);
	if (FAILED(hr)) {
		result.result = RESULT_ERROR;
		result.error = "Failed to create shared surface: " + std::system_category().message(hr);
		m_DesktopDup->ReleaseFrame();
		return result;
	}		
	
	// copy frame into shared texture
	m_Context->CopyResource(stagingTexture, m_LastImage);

	result = getFrameData(stagingTexture, stagingTextureDesc);

	stagingTexture->Release();
	m_DesktopDup->ReleaseFrame();

	return result;
}

FRAME_DATA DesktopDuplication::getFrameThread(UINT timeout) {
	FRAME_DATA result;	

	IDXGIResource* DesktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO FrameInfo;

    // Get new frame
    HRESULT hr = m_DesktopDup->AcquireNextFrame(timeout, &FrameInfo, &DesktopResource);

	if (hr == DXGI_ERROR_ACCESS_LOST) {
		result.result = RESULT_ACCESSLOST;
		m_DesktopDup->ReleaseFrame();
		return result;
    }

	if (hr == DXGI_ERROR_WAIT_TIMEOUT && !m_stagingTextureThread) {
		result.result = RESULT_TIMEOUT;
		m_DesktopDup->ReleaseFrame();
		return result;
    }

	if (hr != DXGI_ERROR_WAIT_TIMEOUT) {
		if (FAILED(hr)) {
			result.result = RESULT_ERROR;
			result.error = "Failed to aquire next frame: " + std::system_category().message(hr);
			m_DesktopDup->ReleaseFrame();
			return result;
		}

		// If still holding old frame, destroy it
		if (m_LastImageThread) {
			m_LastImageThread->Release();
			m_LastImageThread = nullptr;
		}

		// QI for IDXGIResource
		hr = DesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&m_LastImageThread));
		DesktopResource->Release();
		DesktopResource = nullptr;

		if (FAILED(hr)) {
			result.result = RESULT_ERROR;
			result.error = "Failed to QI for ID3D11Texture2D from acquired IDXGIResource: " + std::system_category().message(hr);
			m_DesktopDup->ReleaseFrame();
			return result;
		}
	}

	D3D11_TEXTURE2D_DESC stagingTextureDesc;
	
	// only set up new shared surface if we have not done so before
	if (!m_stagingTextureThread) {
		D3D11_TEXTURE2D_DESC frameDesc;
		m_LastImageThread->GetDesc(&frameDesc);

		stagingTextureDesc.Width = frameDesc.Width;
		stagingTextureDesc.Height = frameDesc.Height;
		stagingTextureDesc.MipLevels = frameDesc.MipLevels;
		stagingTextureDesc.ArraySize = 1;
		stagingTextureDesc.Format = frameDesc.Format;
		stagingTextureDesc.SampleDesc = frameDesc.SampleDesc;
		stagingTextureDesc.Usage = D3D11_USAGE_STAGING;
		stagingTextureDesc.BindFlags = 0;
		stagingTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingTextureDesc.MiscFlags = 0;

		hr = m_Device->CreateTexture2D(&stagingTextureDesc, nullptr, &m_stagingTextureThread);
		if (FAILED(hr)) {
			result.result = RESULT_ERROR;
			result.error = "Failed to create shared surface: " + std::system_category().message(hr);
			m_DesktopDup->ReleaseFrame();
			return result;
		}
	} else {
		m_stagingTextureThread->GetDesc(&stagingTextureDesc);
	}
	
	// copy frame into shared texture
	m_Context->CopyResource(m_stagingTextureThread, m_LastImageThread);

	result = getFrameData(m_stagingTextureThread, stagingTextureDesc);

	m_DesktopDup->ReleaseFrame();

	return result;
}

FRAME_DATA DesktopDuplication::getFrameData(ID3D11Texture2D* texture, D3D11_TEXTURE2D_DESC& textureDesc) {
	FRAME_DATA result;

	D3D11_MAPPED_SUBRESOURCE resourceAccess;

	HRESULT hr = m_Context->Map(texture, 0, D3D11_MAP_READ, 0, &resourceAccess);

	if (FAILED(hr)) {
		result.result = RESULT_ERROR;
		result.error = "Failed to get pointer to the data contined in the shared texture: " + std::system_category().message(hr);
        return result;
    }

	void* imgData = malloc(textureDesc.Width * textureDesc.Height * 4);

	if (imgData == 0) {
		result.result = RESULT_ERROR;
		result.error = "Failed to allocate memory for the frame";
        return result;
	}

	memcpy(imgData, resourceAccess.pData, textureDesc.Width * textureDesc.Height * 4);

	char* data = reinterpret_cast<char*>(imgData);

	// change memory layout from BGRA to RGBA

	char temp;
	for(uint32_t i = 0; i < textureDesc.Width * textureDesc.Height * 4; i += 4) {
		temp = data[i + 2];
		data[i + 2] = data[i];
		data[i] = temp;
	}

	result.result = RESULT_SUCCESS;
	result.data = data;
	result.width = textureDesc.Width;
	result.height = textureDesc.Height;

	m_Context->Unmap(texture, 0);

	return result;
}

void DesktopDuplication::getFrameAsync(const Napi::CallbackInfo &info) {
	Napi::Env env = info.Env();

	Napi::Function callback = info[0].As<Napi::Function>();

	GetFrameAsyncWorker* worker = new GetFrameAsyncWorker(this, callback);
	worker->Queue();
}

Napi::Value DesktopDuplication::wrap_getFrame(const Napi::CallbackInfo &info) {
	Napi::Env env = info.Env();

	FRAME_DATA frame = this->getFrame(1000);

	Napi::Object result = Napi::Object::New(env);

	result.Set("error", env.Null());

	switch(frame.result) {
		case RESULT_TIMEOUT:
			result.Set("result", "timeout");
			return result;
		case RESULT_ACCESSLOST:
			result.Set("result", "accesslost");
			return result;
		case RESULT_ERROR:
			result.Set("result", "error");
			result.Set("error", Napi::String::New(env, frame.error));
		case RESULT_SUCCESS: {
			Napi::Buffer<char> buf = Napi::Buffer<char>::Copy(env, frame.data, frame.width * frame.height * 4);

			free(frame.data);

			result.Set("result", "success");
			result.Set("data", buf);
			result.Set("width", Napi::Number::New(env, (double)frame.width));
			result.Set("height", Napi::Number::New(env, (double)frame.height));
			return result;
		}
		default:
			return env.Null();
	}
}

Napi::Value DesktopDuplication::startAutoCapture(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    if (m_autoCaptureThreadStarted) {
        return Napi::Boolean::New(env, false);
    }

    int delay = info[0].As<Napi::Number>().Int32Value();
	bool allow_skips = info[1].As<Napi::Boolean>().Value();
    Napi::Function callback = info[2].As<Napi::Function>();

    m_autoCaptureThreadCallback = Napi::ThreadSafeFunction::New(env, callback, "AutoCaptureThreadCallback", (allow_skips) ? 1 : 0, 1);

    m_autoCaptureThreadSignal = std::promise<void>();

    m_autoCaptureThread = std::thread(&DesktopDuplication::autoCaptureFn, this, delay);

    m_autoCaptureThreadStarted = true;

    return Napi::Boolean::New(env, true);
}

bool DesktopDuplication::stopAutoCapture() {
    if (!m_autoCaptureThreadStarted) {
        return false;
    }

    m_autoCaptureThreadSignal.set_value();

    m_autoCaptureThread.join(); // wait for thread to finish

	m_autoCaptureThreadCallback.Release();

    m_autoCaptureThreadStarted = false;

    return true;
}

Napi::Value DesktopDuplication::wrap_stopAutoCapture(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    bool result = stopAutoCapture();

    return Napi::Boolean::New(env, result);
}

DesktopDuplication::~DesktopDuplication() {
	cleanUp();

	if (m_autoCaptureThreadStarted) {
		m_autoCaptureThreadSignal.set_value();

		m_autoCaptureThread.join();
	}
}

void DesktopDuplication::cleanUp() {
    if (m_DesktopDup) {
        m_DesktopDup->Release();
        m_DesktopDup = nullptr;
    }

    if (m_LastImage) {
        m_LastImage->Release();
        m_LastImage = nullptr;
    }

    if (m_Device) {
        m_Device->Release();
        m_Device = nullptr;
    }

	if (m_Context) {
		m_Context->Release();
		m_Context = nullptr;
	}

	if (m_LastImageThread) {
		m_LastImageThread->Release();
		m_LastImageThread = nullptr;
	}

	if (m_stagingTextureThread) {
		m_stagingTextureThread->Release();
		m_stagingTextureThread = nullptr;
	}
}

void DesktopDuplication::autoCaptureFnJsCallback(Napi::Env env, Napi::Function fn, FRAME_DATA* frame) {
	Napi::Object result = Napi::Object::New(env);

	result.Set("error", env.Null());

	switch(frame->result) {
		case RESULT_ACCESSLOST:
			result.Set("result", "accesslost");
		case RESULT_SUCCESS: {
			Napi::Buffer<char> buf = Napi::Buffer<char>::Copy(env, frame->data, frame->width * frame->height * 4);

			free(frame->data);

			result.Set("result", "success");
			result.Set("data", buf);
			result.Set("width", Napi::Number::New(env, (double)frame->width));
			result.Set("height", Napi::Number::New(env, (double)frame->height));
		}
	}

	fn.Call({ result });

	free(frame);
}

void DesktopDuplication::autoCaptureFn(int delay) {
    std::future<void> signal = m_autoCaptureThreadSignal.get_future();

    while (signal.wait_for(std::chrono::milliseconds(1)) == std::future_status::timeout) {
        auto start = std::chrono::high_resolution_clock::now();

		FRAME_DATA frame = getFrameThread(delay);

        if (frame.result != RESULT_SUCCESS) {
			if (frame.result == RESULT_ACCESSLOST) {
				// try to reinitialize automatically
				std::string error = initialize();
				if (error != "") {
					// can't reinitialize, end thread execution and notify node
					void* fd_clone_buffer = malloc(sizeof(FRAME_DATA));

					if (fd_clone_buffer != 0) { 
						FRAME_DATA* fd_clone = reinterpret_cast<FRAME_DATA*>(fd_clone_buffer);
						memcpy(fd_clone, &frame, sizeof(FRAME_DATA));

						napi_status status = m_autoCaptureThreadCallback.NonBlockingCall( fd_clone, autoCaptureFnJsCallback );

						if (status != napi_ok) {
							// free data manually if we can't transfer the responsibility to the GC
							free(fd_clone);
						}
					} // else: can't allocate anything, so we can't even notify node

					return;
				}
			}

            // ignore error case
            auto finish = std::chrono::high_resolution_clock::now();

            auto exTime = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);

            if (exTime.count() < delay - 1) {
                auto waitTime = std::chrono::milliseconds{ delay - 1 } - exTime;
                std::this_thread::sleep_for(waitTime); // wait the rest of the delay until the next screen capture
            }

            continue;
        }

		void* fd_clone_buffer = malloc(sizeof(FRAME_DATA));

		if (fd_clone_buffer != 0) { 
			FRAME_DATA* fd_clone = reinterpret_cast<FRAME_DATA*>(fd_clone_buffer);
			memcpy(fd_clone, &frame, sizeof(FRAME_DATA));

			napi_status status = m_autoCaptureThreadCallback.NonBlockingCall( fd_clone, autoCaptureFnJsCallback );

			if (status != napi_ok) {
				// free data manually if we can't transfer the responsibility to the GC
				free(frame.data);
				free(fd_clone);
			}
		}

        auto finish = std::chrono::high_resolution_clock::now();

        // check if have to finish before waiting for a potentially long time
        if (signal.wait_for(std::chrono::milliseconds(1)) != std::future_status::timeout) {
            break;
        }

        auto exTime = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);

        if (exTime.count() < delay - 1) {
            auto waitTime = std::chrono::milliseconds{ delay - 2 } - exTime; // subtract 2ms to account for the signal waiting
            std::this_thread::sleep_for(waitTime); // wait the rest of the delay until the next screen capture
		}
    }
}

Napi::FunctionReference DesktopDuplication::constructor;

Napi::Object DesktopDuplication::Init(Napi::Env env, Napi::Object exports) {
	Napi::Function func = DefineClass(env, "DesktopDuplication", {
		InstanceMethod("initialize", &DesktopDuplication::wrap_initialize),
		InstanceMethod("getFrame", &DesktopDuplication::wrap_getFrame),
		InstanceMethod("getFrameAsync", &DesktopDuplication::getFrameAsync),
		InstanceMethod("startAutoCapture", &DesktopDuplication::startAutoCapture),
		InstanceMethod("stopAutoCapture", &DesktopDuplication::wrap_stopAutoCapture),
    });

	constructor = Napi::Persistent(func);

	constructor.SuppressDestruct();

	exports.Set("DesktopDuplication", func);
	exports.Set("getMonitorCount", Napi::Function::New(env, DesktopDuplication::getMonitorCount));
	return exports;
}

Napi::Object Init (Napi::Env env, Napi::Object exports) {
    DesktopDuplication::Init(env, exports);
    return exports;
}


NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init)