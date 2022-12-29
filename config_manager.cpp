
#include "pch.h"
#include "config_manager.h"
#include "logging.h"


ConfigManager::ConfigManager(std::wstring configFile)
	: m_configFile(configFile)
	, m_bConfigUpdated(false)
	, m_iniData()
{
	m_iniData.SetUnicode(true);
}

ConfigManager::~ConfigManager()
{
	DispatchUpdate();
}

void ConfigManager::ReadConfigFile()
{
	SI_Error result = m_iniData.LoadFile(m_configFile.c_str());
	if (result < 0)
	{
		Log("Failed to read config file, writing default values...\n");
		UpdateConfigFile();
	}
	else
	{
		ParseConfig_Main();
	}
	m_bConfigUpdated = false;
}

void ConfigManager::UpdateConfigFile()
{
	UpdateConfig_Main();

	SI_Error result = m_iniData.SaveFile(m_configFile.c_str());
	if (result < 0)
	{
		ErrorLog("Failed to save config file, %i \n", errno);
	}

	m_bConfigUpdated = false;
}

void ConfigManager::ConfigUpdated()
{
	m_bConfigUpdated = true;
}

void ConfigManager::DispatchUpdate()
{
	if (m_bConfigUpdated)
	{
		UpdateConfigFile();
	}
}

void ConfigManager::ResetToDefaults()
{
	m_configMain = Config_Main();
	UpdateConfigFile();
}

void ConfigManager::ParseConfig_Main()
{
	m_configMain.EnablePassthoughOnLaunch = m_iniData.GetBoolValue("Main", "EnablePassthoughOnLaunch", m_configMain.EnablePassthoughOnLaunch);
	m_configMain.ShowTestImage = m_iniData.GetBoolValue("Main", "ShowTestImage", m_configMain.ShowTestImage);
	m_configMain.PassthroughOpacity = (float)m_iniData.GetDoubleValue("Main", "PassthroughOpacity", m_configMain.PassthroughOpacity);
	m_configMain.ProjectionDistanceFar = (float)m_iniData.GetDoubleValue("Main", "ProjectionDistanceFar", m_configMain.ProjectionDistanceFar);
	m_configMain.ProjectionDistanceNear = (float)m_iniData.GetDoubleValue("Main", "ProjectionDistanceNear", m_configMain.ProjectionDistanceNear);

	m_configMain.Brightness = (float)m_iniData.GetDoubleValue("Main", "Brightness", m_configMain.Brightness);
	m_configMain.Contrast = (float)m_iniData.GetDoubleValue("Main", "Contrast", m_configMain.Contrast);
	m_configMain.Saturation = (float)m_iniData.GetDoubleValue("Main", "Saturation", m_configMain.Saturation);

	m_configMain.PassthroughMode = (EPassthroughBlendMode)m_iniData.GetLongValue("Core", "PassthroughMode", m_configMain.PassthroughMode);

	m_configMain.MaskedFractionChroma = (float)m_iniData.GetDoubleValue("Core", "MaskedFractionChroma", m_configMain.MaskedFractionChroma);
	m_configMain.MaskedFractionLuma = (float)m_iniData.GetDoubleValue("Core", "MaskedFractionLuma", m_configMain.MaskedFractionLuma);
	m_configMain.MaskedSmoothing = (float)m_iniData.GetDoubleValue("Core", "MaskedSmoothing", m_configMain.MaskedSmoothing);

	m_configMain.MaskedKeyColor[0] = (float)m_iniData.GetDoubleValue("Core", "MaskedKeyColorR", m_configMain.MaskedKeyColor[0]);
	m_configMain.MaskedKeyColor[1] = (float)m_iniData.GetDoubleValue("Core", "MaskedKeyColorG", m_configMain.MaskedKeyColor[1]);
	m_configMain.MaskedKeyColor[2] = (float)m_iniData.GetDoubleValue("Core", "MaskedKeyColorB", m_configMain.MaskedKeyColor[2]);

	m_configMain.MaskedUseCameraImage = m_iniData.GetBoolValue("Core", "MaskedUseCameraImage", m_configMain.MaskedUseCameraImage);
}

void ConfigManager::UpdateConfig_Main()
{
	m_iniData.SetBoolValue("Main", "EnablePassthoughOnLaunch", m_configMain.EnablePassthoughOnLaunch);
	m_iniData.SetBoolValue("Main", "ShowTestImage", m_configMain.ShowTestImage);
	m_iniData.SetDoubleValue("Main", "PassthroughOpacity", m_configMain.PassthroughOpacity);
	m_iniData.SetDoubleValue("Main", "ProjectionDistanceFar", m_configMain.ProjectionDistanceFar);
	m_iniData.SetDoubleValue("Main", "ProjectionDistanceNear", m_configMain.ProjectionDistanceNear);

	m_iniData.SetDoubleValue("Main", "Brightness", m_configMain.Brightness);
	m_iniData.SetDoubleValue("Main", "Contrast", m_configMain.Contrast);
	m_iniData.SetDoubleValue("Main", "Saturation", m_configMain.Saturation);

	m_iniData.SetLongValue("Core", "PassthroughMode", (int)m_configMain.PassthroughMode);
	m_iniData.SetDoubleValue("Core", "MaskedFractionChroma", m_configMain.MaskedFractionChroma);
	m_iniData.SetDoubleValue("Core", "MaskedFractionLuma", m_configMain.MaskedFractionLuma);
	m_iniData.SetDoubleValue("Core", "MaskedSmoothing", m_configMain.MaskedSmoothing);

	m_iniData.SetDoubleValue("Core", "MaskedKeyColorR", m_configMain.MaskedKeyColor[0]);
	m_iniData.SetDoubleValue("Core", "MaskedKeyColorG", m_configMain.MaskedKeyColor[1]);
	m_iniData.SetDoubleValue("Core", "MaskedKeyColorB", m_configMain.MaskedKeyColor[2]);

	m_iniData.SetBoolValue("Core", "MaskedUseCameraImage", m_configMain.MaskedUseCameraImage);
}