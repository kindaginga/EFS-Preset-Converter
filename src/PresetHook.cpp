#include "PresetHook.h"

#include "Convert.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace EFS::PresetHook
{
	namespace
	{
		constexpr auto kMenuName = "LooksMenu"sv;
		constexpr std::uint32_t kNotFound = 0xFFFFFFFF;

		using GValue = RE::Scaleform::GFx::Value;

		// The LooksMenu display object, which the game names root1.LooksMenu_mc.
		[[nodiscard]] bool GetLooksMenu(RE::Scaleform::GFx::Movie* a_movie, GValue& a_out)
		{
			if (a_movie->GetVariable(&a_out, "root1.LooksMenu_mc") && a_out.IsObject()) return true;
			GValue root;
			return a_movie->GetVariable(&root, "root") && root.IsObject() && root.GetMember("LooksMenu_mc", &a_out) && a_out.IsObject();
		}

		// Whether the edited character's sculpt list has this ID, through LooksMenu's public GetBoneRegionIndexByID.
		[[nodiscard]] std::optional<bool> HasSculpt(GValue& a_looksMenu, std::uint32_t a_id)
		{
			GValue arg(a_id);
			GValue result;
			if (!a_looksMenu.Invoke("GetBoneRegionIndexByID", &result, &arg, 1)) return std::nullopt;
			double index = 0;
			if (result.IsUInt()) index = result.GetUInt();
			else if (result.IsInt()) index = result.GetInt();
			else if (result.IsNumber()) index = result.GetNumber();
			else return std::nullopt;
			return index != static_cast<double>(kNotFound);
		}

		// The code page a narrow file name is read in by LooksMenu's CRT (fopen documentation: ANSI, or OEM after SetFileApisToOEM).
		[[nodiscard]] UINT FileCodePage()
		{
			return AreFileApisANSI() ? CP_ACP : CP_OEMCP;
		}

		[[nodiscard]] std::string Narrow(const std::wstring& a_wide, bool& a_lossless)
		{
			const auto codePage = FileCodePage();
			BOOL usedDefault = FALSE;
			const int size = WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, a_wide.c_str(), -1, nullptr, 0, nullptr, &usedDefault);
			if (size <= 0) {
				a_lossless = false;
				return {};
			}
			std::string out(static_cast<std::size_t>(size), '\0');
			WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, a_wide.c_str(), -1, out.data(), size, nullptr, &usedDefault);
			out.resize(static_cast<std::size_t>(size) - 1);
			a_lossless = usedDefault == FALSE;
			return out;
		}

		// The wide file name LooksMenu's CRT opens for this narrow name; empty when the name is invalid in that code page.
		[[nodiscard]] std::optional<std::wstring> Widen(const char* a_narrow)
		{
			const auto codePage = FileCodePage();
			const int size = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, a_narrow, -1, nullptr, 0);
			if (size <= 0) return std::nullopt;
			std::wstring out(static_cast<std::size_t>(size), L'\0');
			MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, a_narrow, -1, out.data(), size);
			out.resize(static_cast<std::size_t>(size) - 1);
			return out;
		}

		[[nodiscard]] std::string Utf8(const std::wstring& a_wide)
		{
			const int size = WideCharToMultiByte(CP_UTF8, 0, a_wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
			if (size <= 0) return {};
			std::string out(static_cast<std::size_t>(size), '\0');
			WideCharToMultiByte(CP_UTF8, 0, a_wide.c_str(), -1, out.data(), size, nullptr, nullptr);
			out.resize(static_cast<std::size_t>(size) - 1);
			return out;
		}

		void RemoveTemp(const std::wstring& a_path)
		{
			std::error_code ec;
			if (!std::filesystem::remove(a_path, ec) && ec) {
				logger::warn("temp file {} could not be deleted: {}", Utf8(a_path), ec.message());
			}
		}

		// A new temp file path that LooksMenu's narrow-string file stream can open; empty when there is none.
		[[nodiscard]] std::string MakeTempPath(std::wstring& a_wide)
		{
			static std::atomic<unsigned> counter{ 0 };
			wchar_t dir[MAX_PATH + 1]{};
			const auto length = GetTempPathW(MAX_PATH + 1, dir);
			if (length == 0 || length > MAX_PATH) return {};
			const auto name = std::format(L"EFSPresetConverter_{}_{}.json", GetCurrentProcessId(), ++counter);
			a_wide = std::wstring(dir) + name;
			bool lossless = false;
			auto narrow = Narrow(a_wide, lossless);
			if (lossless) return narrow;
			wchar_t shortDir[MAX_PATH + 1]{};
			const auto shortLength = GetShortPathNameW(dir, shortDir, MAX_PATH + 1);
			if (shortLength == 0 || shortLength > MAX_PATH) return {};
			a_wide = std::wstring(shortDir) + name;
			narrow = Narrow(a_wide, lossless);
			return lossless ? narrow : std::string{};
		}

		// Converts the preset at a_path into a temp file and returns that file's path, or empty to load the original.
		[[nodiscard]] std::string ConvertToTemp(RE::Scaleform::GFx::Movie* a_movie, const char* a_path, std::wstring& a_temp)
		{
			const auto widePath = Widen(a_path);
			if (!widePath) {
				logger::info("preset '{}': its name is not valid in the file code page, so LooksMenu loads it as it is", a_path);
				return {};
			}
			std::ifstream in(std::filesystem::path(*widePath), std::ios::binary);
			if (!in) {
				logger::info("preset '{}': could not be opened here, so LooksMenu loads it as it is", a_path);
				return {};
			}
			const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			in.close();

			GValue looksMenu;
			if (!GetLooksMenu(a_movie, looksMenu)) {
				logger::warn("preset '{}': LooksMenu's sculpt list is not reachable, so it loads unconverted", a_path);
				return {};
			}
			bool unanswered = false;
			const auto result = Convert::ConvertPreset(bytes, [&](std::uint32_t a_id) {
				const auto has = HasSculpt(looksMenu, a_id);
				if (!has) unanswered = true;
				return has.value_or(false);
			});
			if (unanswered) logger::warn("preset '{}': LooksMenu did not answer some sculpt lookups; those sculpts stay as they are", a_path);
			if (!result.error.empty()) {
				logger::info("preset '{}': {}, so it loads unconverted", a_path, result.error);
				return {};
			}
			if (result.renamed.empty()) {
				if (result.clashes) logger::info("preset '{}': {} old sculpt IDs left as they are because the preset also holds their new IDs", a_path, result.clashes);
				else logger::info("preset '{}': nothing to convert", a_path);
				return {};
			}

			const auto narrow = MakeTempPath(a_temp);
			if (narrow.empty()) {
				logger::error("preset '{}': no usable temp file path, so it loads unconverted", a_path);
				return {};
			}
			bool written = false;
			{
				std::ofstream out(std::filesystem::path(a_temp), std::ios::binary | std::ios::trunc);
				out.write(result.bytes.data(), static_cast<std::streamsize>(result.bytes.size()));
				out.close();
				written = static_cast<bool>(out);
			}
			if (!written) {
				logger::error("preset '{}': the converted copy could not be written, so it loads unconverted", a_path);
				RemoveTemp(a_temp);
				a_temp.clear();
				return {};
			}
			// Reads the copy back through the same narrow-to-wide conversion LooksMenu will use.
			bool readable = false;
			if (const auto wideTemp = Widen(narrow.c_str())) {
				std::ifstream check(std::filesystem::path(*wideTemp), std::ios::binary | std::ios::ate);
				readable = check && static_cast<std::size_t>(check.tellg()) == result.bytes.size();
			}
			if (!readable) {
				logger::error("preset '{}': the converted copy could not be read back, so it loads unconverted", a_path);
				RemoveTemp(a_temp);
				a_temp.clear();
				return {};
			}
			logger::info("preset '{}': converted {} old sculpt IDs to the new male numbers{}", a_path, result.renamed.size(),
				result.clashes ? std::format(" ({} left as they are because the preset also holds their new IDs)", result.clashes) : std::string{});
			return narrow;
		}

		class LoadPresetHandler : public RE::Scaleform::GFx::FunctionHandler
		{
		public:
			void Call(const Params& a_params) override
			{
				// The object holding LooksMenu's own LoadPreset: 'this' when it is the wrapped F4EE object, otherwise root.f4se.plugins.F4EE.
				GValue f4ee;
				if (a_params.self && a_params.self->IsObject() && a_params.self->HasMember(kOriginalMember)) {
					f4ee = *a_params.self;
				} else if (!a_params.movie || !a_params.movie->GetVariable(&f4ee, "root.f4se.plugins.F4EE") || !f4ee.IsObject() ||
						   !f4ee.HasMember(kOriginalMember)) {
					logger::error("LoadPreset was called but LooksMenu's own LoadPreset is not reachable");
					return;
				}

				std::wstring temp;
				std::string converted;
				if (a_params.movie && a_params.argCount >= 1 && a_params.args[0].IsString()) {
					try {
						converted = ConvertToTemp(a_params.movie, a_params.args[0].GetString(), temp);
					} catch (const std::exception& e) {
						logger::error("preset conversion failed ({}), so it loads unconverted", e.what());
						converted.clear();
					} catch (...) {
						logger::error("preset conversion failed, so it loads unconverted");
						converted.clear();
					}
				}

				GValue result;
				bool called = false;
				if (!converted.empty()) {
					GValue arg(converted.c_str());
					called = f4ee.Invoke(kOriginalMember, &result, &arg, 1);
				} else {
					called = f4ee.Invoke(kOriginalMember, &result, a_params.args, a_params.argCount);
				}
				if (!called) logger::error("LooksMenu's own LoadPreset could not be called");
				if (a_params.retVal) *a_params.retVal = result;
				if (!temp.empty()) RemoveTemp(temp);
			}
		};

		// One handler for the whole session, allocated on the Scaleform heap by FunctionHandler's own operator new.
		[[nodiscard]] LoadPresetHandler* Handler()
		{
			static auto* handler = new LoadPresetHandler();
			return handler;
		}

		class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				const char* name = a_event.menuName.c_str();
				if (a_event.opening && name && std::string_view(name) == kMenuName) {
					F4SE::GetTaskInterface()->AddUITask([] { WrapOpenMenu(); });
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		MenuSink s_sink;
	}

	void Install()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			logger::error("the UI is not available, so presets are not converted");
			return;
		}
		ui->RegisterSink<RE::MenuOpenCloseEvent>(&s_sink);
		logger::info("waiting for LooksMenu to open");
	}

	bool WrapOpenMenu()
	{
		auto* ui = RE::UI::GetSingleton();
		const auto menu = ui ? ui->GetMenu(kMenuName) : nullptr;
		if (!menu || !menu->uiMovie) {
			logger::warn("LooksMenu opened without a movie");
			return false;
		}
		auto* movie = menu->uiMovie.get();
		GValue f4ee;
		if (!movie->GetVariable(&f4ee, "root.f4se.plugins.F4EE") || !f4ee.IsObject()) {
			logger::info("LooksMenu opened without LooksMenu's F4EE plugin, so there is nothing to wrap");
			return false;
		}
		if (f4ee.HasMember(kOriginalMember)) return true;
		GValue original;
		if (!f4ee.GetMember("LoadPreset", &original) || !original.IsObject()) {
			logger::warn("LooksMenu's F4EE plugin has no LoadPreset function (value type {})", static_cast<int>(original.GetType()));
			return false;
		}
		GValue wrapper;
		movie->CreateFunction(&wrapper, Handler());
		const bool kept = f4ee.SetMember(kOriginalMember, original);
		const bool wrapped = kept && f4ee.SetMember("LoadPreset", wrapper);
		GValue looksMenu;
		logger::info("LooksMenu opened: LoadPreset wrapped {}, sculpt list reachable {}", wrapped, GetLooksMenu(movie, looksMenu));
		return wrapped;
	}
}
