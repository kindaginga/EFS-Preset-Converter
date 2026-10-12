#pragma once

// In-game self-test, compiled only into the EFS_AUTOTEST test build and run when Data/F4SE/Plugins/EFSPresetConverter_AutoTest.ini enables it.
namespace EFS::AutoTest
{
	void Configure();
	void OnGameDataReady();
}
