#include "Convert.h"

#include <cstdio>
#include <format>
#include <map>
#include <set>
#include <unordered_map>

namespace EFS::Convert
{
	namespace
	{
		enum class Role
		{
			kRoot,
			kMorphs,
			kRegions,
			kOther
		};

		// Reads JSON the way LooksMenu's JsonCpp reader does: comments count as white space and scalars are read loosely.
		class Scanner
		{
		public:
			explicit Scanner(std::string_view a_text) :
				_s(a_text)
			{}

			Scan Run()
			{
				Scan scan;
				try {
					SkipSpace();
					if (_i >= _s.size() || _s[_i] != '{') throw Error("the file does not start with an object");
					ReadObject(Role::kRoot);
					for (const auto& [name, throws] : _throws) {
						if (throws) throw Error("a sculpt entry LooksMenu cannot read (\"" + name + "\")");
					}
					scan.ok = true;
					scan.keys = std::move(_keys);
				} catch (const std::runtime_error& e) {
					scan.error = e.what();
				}
				return scan;
			}

		private:
			[[nodiscard]] std::runtime_error Error(const std::string& a_what) const
			{
				return std::runtime_error(std::format("{} at byte {}", a_what, _i));
			}

			bool SkipSpace()
			{
				bool comment = false;
				while (_i < _s.size()) {
					const char c = _s[_i];
					if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
						++_i;
					} else if (c == '/' && _i + 1 < _s.size() && _s[_i + 1] == '/') {
						while (_i < _s.size() && _s[_i] != '\n' && _s[_i] != '\r') ++_i;
						comment = true;
					} else if (c == '/' && _i + 1 < _s.size() && _s[_i + 1] == '*') {
						const auto end = _s.find("*/", _i + 2);
						if (end == std::string_view::npos) throw Error("unterminated comment");
						_i = end + 2;
						comment = true;
					} else {
						break;
					}
				}
				return comment;
			}

			// Consumes the one token JsonCpp's Reader::readObject takes as a separator after a comment.
			void SkipToken()
			{
				if (_i >= _s.size()) return;
				const char c = _s[_i++];
				if (c == '"') {
					while (_i < _s.size()) {
						const char d = _s[_i++];
						if (d == '\\') {
							if (_i < _s.size()) ++_i;
						} else if (d == '"') {
							break;
						}
					}
				} else if ((c >= '0' && c <= '9') || c == '-') {
					while (_i < _s.size() && std::string_view("0123456789.eE+-").find(_s[_i]) != std::string_view::npos) ++_i;
				} else if (c == 't' && _s.substr(_i, 3) == "rue") {
					_i += 3;
				} else if (c == 'f' && _s.substr(_i, 4) == "alse") {
					_i += 4;
				} else if (c == 'n' && _s.substr(_i, 3) == "ull") {
					_i += 3;
				} else if (c == '/' && _i < _s.size()) {
					++_i;
				}
			}

			static void AppendUtf8(std::string& a_out, std::uint32_t a_cp)
			{
				if (a_cp < 0x80) {
					a_out += static_cast<char>(a_cp);
				} else if (a_cp < 0x800) {
					a_out += static_cast<char>(0xC0 | (a_cp >> 6));
					a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
				} else if (a_cp < 0x10000) {
					a_out += static_cast<char>(0xE0 | (a_cp >> 12));
					a_out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
					a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
				} else {
					a_out += static_cast<char>(0xF0 | (a_cp >> 18));
					a_out += static_cast<char>(0x80 | ((a_cp >> 12) & 0x3F));
					a_out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
					a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
				}
			}

			std::uint32_t ReadHex4()
			{
				if (_i + 4 > _s.size()) throw Error("incomplete \\u escape");
				std::uint32_t value = 0;
				for (int k = 0; k < 4; ++k) {
					const char c = _s[_i++];
					value <<= 4;
					if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
					else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
					else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
					else throw Error("bad \\u escape");
				}
				return value;
			}

