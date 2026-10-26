#pragma once

#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <vector>

typedef uint64_t program_state_t;

typedef struct program_state_mapping_s
{
    program_state_t state;
    const char*     name;
} program_state_mapping_t;

//Line buffer size of the cache reader; a line filling it may have been truncated.
static constexpr size_t PROGRAM_STATE_CACHE_MAX_LINE = 4096;

/*
    Reader receives the caller's path. Unknown/cross-category or truncated lines
    are rejected as a whole rather than silently becoming a different variant.
*/
template <class Reader, class Callback>
void R_ReadProgramStateCache(const char* filename, const program_state_mapping_t* mapping, size_t mapping_size, Reader read, Callback callback)
{
    std::istringstream        input(read(filename));
    std::string               line;
    std::set<program_state_t> seen;

    while (std::getline(input, line))
    {
        if (line.size() >= PROGRAM_STATE_CACHE_MAX_LINE - 1)
            continue;

        std::istringstream tokens(line);
        std::string        token;
        program_state_t    state  = 0;
        bool               valid  = true;
        bool               filled = false;
        bool               none   = false;

        while (tokens >> token)
        {
            if (token == "NONE")
            {
                if (filled)
                    valid = false;

                none   = true;
                filled = true;
                continue;
            }

            bool known = false;

            for (size_t i = 0; i < mapping_size; ++i)
            {
                if (token == mapping[i].name)
                {
                    state |= mapping[i].state;
                    known = true;
                    break;
                }
            }

            if (!known || none)
                valid = false;

            filled = true;
        }

        if (filled && valid && seen.insert(state).second)
            callback(state);
    }
}
