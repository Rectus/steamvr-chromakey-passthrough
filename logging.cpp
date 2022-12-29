
#include "pch.h"
#include "logging.h"

#define LOG_MAX_ERROR_MESSAGES 100

std::ofstream stream;
int errorMessageCount = 0;

void InitLogging(std::wstring fileName)
{
	if (stream.is_open())
	{
		stream.close();
	}

	PWSTR appdataPath;
	SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &appdataPath);

	std::filesystem::path filePath = std::filesystem::path(appdataPath) / fileName;
	stream.open(filePath.wstring(), std::ios_base::ate);
}

void ErrorLog(const char* format, ...)
{
	if (errorMessageCount++ > LOG_MAX_ERROR_MESSAGES)
	{
		return;
	}

	va_list varArgs;
	va_start(varArgs, format);

	char buffer[1024];

	vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, varArgs);
	OutputDebugStringA(buffer);

	if (stream.is_open())
	{
		stream << buffer;
		stream.flush();
	}
	va_end(varArgs);
}


void Log(const char* format, ...)
{
	va_list varArgs;
	va_start(varArgs, format);

	char buffer[1024];

	vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, varArgs);
	OutputDebugStringA(buffer);

	if (stream.is_open())
	{
		stream << buffer;
		stream.flush();
	}
	va_end(varArgs);
}