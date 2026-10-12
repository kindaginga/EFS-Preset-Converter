#include "AutoTest.h"

#include "PresetHook.h"

#include "RE/Bethesda/PlayerCharacter.h"
#include "RE/Bethesda/TESBoundAnimObjects.h"
#include "RE/Bethesda/TESCamera.h"
#include "RE/Bethesda/UIMessageQueue.h"

namespace EFS::AutoTest
{
	namespace
	{
		constexpr auto kConfigPath = "Data/F4SE/Plugins/EFSPresetConverter_AutoTest.ini"sv;

		bool s_enabled{ false };
		std::string s_start{ "coc QASmoke" };
		std::string s_outputDir;
		std::vector<std::string> s_male;
		std::vector<std::string> s_female;
		int s_rounds{ 1 };  // times the male phase runs; each round starts one preset later in the list
		int s_firstDelayMs{ 3000 };  // wait between LooksMenu opening and the first preset load
		bool s_saveEarly{ false };   // also save the face 0.1 s after each load, not only after 1.5 s
		int s_holdMs{ 1500 };        // wait between a preset load and its SavePreset
		std::atomic<bool> s_started{ false };

		[[nodiscard]] std::vector<std::string> Split(const std::string& a_text)
		{
			std::vector<std::string> out;
			std::stringstream stream(a_text);
			std::string item;
			while (std::getline(stream, item, '|')) {
				if (!item.empty()) out.push_back(item);
			}
			return out;
		}

		// Runs a task on the given F4SE thread and waits up to a_seconds for it to finish.
		bool RunAndWait(bool a_ui, std::function<void()> a_task, int a_seconds = 60)
		{
			auto done = std::make_shared<std::atomic<bool>>(false);
			auto task = [task = std::move(a_task), done] {
				task();
				done->store(true);
			};
			if (a_ui) F4SE::GetTaskInterface()->AddUITask(std::move(task));
			else F4SE::GetTaskInterface()->AddTask(std::move(task));
			for (int i = 0; i < a_seconds * 10 && !done->load(); ++i) std::this_thread::sleep_for(100ms);
			return done->load();
		}

		void Console(const std::string& a_command)
		{
			const bool ran = RunAndWait(false, [a_command] { RE::Console::ExecuteCommand(a_command.c_str()); }, 180);
			logger::info("autotest: console '{}' ran {}", a_command, ran);
		}

		[[nodiscard]] bool MenuOpen(std::string_view a_name)
		{
			auto* ui = RE::UI::GetSingleton();
			return ui && ui->GetMenuOpen(RE::BSFixedString(a_name));
		}

		// Polls a condition on the game thread every half second for up to a_seconds.
		bool WaitFor(const char* a_what, std::function<bool()> a_condition, int a_seconds)
		{
			for (int i = 0; i < a_seconds * 2; ++i) {
				auto result = std::make_shared<std::atomic<int>>(-1);
				RunAndWait(false, [a_condition, result] { result->store(a_condition() ? 1 : 0); }, 10);
				if (result->load() == 1) {
					logger::info("autotest: {} after {:.1f} s", a_what, i / 2.0);
					return true;
				}
				std::this_thread::sleep_for(500ms);
			}
			logger::error("autotest: FAILED waiting for {} ({} s)", a_what, a_seconds);
			return false;
		}

		[[nodiscard]] std::optional<std::uint32_t> PlayerSex()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* npc = player ? player->GetNPC() : nullptr;
			if (!npc) return std::nullopt;
			return npc->GetSex();
		}

