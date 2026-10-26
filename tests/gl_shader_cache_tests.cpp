#include "../src/gl_shader_cache.h"
#include <cassert>
#include <map>

int main()
{
    const char*                        types[] = {"wsurf", "water", "dlight", "dfinal", "studio", "sprite", "triapi", "portal", "drawtexturedrect", "drawfilledrect"};
    std::map<std::string, std::string> files;
    for (size_t i = 0; i < 10; ++i)
        files[std::string(types[i]) + "_cache.txt"] = std::string(types[i]) + "\nNONE\n";
    for (size_t i = 0; i < 10; ++i)
    {
        program_state_mapping_t      mapping[] = {{program_state_t{1} << i, types[i]}};
        std::vector<program_state_t> states;
        const auto                   path = std::string(types[i]) + "_cache.txt";
        auto                         read = [&](const char* requested) {
            assert(path == requested);
            return files.at(requested);
        };
        R_ReadProgramStateCache(path.c_str(), mapping, 1, read, [&](auto state) { states.push_back(state); });
        assert((std::vector<program_state_t>{program_state_t{1} << i, 0}) == states);
    }
    program_state_mapping_t mapping[] = {{1, "A"}, {2, "B"}};
    auto                    read      = [](const char*) {
        return std::string("\nA B\nA B\nUNKNOWN\nA UNKNOWN\nNONE A\nA NONE\nNONE\n") + std::string(PROGRAM_STATE_CACHE_MAX_LINE, 'A') + "\nB\n";
    };
    std::vector<program_state_t> states;
    R_ReadProgramStateCache("malformed", mapping, 2, read, [&](auto state) { states.push_back(state); });
    assert((std::vector<program_state_t>{3, 0, 2}) == states);
    R_ReadProgramStateCache("missing", mapping, 2, [](const char*) { return std::string(); }, [](auto) { assert(false); });
}
