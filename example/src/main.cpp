/**
 * @file main.cpp
 *
 * This module holds the main() function, which is the entrypoint
 * to the program.
 *
 * © 2019 by Richard Walters
 */

#include <memory>
#include <MoonClock/MoonClock.hpp>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <StringExtensions/StringExtensions.hpp>
#include <SystemAbstractions/File.hpp>
#include <SystemAbstractions/Time.hpp>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace {

    /**
     * This function prints to the standard error stream information
     * about how to use this program.
     */
    void PrintUsageInformation() {
        fprintf(
            stderr,
            (
                "Usage: MoonClockTest SCRIPT FUNCTION\n"
                "\n"
                "Load a given Lua SCRIPT, instrument its functions, call\n"
                "the given FUNCTION, and print out a report on performance\n"
                "metrics associated with all Lua functions found.\n"
                "\n"
                "SCRIPT    Path to file containing Lua functions to execute.\n"
                "\n"
                "FUNCTION  Name of the Lua function to call.\n"
            )
        );
    }

    /**
     * This contains variables set through the operating system environment
     * or the command-line arguments.
     */
    struct Environment {
        /**
         * This is the path to the file containing the Lua functions to call.
         */
        std::string scriptPath;

        /**
         * This is the name of the Lua function to call.
         */
        std::string functionName;
    };

    /**
     * This function updates the program environment to incorporate
     * any applicable command-line arguments.
     *
     * @param[in] argc
     *     This is the number of command-line arguments given to the program.
     *
     * @param[in] argv
     *     This is the array of command-line arguments given to the program.
     *
     * @param[in,out] environment
     *     This is the environment to update.
     *
     * @return
     *     An indication of whether or not the function succeeded is returned.
     */
    bool ProcessCommandLineArguments(
        int argc,
        char* argv[],
        Environment& environment
    ) {
        size_t state = 0;
        for (int i = 1; i < argc; ++i) {
            const std::string arg(argv[i]);
            switch (state) {
                case 0: { // SCRIPT
                    environment.scriptPath = arg;
                    state = 1;
                } break;

                case 1: { // FUNCTION
                    environment.functionName = arg;
                    state = 2;
                } break;

                case 2: { // extra argument
                    fprintf(
                        stderr,
                        "extra arguments given\n"
                    );
                    return false;
                } break;
            }
        }
        if (state < 1) {
            fprintf(
                stderr,
                "no SCRIPT given\n"
            );
            return false;
        }
        if (state < 2) {
            fprintf(
                stderr,
                "no FUNCTION given\n"
            );
            return false;
        }
        return true;
    }

    /**
     * This function is provided to the Lua interpreter for use in
     * allocating memory.
     *
     * @param[in] ud
     *     This is the "ud" opaque pointer given to lua_newstate when
     *     the Lua interpreter state was created.
     *
     * @param[in] ptr
     *     If not NULL, this points to the memory block to be
     *     freed or reallocated.
     *
     * @param[in] osize
     *     This is the size of the memory block pointed to by "ptr".
     *
     * @param[in] nsize
     *     This is the number of bytes of memory to allocate or reallocate,
     *     or zero if the given block should be freed instead.
     *
     * @return
     *     A pointer to the allocated or reallocated memory block is
     *     returned, or NULL is returned if the given memory block was freed.
     */
    void* LuaAllocator(void* ud, void* ptr, size_t osize, size_t nsize) {
        if (nsize == 0) {
            free(ptr);
            return NULL;
        } else {
            return realloc(ptr, nsize);
        }
    }

    /**
     * This structure is used to pass state from the caller of the
     * lua_load function to the reader function supplied to lua_load.
     */
    struct LuaReaderState {
        /**
         * This is the code chunk to be read by the Lua interpreter.
         */
        const std::string* chunk = nullptr;

        /**
         * This flag indicates whether or not the Lua interpreter
         * has been fed the code chunk as input yet.
         */
        bool read = false;
    };

    /**
     * This function is provided to the Lua interpreter in order to
     * read the next chunk of code to be interpreted.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     *
     * @param[in] data
     *     This points to a LuaReaderState structure containing
     *     state information provided by the caller of lua_load.
     *
     * @param[out] size
     *     This points to where the size of the next chunk of code
     *     should be stored.
     *
     * @return
     *     A pointer to the next chunk of code to interpret is returned.
     *
     * @retval NULL
     *     This is returned once all the code to be interpreted has
     *     been read.
     */
    const char* LuaReader(lua_State* lua, void* data, size_t* size) {
        LuaReaderState* state = (LuaReaderState*)data;
        if (state->read) {
            return NULL;
        } else {
            state->read = true;
            *size = state->chunk->length();
            return state->chunk->c_str();
        }
    }

    /**
     * This function is provided to the Lua interpreter when lua_pcall
     * is called.  It is called by the Lua interpreter if a runtime
     * error occurs while interpreting scripts.
     *
     * @param[in] lua
     *     This points to the state of the Lua interpreter.
     *
     * @return
     *     The number of return values that have been pushed onto the
     *     Lua stack by the function as return values of the function
     *     is returned.
     */
    int LuaTraceback(lua_State* lua) {
        const char* message = lua_tostring(lua, 1);
        if (message == NULL) {
            if (!lua_isnoneornil(lua, 1)) {
                if (!luaL_callmeta(lua, 1, "__tostring")) {
                    lua_pushliteral(lua, "(no error message)");
                }
            }
        } else {
            luaL_traceback(lua, lua, message, 1);
        }
        return 1;
    }

    /**
     * This function loads the contents of the file with the given path.
     *
     * @param[in] filePath
     *     This is the path of the file to load.
     *
     * @return
     *     This file's contents are returned.  If the file could not be
     *     read, an empty string is returned.
     */
    std::string LoadFile(const std::string& filePath) {
        SystemAbstractions::File file(filePath);
        if (!file.OpenReadOnly()) {
            fprintf(stderr, "Unable to open file '%s'\n", filePath.c_str());
            return "";
        }
        std::vector< uint8_t > fileContentsAsVector(file.GetSize());
        if (file.Read(fileContentsAsVector) != fileContentsAsVector.size()) {
            fprintf(stderr,"Unable to read file '%s'\n", filePath.c_str());
            return "";
        }
        return std::string(
            (const char*)fileContentsAsVector.data(),
            fileContentsAsVector.size()
        );
    }

    bool LoadScript(
        lua_State* lua,
        const std::string& name,
        const std::string& script
    ) {
        lua_settop(lua, 0);
        lua_pushcfunction(lua, LuaTraceback);
        LuaReaderState luaReaderState;
        luaReaderState.chunk = &script;
        std::string errorMessage;
        switch (const int luaLoadResult = lua_load(lua, LuaReader, &luaReaderState, ("=" + name).c_str(), "t")) {
            case LUA_OK: {
                const int luaPCallResult = lua_pcall(lua, 0, 0, 1);
                if (luaPCallResult != LUA_OK) {
                    if (!lua_isnil(lua, -1)) {
                        errorMessage = lua_tostring(lua, -1);
                    }
                }
            } break;
            case LUA_ERRSYNTAX: {
                errorMessage = lua_tostring(lua, -1);
            } break;
            case LUA_ERRMEM: {
                errorMessage = "LUA_ERRMEM";
            } break;
            case LUA_ERRGCMM: {
                errorMessage = "LUA_ERRGCMM";
            } break;
            default: {
                errorMessage = StringExtensions::sprintf("(unexpected lua_load result: %d)", luaLoadResult);
            } break;
        }
        lua_settop(lua, 0);
        if (errorMessage.empty()) {
            fwrite(errorMessage.data(), errorMessage.length(), 1, stderr);
            return true;
        } else {
            return false;
        }
    }

    bool Call(
        lua_State* lua,
        const std::string& luaFunctionName
    ) {
        const int numberOfArguments = lua_gettop(lua);
        lua_pushcfunction(lua, LuaTraceback);
        lua_insert(lua, 1);
        lua_getglobal(lua, luaFunctionName.c_str());
        lua_insert(lua, 2);
        const int luaPCallResult = lua_pcall(lua, numberOfArguments, 0, 1);
        std::string errorMessage;
        if (luaPCallResult != LUA_OK) {
            if (!lua_isnil(lua, -1)) {
                errorMessage = lua_tostring(lua, -1);
            }
        }
        lua_settop(lua, 0);
        if (errorMessage.empty()) {
            fwrite(errorMessage.data(), errorMessage.length(), 1, stderr);
            return true;
        } else {
            return false;
        }
    }

    struct Clock : public Timekeeping::Clock {
        // Properties

        SystemAbstractions::Time time_;

        // Methods

        // Timekeeping::Clock

        virtual double GetCurrentTime() override {
            return time_.GetTime();
        }
    };

}