		// Loads a preset through the F4EE object LooksMenu uses, then saves the face it produced.
		void RunCase(const std::string& a_label, const std::string& a_preset, const std::string& a_out)
		{
			RunAndWait(true, [a_label, a_preset] {
				auto* ui = RE::UI::GetSingleton();
				const auto menu = ui ? ui->GetMenu("LooksMenu") : nullptr;
				if (!menu || !menu->uiMovie) {
					logger::error("autotest: case {}: LooksMenu is not open", a_label);
					return;
				}
				RE::Scaleform::GFx::Value f4ee;
				if (!menu->uiMovie->GetVariable(&f4ee, "root.f4se.plugins.F4EE") || !f4ee.IsObject()) {
					logger::error("autotest: case {}: no F4EE object", a_label);
					return;
				}
				// A "raw:" prefix calls LooksMenu's own LoadPreset directly, bypassing the converter, as a control.
				const bool raw = a_preset.starts_with("raw:");
				const std::string path = raw ? a_preset.substr(4) : a_preset;
				RE::Scaleform::GFx::Value arg(path.c_str());
				RE::Scaleform::GFx::Value result;
				const bool called = f4ee.Invoke(raw ? PresetHook::kOriginalMember : "LoadPreset", &result, &arg, 1);
				logger::info("autotest: case {}: wrapped {}, {}LoadPreset('{}') called {} returned {}", a_label, f4ee.HasMember(PresetHook::kOriginalMember),
					raw ? "original " : "", path, called, result.IsInt() ? result.GetInt() : (result.IsUInt() ? static_cast<std::int64_t>(result.GetUInt()) : -999));
			});
			if (s_saveEarly) {
				std::this_thread::sleep_for(100ms);
				RunAndWait(true, [a_label, a_out] {
					auto* ui = RE::UI::GetSingleton();
					const auto menu = ui ? ui->GetMenu("LooksMenu") : nullptr;
					RE::Scaleform::GFx::Value f4ee;
					if (!menu || !menu->uiMovie || !menu->uiMovie->GetVariable(&f4ee, "root.f4se.plugins.F4EE") || !f4ee.IsObject()) return;
					const auto early = a_out.substr(0, a_out.size() - 9) + ".early.json";
					RE::Scaleform::GFx::Value arg(early.c_str());
					RE::Scaleform::GFx::Value result;
					logger::info("autotest: case {}: early SavePreset('{}') called {}", a_label, early, f4ee.Invoke("SavePreset", &result, &arg, 1));
				});
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(s_holdMs));
			RunAndWait(true, [a_label, a_out] {
				auto* ui = RE::UI::GetSingleton();
				const auto menu = ui ? ui->GetMenu("LooksMenu") : nullptr;
				RE::Scaleform::GFx::Value f4ee;
				if (!menu || !menu->uiMovie || !menu->uiMovie->GetVariable(&f4ee, "root.f4se.plugins.F4EE") || !f4ee.IsObject()) return;
				RE::Scaleform::GFx::Value arg(a_out.c_str());
				RE::Scaleform::GFx::Value result;
				const bool called = f4ee.Invoke("SavePreset", &result, &arg, 1);
				logger::info("autotest: case {}: SavePreset('{}') called {}", a_label, a_out, called);
			});
			std::this_thread::sleep_for(500ms);
		}

		// Switches the player camera to third person so LooksMenu frames the face.
		void ThirdPerson()
		{
			RunAndWait(false, [] {
				auto* camera = RE::PlayerCamera::GetSingleton();
				const auto state = camera ? camera->GetState(RE::CameraStates::k3rdPerson) : nullptr;
				if (state) camera->SetState(state.get());
				logger::info("autotest: third person camera {}", static_cast<bool>(state));
			});
			std::this_thread::sleep_for(2s);
			Console("tfc");
			auto degrees = std::make_shared<std::atomic<float>>(0.0f);
			RunAndWait(false, [degrees] {
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (player) degrees->store(player->data.angle.z * 57.29578f);
			});
			Console(std::format("player.setangle z {:.1f}", std::fmod(degrees->load() + 180.0f, 360.0f)));
			std::this_thread::sleep_for(2s);
		}

