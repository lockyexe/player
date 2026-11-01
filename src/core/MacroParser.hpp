#pragma once

#include "Macro.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <variant>

namespace mp {

struct ParseError {
    std::string message;
};

using ParseResult = std::variant<Macro, ParseError>;

// Detects the format from the bytes (GDR2 magic "GDR", JSON '{', otherwise
// msgpack) and parses it. Never invents inputs: anything malformed is an error.
ParseResult parseMacro(std::span<const uint8_t> data);
ParseResult parseMacroFile(std::filesystem::path const& path);

char const* formatName(Format f);

} // namespace mp