			// Reads a quoted string at the current position; returns where its contents start and end, and the decoded text.
			std::tuple<std::size_t, std::size_t, std::string> ReadString()
			{
				++_i;
				const auto start = _i;
				std::string text;
				while (true) {
					if (_i >= _s.size()) throw Error("unterminated string");
					const char c = _s[_i];
					if (c == '"') break;
					if (c != '\\') {
						if (!(c == '\r' && _i + 1 < _s.size() && _s[_i + 1] == '\n')) text += c;
						++_i;
						continue;
					}
					if (_i + 1 >= _s.size()) throw Error("unterminated string");
					const char e = _s[_i + 1];
					_i += 2;
					switch (e) {
					case '"': text += '"'; break;
					case '\\': text += '\\'; break;
					case '/': text += '/'; break;
					case 'b': text += '\b'; break;
					case 'f': text += '\f'; break;
					case 'n': text += '\n'; break;
					case 'r': text += '\r'; break;
					case 't': text += '\t'; break;
					case 'u':
						{
							std::uint32_t cp = ReadHex4();
							if (cp >= 0xD800 && cp <= 0xDBFF) {
								if (_i + 6 > _s.size() || _s[_i] != '\\' || _s[_i + 1] != 'u') throw Error("unpaired surrogate");
								_i += 2;
								const auto low = ReadHex4();
								cp = 0x10000 + ((cp & 0x3FF) << 10) + (low & 0x3FF);
							}
							AppendUtf8(text, cp);
							break;
						}
					default:
						throw Error("bad escape in a string");
					}
				}
				const auto end = _i;
				++_i;
				return { start, end - start, std::move(text) };
			}

			void ReadValue(Role a_role)
			{
				SkipSpace();
				if (_i >= _s.size()) throw Error("unexpected end of file");
				const char c = _s[_i];
				if (c == '{') {
					ReadObject(a_role);
				} else if (c == '[') {
					ReadArray();
				} else if (c == '"') {
					(void)ReadString();
				} else {
					const auto start = _i;
					while (_i < _s.size()) {
						const char d = _s[_i];
						if (d == ',' || d == ']' || d == '}' || d == ' ' || d == '\t' || d == '\r' || d == '\n' || d == '/') break;
						++_i;
					}
					const auto token = _s.substr(start, _i - start);
					const bool literal = token == "true" || token == "false" || token == "null";
					const bool number = !token.empty() && token.find_first_not_of("0123456789+-.eE") == std::string_view::npos;
					if (!literal && !number) {
						_i = start;
						throw Error("unexpected text");
					}
				}
			}

			void ReadObject(Role a_role)
			{
				++_i;
				SkipSpace();
				if (_i < _s.size() && _s[_i] == '}') {
					++_i;
					return;
				}
				while (true) {
					SkipSpace();
					if (_i >= _s.size() || _s[_i] != '"') throw Error("expected a name");
					auto [start, length, name] = ReadString();
					const std::string cname(name.c_str());
					SkipSpace();
					if (_i >= _s.size() || _s[_i] != ':') throw Error("expected a colon");
					++_i;
					Role child = Role::kOther;
					if (a_role == Role::kRoot && cname == "Morphs") child = Role::kMorphs;
					else if (a_role == Role::kMorphs && cname == "Regions") child = Role::kRegions;
					if (child != Role::kOther) {
						_keys.clear();
						_throws.clear();
					}
					if (a_role == Role::kRegions) {
						_keys.push_back({ start, length, name, ReadSculptId(name) });
						SkipSpace();
						bool nonScalar = false;
						if (_i < _s.size() && _s[_i] == '[') {
							ReadArray(&nonScalar);
						} else {
							nonScalar = !(_s.substr(_i, 4) == "null");
							ReadValue(child);
						}
						_throws[cname] = nonScalar;
					} else {
						ReadValue(child);
					}
					const bool comment = SkipSpace();
					if (_i >= _s.size()) throw Error("unexpected end of file");
					if (_s[_i] == '}') {
						++_i;
						return;
					}
					if (_s[_i] == ',') ++_i;
					else if (comment) SkipToken();
					else throw Error("expected a comma or a closing brace");
					SkipSpace();
					if (name.empty() && _i < _s.size() && _s[_i] == '}') {
						++_i;
						return;
					}
				}
			}

