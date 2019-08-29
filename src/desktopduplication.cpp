#include "desktopduplication.h"

DesktopDuplication::DesktopDuplication(const Napi::CallbackInfo &info) : Napi::ObjectWrap<DesktopDuplication>(info), m_Device(nullptr), m_Context(nullptr), m_DesktopDup(nullptr), m_LastImage(nullptr) {
	UINT outputNum = info[0].As<Napi::Number>().Uint32Value();
	m_OutputNumber = outputNum;

	RtlZeroMemory(&m_OutputDesc, sizeof(m_OutputDesc));
}

void DesktopDuplication::initialize(const Napi::CallbackInfo &info) {
	Napi::Env env = info.Env();

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
		Napi::Error::New(env, "Failed to create device: " + std::system_category().message(hr)).ThrowAsJavaScriptException();
		return;
	}

	 // Get DXGI device
	IDXGIDevice* DxgiDevice = nullptr;
	hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&DxgiDevice));
	if (FAILED(hr)) {
		Napi::Error::New(env, "Failed to query interface for DXGI Device: " + std::system_category().message(hr)).ThrowAsJavaScriptException();
		cleanUp();
		return;
	}

	// Get DXGI adapter
	IDXGIAdapter* DxgiAdapter = nullptr;
	hr = DxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&DxgiAdapter));
	DxgiDevice->Release();
	DxgiDevice = nullptr;
	if (FAILED(hr)) {
		Napi::Error::New(env, "Failed to get parent DXGI Adapter: " + std::system_category().message(hr)).ThrowAsJavaScriptException();
		cleanUp();
		return;
	}

	// Get output
	IDXGIOutput* DxgiOutput = nullptr;
	hr = DxgiAdapter->EnumOutputs(m_OutputNumber, &DxgiOutput);
	DxgiAdapter->Release();
	DxgiAdapter = nullptr;
	if (FAILED(hr)) {
		Napi::Error::New(env, "Failed to get specified output: " + std::system_category().message(hr)).ThrowAsJavaScriptException();
		cleanUp();
		return;
	}

	DxgiOutput->GetDesc(&m_OutputDesc);

	// QI for Output 1
	IDXGIOutput1* DxgiOutput1 = nullptr;
	hr = DxgiOutput->QueryInterface(__uuidof(DxgiOutput1), reinterpret_cast<void**>(&DxgiOutput1));
	DxgiOutput->Release();
	DxgiOutput = nullptr;
	if (FAILED(hr)) {
		Napi::Error::New(env, "Failed to query interface for DxgiOutput1: " + std::system_category().message(hr)).ThrowAsJavaScriptException();
		cleanUp();
		return;
	}

	// Create desktop duplication
	hr = DxgiOutput1->DuplicateOutput(m_Device, &m_DesktopDup);
	DxgiOutput1->Release();
	DxgiOutput1 = nullptr;
	if (FAILED(hr)) {
		if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
			Napi::Error::New(env, "There is already the maximum number of applications using the Desktop Duplication API running, please close one of those applications and then try again.").ThrowAsJavaScriptException();
			return;
		}

		Napi::Error::New(env, "Failed to get duplicate output: " + std::system_category().message(hr)).ThrowAsJavaScriptException();
		cleanUp();
		return;
	}
}

FRAME_DATA DesktopDuplication::getFrame(UINT timeout) {
	FRAME_DATA result;	

	IDXGIResource* DesktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO FrameInfo;

    // Get new frame
    HRESULT hr = m_DesktopDup->AcquireNextFrame(timeout, &FrameInfo, &DesktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
		result.result = RESULT_TIMEOUT;
		return result;
    }

	if (hr == DXGI_ERROR_ACCESS_LOST) {
		result.result = RESULT_ACCESSLOST;
		return result;
    }

    if (FAILED(hr)) {
		result.result = RESULT_ERROR;
		result.error = "Failed to aquire next frame: " + std::system_category().message(hr);
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
        return result;
    }

	D3D11_TEXTURE2D_DESC frameDesc;
	m_LastImage->GetDesc(&frameDesc);

	// create shared surface to copy aquired image to
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

	hr = m_Device->CreateTexture2D(&stagingTextureDesc, nullptr, &stagingTexture);
    if (FAILED(hr)) {
		result.result = RESULT_ERROR;
		result.error = "Failed to create shared surface: " + std::system_category().message(hr);
		return result;
	}

	// copy frame into shared texture
	m_Context->CopyResource(stagingTexture, m_LastImage);

	D3D11_MAPPED_SUBRESOURCE resourceAccess;

	hr = m_Context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &resourceAccess);

	if (FAILED(hr)) {
		result.result = RESULT_ERROR;
		result.error = "Failed to get pointer to the data contined in the shared texture: " + std::system_category().message(hr);
        return result;
    }

	void* imgData = malloc(stagingTextureDesc.Width * stagingTextureDesc.Height * 4);
	memcpy(imgData, resourceAccess.pData, stagingTextureDesc.Width * stagingTextureDesc.Height * 4);

	char* data = reinterpret_cast<char*>(imgData);

	// change memory layout from BGRA to RGBA

	char temp;
	for(uint32_t i = 0; i < stagingTextureDesc.Width * stagingTextureDesc.Height * 4; i += 4) {
		temp = data[i + 2];
		data[i + 2] = data[i];
		data[i] = temp;
	}

	m_Context->Unmap(stagingTexture, 0);
	stagingTexture->Release();
	m_DesktopDup->ReleaseFrame();

	result.result = RESULT_SUCCESS;
	result.data = data;
	result.width = stagingTextureDesc.Width;
	result.height = stagingTextureDesc.Height;

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
			Napi::Buffer<char> buf = Napi::Buffer<char>::New(env, frame.data, frame.width * frame.height * 4, [](Napi::Env env, char* data) { free(data); } );

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

DesktopDuplication::~DesktopDuplication() {
	cleanUp();
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
}

Napi::FunctionReference DesktopDuplication::constructor;

Napi::Object DesktopDuplication::Init(Napi::Env env, Napi::Object exports) {
	Napi::Function func = DefineClass(env, "DesktopDuplication", {
		InstanceMethod("initialize", &DesktopDuplication::initialize),
		InstanceMethod("getFrame", &DesktopDuplication::wrap_getFrame),
		InstanceMethod("getFrameAsync", &DesktopDuplication::getFrameAsync)
    });

	constructor = Napi::Persistent(func);

	constructor.SuppressDestruct();

	exports.Set("DesktopDuplication", func);
	return exports;
}

Napi::Object Init (Napi::Env env, Napi::Object exports) {
    DesktopDuplication::Init(env, exports);
    return exports;
}


NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init)