		bool RunPhase(const char* a_sex, const std::vector<std::string>& a_presets)
		{
			ThirdPerson();
			Console("slm 14");
			if (!WaitFor("LooksMenu open", [] { return MenuOpen("LooksMenu"); }, 30)) return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(s_firstDelayMs));
			int index = 0;
			for (const auto& preset : a_presets) {
				const auto label = std::format("{}{}", a_sex, ++index);
				const auto stem = std::filesystem::path(preset.starts_with("raw:") ? preset.substr(4) : preset).stem().string();
				const auto out = std::format("{}\\{}_{}{}.out.json", s_outputDir, label, preset.starts_with("raw:") ? "raw_" : "", stem);
				RunCase(label, preset, out);
			}
			RunAndWait(false, [] { RE::UIMessageQueue::GetSingleton()->AddMessage("LooksMenu", RE::UI_MESSAGE_TYPE::kHide); });
			return WaitFor("LooksMenu closed", [] { return !MenuOpen("LooksMenu"); }, 30);
		}

		void Run()
		{
			logger::info("autotest: started");
			if (!WaitFor("main menu", [] { return MenuOpen("MainMenu"); }, 300)) return Console("qqq");
			std::this_thread::sleep_for(5s);
			const auto starts = Split(s_start);
			Console(starts.empty() ? s_start : starts.front());
			if (!WaitFor("the player in a loaded cell", [] {
					auto* player = RE::PlayerCharacter::GetSingleton();
					return player && player->parentCell && !MenuOpen("LoadingMenu") && !MenuOpen("MainMenu");
				}, 300)) return Console("qqq");
			std::this_thread::sleep_for(3s);
			for (std::size_t i = 1; i < starts.size(); ++i) Console(starts[i]);
			std::this_thread::sleep_for(8s);

			auto sex = std::make_shared<std::optional<std::uint32_t>>();
			if (!RunAndWait(false, [sex] { *sex = PlayerSex(); })) sex = std::make_shared<std::optional<std::uint32_t>>();
			logger::info("autotest: player sex {}", *sex ? std::to_string(**sex) : "unknown"s);
			if (*sex && **sex != 0) {
				Console("player.sexchange");
				std::this_thread::sleep_for(3s);
			}
			for (int round = 0; round < s_rounds && !s_male.empty(); ++round) {
				auto order = s_male;
				std::rotate(order.begin(), order.begin() + (round % order.size()), order.end());
				if (!RunPhase(std::format("round{}male", round + 1).c_str(), order)) return Console("qqq");
			}

			if (!s_female.empty()) {
				Console("player.sexchange");
				std::this_thread::sleep_for(3s);
				auto after = std::make_shared<std::optional<std::uint32_t>>();
				if (!RunAndWait(false, [after] { *after = PlayerSex(); })) after = std::make_shared<std::optional<std::uint32_t>>();
				logger::info("autotest: player sex {}", *after ? std::to_string(**after) : "unknown"s);
				if (!RunPhase("female", s_female)) return Console("qqq");
			}
			logger::info("autotest: finished");
			Console("qqq");
		}
	}

	void Configure()
	{
		std::ifstream in{ std::string(kConfigPath) };
		if (!in) return;
		std::string line;
		while (std::getline(in, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			const auto eq = line.find('=');
			if (eq == std::string::npos) continue;
			const auto key = line.substr(0, eq);
			const auto value = line.substr(eq + 1);
			if (key == "Enabled") s_enabled = value == "1";
			else if (key == "Start") s_start = value;
			else if (key == "OutputDir") s_outputDir = value;
			else if (key == "Male") s_male = Split(value);
			else if (key == "Female") s_female = Split(value);
			else if (key == "Rounds") s_rounds = std::max(1, std::atoi(value.c_str()));
			else if (key == "FirstDelayMs") s_firstDelayMs = std::max(0, std::atoi(value.c_str()));
			else if (key == "SaveEarly") s_saveEarly = value == "1";
			else if (key == "HoldMs") s_holdMs = std::max(0, std::atoi(value.c_str()));
		}
		logger::info("autotest: enabled {}, start '{}', {} male and {} female presets, output '{}'", s_enabled, s_start, s_male.size(), s_female.size(), s_outputDir);
	}

	void OnGameDataReady()
	{
		if (!s_enabled || s_started.exchange(true)) return;
		std::thread(Run).detach();
	}

}