			void ReadArray(bool* a_nonScalar = nullptr)
			{
				++_i;
				SkipSpace();
				if (_i < _s.size() && _s[_i] == ']') {
					++_i;
					return;
				}
				for (int index = 0;; ++index) {
					SkipSpace();
					if (a_nonScalar && index < 8 && _i < _s.size() && (_s[_i] == '"' || _s[_i] == '[' || _s[_i] == '{')) *a_nonScalar = true;
					ReadValue(Role::kOther);
					SkipSpace();
					if (_i >= _s.size()) throw Error("unexpected end of file");
					if (_s[_i] == ',') {
						++_i;
					} else if (_s[_i] == ']') {
						++_i;
						return;
					} else {
						throw Error("expected a comma or a closing bracket");
					}
				}
			}

			std::string_view _s;
			std::size_t _i{ 0 };
			std::vector<RegionKey> _keys;
			std::map<std::string, bool> _throws;
		};

		[[nodiscard]] bool IsOld(std::uint32_t a_id) noexcept
		{
			return a_id >= kOldMin && a_id <= kOldMax;
		}
	}

	std::uint32_t ReadSculptId(const std::string& a_name)
	{
		unsigned int value = 0;
		(void)sscanf_s(a_name.c_str(), "%X", &value);
		return value;
	}

	Scan FindRegionKeys(std::string_view a_bytes)
	{
		// A UTF-8 byte order mark is skipped; key positions still count from the start of the file.
		const std::size_t skip = a_bytes.starts_with("\xEF\xBB\xBF") ? 3 : 0;
		const auto body = a_bytes.substr(skip);
		auto scan = Scanner(body.substr(0, body.find_first_of("\x1A\xFF"))).Run();
		for (auto& key : scan.keys) key.start += skip;
		return scan;
	}

	Result ConvertPreset(std::string_view a_bytes, const std::function<bool(std::uint32_t)>& a_hasSculpt)
	{
		Result result;
		auto scan = FindRegionKeys(a_bytes);
		if (!scan.ok) {
			result.error = scan.error;
			return result;
		}

		std::map<std::uint32_t, bool> known;
		const auto has = [&](std::uint32_t a_id) {
			const auto it = known.find(a_id);
			if (it != known.end()) return it->second;
			const bool value = a_hasSculpt(a_id);
			known.emplace(a_id, value);
			return value;
		};

		std::vector<bool> move(scan.keys.size(), false);
		std::vector<std::uint32_t> finalId(scan.keys.size());
		std::unordered_map<std::uint32_t, std::set<std::string>> names;
		for (std::size_t k = 0; k < scan.keys.size(); ++k) {
			const auto id = scan.keys[k].id;
			move[k] = IsOld(id) && !has(id) && has(id + kShift);
			finalId[k] = move[k] ? id + kShift : id;
			names[finalId[k]].insert(scan.keys[k].text.c_str());
		}
		std::set<std::uint32_t> clashed;
		for (std::size_t k = 0; k < scan.keys.size(); ++k) {
			if (move[k] && names[finalId[k]].size() > 1) {
				clashed.insert(finalId[k]);
				move[k] = false;
				finalId[k] = scan.keys[k].id;
			}
		}
		result.clashes = clashed.size();

		std::string out;
		out.reserve(a_bytes.size() + 64);
		std::size_t cursor = 0;
		for (std::size_t k = 0; k < scan.keys.size(); ++k) {
			if (!move[k]) continue;
			const auto& key = scan.keys[k];
			out.append(a_bytes.substr(cursor, key.start - cursor));
			out += std::format("{:X}", finalId[k]);
			cursor = key.start + key.length;
			result.renamed.emplace_back(key.id, finalId[k]);
		}
		if (result.renamed.empty()) return result;
		out.append(a_bytes.substr(cursor));

		// Self-check: the rewritten preset must read back with exactly the planned IDs, in the same order.
		const auto check = FindRegionKeys(out);
		bool same = check.ok && check.keys.size() == scan.keys.size();
		for (std::size_t k = 0; same && k < check.keys.size(); ++k) same = check.keys[k].id == finalId[k];
		if (!same) {
			result.renamed.clear();
			result.error = "the converted preset failed its self-check";
			return result;
		}
		result.bytes = std::move(out);
		return result;
	}
}
