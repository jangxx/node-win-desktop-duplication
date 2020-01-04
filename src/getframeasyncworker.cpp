#include "getframeasyncworker.h"

GetFrameAsyncWorker::GetFrameAsyncWorker(DesktopDuplication* target, Napi::Function callback) : Napi::AsyncWorker(callback), m_DeskDup(target) {

}

void GetFrameAsyncWorker::Execute() {
	m_Frame = m_DeskDup->getFrame(1000);
}

std::vector<napi_value> GetFrameAsyncWorker::GetResult(Napi::Env env) {
	Napi::Object result = Napi::Object::New(env);

	result.Set("error", env.Null());

	switch(m_Frame.result) {
		case RESULT_TIMEOUT:
			result.Set("result", "timeout");
			return { result };
		case RESULT_ACCESSLOST:
			result.Set("result", "accesslost");
			return { result };
		case RESULT_ERROR:
			result.Set("result", "error");
			result.Set("error", Napi::String::New(env, m_Frame.error));
			return { result };
		case RESULT_SUCCESS: {
			Napi::Buffer<char> buf = Napi::Buffer<char>::Copy(env, m_Frame.data, m_Frame.width * m_Frame.height * 4);

			free(m_Frame.data);

			result.Set("result", "success");
			result.Set("data", buf);
			result.Set("width", Napi::Number::New(env, (double)m_Frame.width));
			result.Set("height", Napi::Number::New(env, (double)m_Frame.height));
			return { result };
		}
		default:
			return { env.Null() };
	}
}