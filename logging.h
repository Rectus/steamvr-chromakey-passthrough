
#pragma once

#include "pch.h"

void InitLogging(std::wstring fileName);

void ErrorLog(const char* format, ...);
void Log(const char* format, ...);

