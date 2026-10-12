// Runs the plugin's preset conversion outside the game: efs_convert_cli <preset.json> <sculpt-ids.txt> <out.json>
// sculpt-ids.txt lists the edited character's sculpt IDs in decimal, one per line.
#include "Convert.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>

int main(int argc, char** argv)
{
	if (argc != 4) {
		std::cerr << "usage: efs_convert_cli <preset.json> <sculpt-ids.txt> <out.json>\n";
		return 2;
	}
	std::ifstream in(argv[1], std::ios::binary);
	if (!in) {
		std::cout << "error: cannot open preset\n";
		return 1;
	}
	const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	std::set<std::uint32_t> ids;
	std::ifstream list(argv[2]);
	for (std::string line; std::getline(list, line);) {
		if (!line.empty()) ids.insert(static_cast<std::uint32_t>(std::stoul(line)));
	}
	const auto result = EFS::Convert::ConvertPreset(bytes, [&](std::uint32_t a_id) { return ids.contains(a_id); });
	if (!result.error.empty()) {
		std::cout << "unchanged: " << result.error << "\n";
		return 0;
	}
	if (result.renamed.empty()) {
		std::cout << "unchanged: nothing to convert, clashes " << result.clashes << "\n";
		return 0;
	}
	std::ofstream out(argv[3], std::ios::binary | std::ios::trunc);
	out.write(result.bytes.data(), static_cast<std::streamsize>(result.bytes.size()));
	std::cout << "converted " << result.renamed.size() << ", clashes " << result.clashes << "\n";
	return 0;
}
