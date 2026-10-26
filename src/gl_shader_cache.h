#pragma once

#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using program_state_t = uint64_t;
struct program_state_mapping_t
{
    program_state_t state;
    const char*     name;
};

// Reader receives the caller's path. Unknown/cross-category or truncated lines
// are rejected as a whole rather than silently becoming a different variant.
template <class Reader, class Callback>
void R_ReadProgramStateCache(const char* filename, const program_state_mapping_t* mapping, size_t mappingSize, Reader read, Callback callback)
{
    std::istringstream        input(read(filename));
    std::string               line;
    std::set<program_state_t> seen;
    while (std::getline(input, line))
    {
        if (line.size() >= 4095) continue;
        std::istringstream tokens(line);
        std::string        token;
        program_state_t    state = 0;
        bool               valid = true, filled = false, none = false;
        while (tokens >> token)
        {
            if (token == "NONE")
            {
                if (filled) valid = false;
                none   = true;
                filled = true;
                continue;
            }
            bool known = false;
            for (size_t i = 0; i < mappingSize; ++i)
            {
                if (token == mapping[i].name)
                {
                    state |= mapping[i].state;
                    known = true;
                    break;
                }
            }
            if (!known || none) valid = false;
            filled = true;
        }
        if (filled && valid && seen.insert(state).second) callback(state);
    }
}
