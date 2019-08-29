#pragma once

#include <windows.h>

enum RESULT_TYPE {
	RESULT_SUCCESS,
	RESULT_ERROR,
	RESULT_TIMEOUT,
	RESULT_ACCESSLOST
};

typedef struct {
	RESULT_TYPE result;
	std::string error;
	char* data;
	UINT width;
	UINT height;
} FRAME_DATA;