#pragma once

#include <algorithm>
#include <cstdarg>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <memory>
#include <deque>
#include <map>

using namespace std::chrono_literals;


#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#include <wrl.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <shlobj_core.h>
#include <pathcch.h>

using Microsoft::WRL::ComPtr;


#include "openvr.h"
#include "Vectors.h"
#include "Matrices.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"