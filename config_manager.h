
#pragma once

#include "SimpleIni.h"
#include "shared_structs.h"


struct Config_Main
{
	bool EnablePassthoughOnLaunch = true;
	bool ShowTestImage = false;
	float PassthroughOpacity = 1.0f;
	float ProjectionDistanceFar = 5.0f;
	float ProjectionDistanceNear = 1.0f;

	float Brightness = 0.0f;
	float Contrast = 1.0f;
	float Saturation = 1.0f;

	EPassthroughBlendMode PassthroughMode;

	float MaskedFractionChroma = 0.2f;
	float MaskedFractionLuma = 0.4f;
	float MaskedSmoothing = 0.01f;
	float MaskedKeyColor[3] = { 0 ,0 ,0 };
	bool MaskedUseCameraImage = false;
};




class ConfigManager
{
public:
	ConfigManager(std::wstring configFile);
	~ConfigManager();

	void ReadConfigFile();
	void ConfigUpdated();
	void DispatchUpdate();
	void ResetToDefaults();

	Config_Main& GetConfig_Main() { return m_configMain; }


private:
	void UpdateConfigFile();

	void ParseConfig_Main();
	void UpdateConfig_Main();

	std::wstring m_configFile;
	CSimpleIniA m_iniData;
	bool m_bConfigUpdated;

	Config_Main m_configMain;
};

