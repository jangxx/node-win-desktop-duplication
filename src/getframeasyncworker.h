#pragma once

#include "napi.h"
#include "types.h"
#include "desktopduplication.h"

class DesktopDuplication;

class GetFrameAsyncWorker : public Napi::AsyncWorker {
	public:
		GetFrameAsyncWorker(DesktopDuplication* target, Napi::Function callback);

		void Execute();

		std::vector<napi_value> GetResult(Napi::Env env);
		
	private:
		FRAME_DATA m_Frame;
		DesktopDuplication* m_DeskDup;
};