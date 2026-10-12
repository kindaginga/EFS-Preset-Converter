#include "Plugin.h"

#include "PresetHook.h"
#ifdef EFS_AUTOTEST
#	include "AutoTest.h"
#endif

#include "REL/RuntimeDatabase.h"

#include <spdlog/sinks/ostream_sink.h>

namespace
{
	[[nodiscard]] constexpr std::uint32_t PackVersion(std::uint32_t a_major, std::uint32_t a_minor, std::uint32_t a_build) noexcept
	{
		return ((a_major & 0xFF) << 24) | ((a_minor & 0xFF) << 16) | ((a_build & 0xFFF) << 4);
	}

	[[nodiscard]] constexpr F4SE::PluginVersionData MakePluginVersionData() noexcept
	{
		F4SE::PluginVersionData data{};
		data.pluginVersion = PackVersion(
			static_cast<std::uint32_t>(Version::MAJOR),
			static_cast<std::uint32_t>(Version::MINOR),
			static_cast<std::uint32_t>(Version::PATCH));
		for (std::size_t i = 0; i < Version::PROJECT.size() && i < std::size(data.name) - 1; ++i) {
			data.name[i] = Version::PROJECT[i];
		}
		data.addressIndependence = F4SE::PluginVersionData::kAddressIndependence_Signatures;
		data.structureIndependence =
			F4SE::PluginVersionData::kStructureIndependence_1_10_980Layout |
			F4SE::PluginVersionData::kStructureIndependence_1_11_137Layout;
		return data;
	}

	std::ofstream s_logFile;

	// Opens the log through a wide path, so a Documents folder name outside the ANSI code page still works.
	[[nodiscard]] bool InitializeLogger()
	{
		auto path = logger::log_directory();
		if (!path) return false;
		*path /= std::string(Version::PROJECT) + ".log";
		s_logFile.open(*path, std::ios::out | std::ios::trunc);
		if (!s_logFile) return false;
		auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(s_logFile, true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v"s);
		return true;
	}

	// Loads the Runtime Database file CommonLibF4RD would pick (REL/Relocation.cpp IDDatabase::load) with the library's own loader,
	// so a missing, damaged or newer-format file turns the plugin off instead of letting the library close the game.
	[[nodiscard]] bool RuntimeDatabaseUsable(const REL::Version& a_version)
	{
		const std::array<std::filesystem::path, 2> candidates{
			std::filesystem::path("Data/F4SE/Plugins/f4rd-runtime.bin"),
			std::filesystem::path(std::format("Data/F4SE/Plugins/f4rd-runtime-{}.bin", a_version.string()))
		};
		for (const auto& candidate : candidates) {
			std::error_code error;
			if (!std::filesystem::exists(candidate, error) || error) continue;
			try {
				(void)REL::RuntimeDatabase::load(candidate);
				logger::info("runtime database: {}", candidate.generic_string());
				return true;
			} catch (const std::exception& e) {
				logger::critical("the Runtime Database file {} cannot be used ({})", candidate.generic_string(), e.what());
				return false;
			}
		}
		logger::critical("Runtime Database (Nexus mod 108394) is not installed");
		return false;
	}

	// Every address the plugin uses outside its test build, resolved without the library's fail-and-close path.
	[[nodiscard]] bool AddressesResolve()
	{
		static const std::array<REL::ID, 23> ids{
			REL::ID(507142, 2268729),   // BSStringPool GetEntry<char> (BSFixedString)
			REL::ID(1204430, 2268720),  // BSStringPool Entry release
			REL::ID(548587, 4796314),   // UI singleton
			REL::ID(578487, 2707105),   // UI menu map lock
			REL::ID(1436639, 2284772),  // UI helpers used by GetMenu and event registration
			REL::ID(1519575, 2284766),
			REL::ID(175796, 2284768),
			REL::ID(190238, 4483350),
			REL::ID(244786, 2286228),   // Scaleform ObjectInterface
			REL::ID(856221, 2286229),
			REL::ID(788691, 2286078),
			REL::ID(1517430, 4494126),
			REL::ID(1360149, 2286589),
			REL::ID(655847, 2286101),
			REL::ID(1330475, 2286424),
			REL::ID(404814, 2287230),   // Scaleform Movie release
			REL::ID(939898, 2707353),   // Scaleform global heap
			REL::ID(1425657, 2192245),  // BSReadWriteLock
			REL::ID(1573164, 2267897),
			REL::ID(336186, 2267898),
			REL::ID(1372435, 2267901),
			REL::ID(1279453, 2267902),
			REL::ID(1424022, 4796078),  // global event singleton
		};
		auto& database = REL::IDDatabase::get();
		bool all = true;
		for (const auto& id : ids) {
			const auto result = database.resolve(id);
			if (!result) {
				logger::critical("address ID {} did not resolve ({})", result.id, REL::id_resolve_status_text(result.status));
				all = false;
			}
		}
		return all;
	}

	void OnMessage(F4SE::MessagingInterface::Message* a_message)
	{
		if (!a_message) return;
		switch (a_message->type) {
		case F4SE::MessagingInterface::kGameDataReady:
			EFS::PresetHook::Install();
#ifdef EFS_AUTOTEST
			EFS::AutoTest::OnGameDataReady();
#endif
			break;
		default:
			break;
		}
	}
}

extern "C" DLLEXPORT constinit F4SE::PluginVersionData F4SEPlugin_Version = MakePluginVersionData();

// Returns true even when the plugin turns itself off, so F4SE does not show its "Exit game?" prompt; the log says why.
bool Plugin::Initialize(const F4SE::LoadInterface* a_f4se)
{
	try {
		if (!InitializeLogger()) return true;
		if (a_f4se->IsEditor()) {
			logger::info("loaded in the editor; nothing to do");
			return false;
		}
		if (!RuntimeDatabaseUsable(a_f4se->RuntimeVersion())) {
			logger::critical("automatic preset conversion is off; the plugin stays inactive");
			return true;
		}
		F4SE::Init(a_f4se);
		if (!AddressesResolve()) {
			logger::critical("automatic preset conversion is off; the plugin stays inactive");
			return true;
		}
		logger::info("{} v{} loaded on game version {}", Version::PROJECT, Version::NAME, REL::Module::get().version().string());
#ifdef EFS_AUTOTEST
		EFS::AutoTest::Configure();
#endif
		F4SE::GetMessagingInterface()->RegisterListener(OnMessage);
		return true;
	} catch (const std::exception& e) {
		try {
			logger::critical("startup failed ({}); the plugin stays inactive", e.what());
		} catch (...) {
		}
		return true;
	} catch (...) {
		return true;
	}
}
