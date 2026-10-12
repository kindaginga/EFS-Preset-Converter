#pragma once

#pragma warning(push)
#include "F4SE/F4SE.h"
// Only the game headers the plugin uses: RE/Fallout.h brings in headers whose global address objects resolve when the DLL loads
// (PowerArmor.h, Utilities.h, Workshop.h), before the Runtime Database check can turn the plugin off.
#include "RE/Bethesda/BSTEvent.h"
#include "RE/Bethesda/IMenu.h"
#include "RE/Bethesda/UI.h"
#include "RE/Scaleform/GFx/GFx_Player.h"

#include <spdlog/sinks/basic_file_sink.h>
#pragma warning(pop)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#define DLLEXPORT __declspec(dllexport)

namespace logger = F4SE::log;

using namespace std::literals;

#include "Version.h"
