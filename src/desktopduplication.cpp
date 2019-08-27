#include "desktopduplication.h"

DesktopDuplication::DesktopDuplication(const Napi::CallbackInfo &info) : Napi::ObjectWrap<DesktopDuplication>(info), m_Device(nullptr), m_Context(nullptr), m_DesktopDup(nullptr), m_LastImage(nullptr), m_SharedImage(nullptr) {
	UINT outputNum = info[0].As<Napi::Number>().Uint32Value();
	m_OutputNumber = outputNum;

	RtlZeroMemory(&m_OutputDesc, sizeof(m_OutputDesc));
}

void DesktopDuplication::initialize(const Napi::CallbackInfo &info) {
	Napi::Env env = info.Env();

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

	// create shared surface to copy aquired image to

	D3D11_TEXTURE2D_DESC DeskTexD;
	RtlZeroMemory(&DeskTexD, sizeof(D3D11_TEXTURE2D_DESC));

    DeskTexD.Width = m_OutputDesc.DesktopCoordinates.right - m_OutputDesc.DesktopCoordinates.left;
    DeskTexD.Height = m_OutputDesc.DesktopCoordinates.bottom - m_OutputDesc.DesktopCoordinates.top;
    DeskTexD.MipLevels = 1;
    DeskTexD.ArraySize = 1;
    DeskTexD.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    DeskTexD.SampleDesc.Count = 1;
    DeskTexD.Usage = D3D11_USAGE_DEFAULT;
    DeskTexD.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    DeskTexD.CPUAccessFlags = 0;
    DeskTexD.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

	hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &m_SharedImage);
    if (FAILED(hr)) {
		Napi::Error::New(env, "Failed to create shared surface: " + std::system_category().message(hr)).ThrowAsJavaScriptException();
		cleanUp();
		return;
	}

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

Napi::Value DesktopDuplication::getFrame(const Napi::CallbackInfo &info) {
	Napi::Env env = info.Env();

	Napi::Object result = Napi::Object::New(env);
	result.Set("error", env.Null());

	IDXGIResource* DesktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO FrameInfo;

    // Get new frame
    HRESULT hr = m_DesktopDup->AcquireNextFrame(500, &FrameInfo, &DesktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
		result.Set("result", Napi::String::New(env, "timeout"));
		return result;
    }

    if (FAILED(hr)) {
		result.Set("result", "error");
		result.Set("error", Napi::String::New(env, "Failed to aquire next frame: " + std::system_category().message(hr)));
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
		result.Set("result", "error");
		result.Set("error", Napi::String::New(env, "Failed to QI for ID3D11Texture2D from acquired IDXGIResource: " + std::system_category().message(hr)));
        return result;
    }

	// copy frame into shared texture
	m_Context->CopyResource(m_SharedImage, m_LastImage);

	D3D11_MAPPED_SUBRESOURCE resourceAccess;
	RtlZeroMemory(&resourceAccess, sizeof(D3D11_MAPPED_SUBRESOURCE));

	hr = m_Context->Map(m_SharedImage, 0, D3D11_MAP_READ, 0, &resourceAccess);

	if (FAILED(hr)) {
		result.Set("result", "error");
		result.Set("error", Napi::String::New(env, "Failed to get pointer to the data contined in the shared texture: " + std::system_category().message(hr)));
        return result;
    }

	Napi::Buffer<char> buf = Napi::Buffer<char>::New(env, reinterpret_cast<char*>(resourceAccess.pData), resourceAccess.RowPitch);

	long width = m_OutputDesc.DesktopCoordinates.right - m_OutputDesc.DesktopCoordinates.left;
	long height = m_OutputDesc.DesktopCoordinates.bottom - m_OutputDesc.DesktopCoordinates.top;

	result.Set("result", "success");
	result.Set("data", buf);
	result.Set("width", Napi::Number::New(env, (double)width));
	result.Set("height", Napi::Number::New(env, (double)height));

	return result;
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

	if (m_SharedImage) {
		m_SharedImage->Release();
		m_SharedImage = nullptr;
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
		InstanceMethod("getFrame", &DesktopDuplication::getFrame)
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