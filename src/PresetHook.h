#pragma once

namespace EFS::PresetHook
{
	// Name under which the wrapper keeps LooksMenu's own LoadPreset on the F4EE object.
	inline constexpr auto kOriginalMember = "EFSOriginalLoadPreset";

	// Registers for LooksMenu opening; each time it opens, its LoadPreset is wrapped so presets pass through the converter.
	void Install();

	// Wraps LoadPreset in the open LooksMenu movie (UI thread); false when LooksMenu's F4EE plugin is not there.
	bool WrapOpenMenu();
}
