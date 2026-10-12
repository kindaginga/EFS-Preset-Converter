#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace EFS::Convert
{
	// Old releases used 500495 to 12517999 for both sexes; male sculpts now use female ID + kShift.
	inline constexpr std::uint32_t kOldMin = 500495;
	inline constexpr std::uint32_t kOldMax = 12517999;
	inline constexpr std::uint32_t kShift = 100000000;

	// One name inside "Morphs" > "Regions".
	struct RegionKey
	{
		std::size_t start{ 0 };   // first byte after the opening quote
		std::size_t length{ 0 };  // bytes up to the closing quote, escapes included
		std::string text;         // the name with escapes decoded
		std::uint32_t id{ 0 };    // the sculpt ID LooksMenu reads from the name
	};

	struct Scan
	{
		bool ok{ false };
		std::string error;
		std::vector<RegionKey> keys;
	};

	struct Result
	{
		std::string bytes;                                             // the converted preset; empty when nothing changed
		std::vector<std::pair<std::uint32_t, std::uint32_t>> renamed;  // old ID, new ID
		std::size_t clashes{ 0 };                                      // renames skipped because two names would share one ID
		std::string error;                                             // why the preset was left alone, when it could not be read
	};

	// Reads a sculpt ID from a name the same way LooksMenu does (sscanf_s "%X" into a zeroed UInt32).
	[[nodiscard]] std::uint32_t ReadSculptId(const std::string& a_name);

	// Finds every name in the preset's "Morphs" > "Regions" object.
	[[nodiscard]] Scan FindRegionKeys(std::string_view a_bytes);

	// Moves each old shared ID to ID + kShift when the edited character's sculpt list has the new ID and not the old one.
	[[nodiscard]] Result ConvertPreset(std::string_view a_bytes, const std::function<bool(std::uint32_t)>& a_hasSculpt);
}