/**
 * This function is the entrypoint of the program.
 * It loads a Lua script, instruments it, calls a Lua function,
 * and finally prints out a report generated by the instrumentation.
 *
 * @param[in] argc
 *     This is the number of command-line arguments given to the program.
 *
 * @param[in] argv
 *     This is the array of command-line arguments given to the program.
 */
int main(int argc, char* argv[]) {
#ifdef _WIN32
    //_crtBreakAlloc = 18;
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif /* _WIN32 */
    Environment environment;
    (void)setbuf(stdout, NULL);
    if (!ProcessCommandLineArguments(argc, argv, environment)) {
        PrintUsageInformation();
        return EXIT_FAILURE;
    }
    std::shared_ptr< lua_State > lua(
        lua_newstate(LuaAllocator, NULL),
        [](lua_State* lua) {
            lua_close(lua);
        }
    );
    MoonClock::MoonClock moonClock;
    const auto clock = std::make_shared< Clock >();
    moonClock.SetClock(clock);
    lua_gc(lua.get(), LUA_GCSTOP, 0);
    luaL_openlibs(lua.get());
    lua_gc(lua.get(), LUA_GCRESTART, 0);
    const auto script = LoadFile(environment.scriptPath);
    if (script.empty()) {
        return EXIT_FAILURE;
    }
    if (!LoadScript(lua.get(), environment.scriptPath, script)) {
        return EXIT_FAILURE;
    }
    moonClock.StartInstrumentation(lua);
    if (!Call(lua.get(), environment.functionName)) {
        return EXIT_FAILURE;
    }
    moonClock.StopInstrumentation();
    const auto report = moonClock.GenerateReport();
    printf("-----------------------------------------------------------------------------------------\n");
    printf("Report:\n");
    printf("-----------------------------------------------------------------------------------------\n");
    printf(
        "%-20s %7s  %14s %14s %14s %14s\n",
        "FUNC", "#", "MIN", "MAX", "TOTAL", "AVG"
    );
    for (const auto& fn: report.functionInfo) {
        printf(
            "%-20s %7zu  %14.9lf %14.9lf %14.9lf %14.9lf\n",
            StringExtensions::Join(fn.first, ".").c_str(),
            fn.second.numCalls,
            fn.second.minTime,
            fn.second.maxTime,
            fn.second.totalTime,
            (fn.second.totalTime / fn.second.numCalls)
        );
        for (const auto& subfn: fn.second.calls) {
            printf(
                "  %-18s %7zu  %14s %14s %14.9lf %14s\n",
                StringExtensions::Join(subfn.first, ".").c_str(),
                subfn.second.numCalls,
                "",
                "",
                subfn.second.totalTime,
                ""
            );
        }
    }
    printf("-----------------------------------------------------------------------------------------\n");
    return EXIT_SUCCESS;
}
