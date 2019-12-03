/**
 * @file Moon_Clock_Tests.cpp
 *
 * This module contains the unit tests of the
 * MoonClock::MoonClock class.
 *
 * © 2019 by Richard Walters
 */

#include <gtest/gtest.h>
#include <limits>
#include <map>
#include <MoonClock/MoonClock.hpp>
#include <set>
#include <string>
#include <StringExtensions/StringExtensions.hpp>
#include <Timekeeping/Clock.hpp>
#include <vector>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace {

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

    std::vector< std::string > ReadLuaStringList(lua_State* lua, int listIndex) {
        if (listIndex < 0) {
            listIndex = lua_gettop(lua) + listIndex + 1;
        }
        std::vector< std::string > list(lua_rawlen(lua, listIndex));
        for (size_t i = 0; i < list.size(); ++i) {
            lua_pushinteger(lua, i + 1); // -1 = i
            lua_rawget(lua, listIndex); // -1 = list[i]
            list[i] = lua_tostring(lua, -1);
            lua_pop(lua, 1); // (stack empty)
        }
        return list;
    }

    template< class T > std::vector< std::string > Keys(
        const std::map< std::vector< std::string >, T >& map
    ) {
        std::vector< std::string > keys;
        keys.reserve(map.size());
        for (const auto& entry: map) {
            keys.push_back(StringExtensions::Join(entry.first, "."));
        }
        return keys;
    }

    std::vector< std::string > Keys(
        const std::set< std::vector< std::string > >& set
    ) {
        std::vector< std::string > keys;
        keys.reserve(set.size());
        for (const auto& entry: set) {
            keys.push_back(StringExtensions::Join(entry, "."));
        }
        return keys;
    }

    struct MockClock : public Timekeeping::Clock {
        // Properties

        double time_ = 0.0;

        // Methods

        // Timekeeping::Clock
        virtual double GetCurrentTime() override {
            return time_;
        }
    };

}

/**
 * This is the common fixture for all tests in this module.  It sets up a Lua
 * environment and other useful tools for the tests.
 */
struct Moon_Clock_Tests
    : public ::testing::Test
{
    // Properties

    /**
     * This points to the Lua interpreter that the runner is encapsulating.
     * This interpreter is used to execute the Lua test scripts.
     */
    lua_State* lua = nullptr;

    // ::testing::Test

    virtual void SetUp() override {
        // Create the Lua interpreter.
        lua = lua_newstate(LuaAllocator, NULL);

        // Load standard Lua libraries.
        //
        // Temporarily disable the garbage collector as we load the
        // libraries, to improve performance
        // (http://lua-users.org/lists/lua-l/2008-07/msg00690.html).
        lua_gc(lua, LUA_GCSTOP, 0);
        luaL_openlibs(lua);
        lua_gc(lua, LUA_GCRESTART, 0);
    }

    virtual void TearDown() override {
        lua_close(lua);
    }
};

TEST_F(Moon_Clock_Tests, Find_Functions_In_Composite_Lua_Table) {
    // Set up two tables, "foo" and "bar".  Place "bar" within "foo",
    // and make "foo" global.  Then place a function inside each table,
    // and verify they can be found.
    //
    // foo (table)
    //   |
    //   +-- bar (table)
    //   |    |
    //   |    +-- baz (function)
    //   |
    //   +-- spam (function)
    const auto Baz = [](lua_State* lua) {
        lua_pushstring(lua, "BAZ");
        return 1;
    };
    const auto Spam = [](lua_State* lua) {
        lua_pushstring(lua, "SPAM");
        return 1;
    };
    lua_newtable(lua); // -1 = bar
    lua_pushstring(lua, "baz"); // -1 = "baz", -2 = bar
    lua_pushcfunction(lua, Baz); // -1 = Baz, -2 = "baz", -3 = bar
    lua_rawset(lua, -3); // -1 = bar
    lua_newtable(lua); // -1 = foo, -2 = bar
    lua_insert(lua, -2); // -1 = bar, -2 = foo
    lua_pushstring(lua, "bar"); // -1 = "bar", -2 = bar, -3 = foo
    lua_insert(lua, -2); // -1 = bar, -2 = "bar", -3 = foo
    lua_rawset(lua, -3); // -1 = foo
    lua_pushstring(lua, "spam"); // -1 = "spam", -2 = foo
    lua_pushcfunction(lua, Spam); // -1 = Spam, -2 = "spam", -3 = foo
    lua_rawset(lua, -3); // -1 = foo
    MoonClock::FindFunctionsInComposite(lua, -1); // -1 = results, -2 = foo
    lua_remove(lua, -2); // -1 = results
    struct ExpectedResultsEntry {
        std::string functionResult; // the string the function should return
    };
    std::map< std::vector< std::string >, ExpectedResultsEntry > expectedResults{
        {{"bar", "baz"}, {"BAZ"}},
        {{"spam"}, {"SPAM"}},
    };
    const auto numResults = (int)lua_rawlen(lua, -1);
    for (int i = 1; i <= numResults; ++i) {
        lua_pushinteger(lua, i); // -1 = i, -2 = results
        lua_rawget(lua, -2); // -1 = results[i], -2 = results
        lua_pushstring(lua, "path"); // -1 = "path", -2 = results[i], -3 = results
        lua_rawget(lua, -2); // -1 = results[i].path, -2 = results[i], -3 = results
        const auto path = ReadLuaStringList(lua, -1);
        const auto expectedResultsEntry = expectedResults.find(path);
        ASSERT_FALSE(expectedResultsEntry == expectedResults.end()) << "Extra function found: " << StringExtensions::Join(path, ".");
        lua_pop(lua, 1); // -1 = results[i], -2 = results
        lua_pushstring(lua, "fn"); // -1 = "fn", -2 = results[i], -3 = results
        lua_rawget(lua, -2); // -1 = results[i].fn, -2 = results[i], -3 = results
        lua_pushstring(lua, "parent"); // -1 = "parent", -2 = results[i].fn, -3 = results[i], -4 = results
        lua_rawget(lua, -3); // -1 = results[i].parent, -2 = results[i].fn, -3 = results[i], -4 = results
        lua_pushstring(lua, path[path.size() - 1].c_str()); // -1 = path[#path], -2 = results[i].parent, -3 = results[i].fn, -4 = results[i], -5 = results
        lua_rawget(lua, -2); // -1 = results[i].parent[path[#path]], -2 = results[i].parent, -3 = results[i].fn, -4 = results[i], -5 = results
        EXPECT_EQ(1, lua_compare(lua, -1, -3, LUA_OPEQ));
        lua_pop(lua, 2); // -1 = results[i].fn, -2 = results[i], -3 = results
        lua_call(lua, 0, 1); // -1 = results[i].fn(), -2 = results[i], -3 = results
        EXPECT_EQ(expectedResultsEntry->second.functionResult, (std::string)lua_tostring(lua, -1));
        lua_pop(lua, 2); // -1 = results
        (void)expectedResults.erase(expectedResultsEntry);
    }
    lua_pop(lua, 1); // (stack empty)
    EXPECT_TRUE(expectedResults.empty()) << "Functions not found but expected: " << StringExtensions::Join(Keys(expectedResults), ", ");
}

TEST_F(Moon_Clock_Tests, Find_Functions_In_Lua_Userdata) {
    lua_newuserdata(lua, sizeof(void*));
    lua_newtable(lua);
    lua_pushstring(lua, "__pairs");
    lua_pushvalue(lua, -2);
    lua_pushcclosure(
        lua,
        [](lua_State* lua){
            lua_getglobal(lua, "next");
            lua_pushvalue(lua, lua_upvalueindex(1));
            lua_pushnil(lua);
            return 3;
        },
        1
    );
    lua_rawset(lua, -3);
    lua_pushstring(lua, "__index");
    lua_pushvalue(lua, -2);
    lua_rawset(lua, -3);
    lua_pushstring(lua, "__newindex");
    lua_pushvalue(lua, -2);
    lua_rawset(lua, -3);
    lua_pushstring(lua, "foo");
    lua_pushcfunction(lua, [](lua_State* lua){
        lua_pushstring(lua, "FOO");
        return 1;
    });
    lua_rawset(lua, -3);
    lua_setmetatable(lua, -2);
    MoonClock::FindFunctionsInComposite(lua, -1); // -1 = results, -2 = userdata
    lua_remove(lua, -2); // -1 = results
    struct ExpectedResultsEntry {
        std::string functionResult; // the string the function should return
    };
    std::map< std::vector< std::string >, ExpectedResultsEntry > expectedResults{
        {{"foo"}, {"FOO"}},
    };
    const auto numResults = (int)lua_rawlen(lua, -1);
    for (int i = 1; i <= numResults; ++i) {
        lua_pushinteger(lua, i); // -1 = i, -2 = results
        lua_rawget(lua, -2); // -1 = results[i], -2 = results
        lua_pushstring(lua, "path"); // -1 = "path", -2 = results[i], -3 = results
        lua_rawget(lua, -2); // -1 = results[i].path, -2 = results[i], -3 = results
        const auto path = ReadLuaStringList(lua, -1);
        const auto expectedResultsEntry = expectedResults.find(path);
        EXPECT_FALSE(expectedResultsEntry == expectedResults.end()) << "Extra function found: " << StringExtensions::Join(path, ".");
        if (expectedResultsEntry == expectedResults.end()) {
            lua_pop(lua, 2); // -1 = results
            continue;
        }
        lua_pop(lua, 1); // -1 = results[i], -2 = results
        lua_pushstring(lua, "fn"); // -1 = "fn", -2 = results[i], -3 = results
        lua_rawget(lua, -2); // -1 = results[i].fn, -2 = results[i], -3 = results
        lua_pushstring(lua, "parent"); // -1 = "parent", -2 = results[i].fn, -3 = results[i], -4 = results
        lua_rawget(lua, -3); // -1 = results[i].parent, -2 = results[i].fn, -3 = results[i], -4 = results
        lua_pushstring(lua, path[path.size() - 1].c_str()); // -1 = path[#path], -2 = results[i].parent, -3 = results[i].fn, -4 = results[i], -5 = results
        lua_gettable(lua, -2); // -1 = results[i].parent[path[#path]], -2 = results[i].parent, -3 = results[i].fn, -4 = results[i], -5 = results
        EXPECT_EQ(1, lua_compare(lua, -1, -3, LUA_OPEQ));
        lua_pop(lua, 2); // -1 = results[i].fn, -2 = results[i], -3 = results
        lua_call(lua, 0, 1); // -1 = results[i].fn(), -2 = results[i], -3 = results
        EXPECT_EQ(expectedResultsEntry->second.functionResult, (std::string)lua_tostring(lua, -1));
        lua_pop(lua, 2); // -1 = results
        (void)expectedResults.erase(expectedResultsEntry);
    }
    lua_pop(lua, 1); // (stack empty)
    EXPECT_TRUE(expectedResults.empty()) << "Functions not found but expected: " << StringExtensions::Join(Keys(expectedResults), ", ");
}

TEST_F(Moon_Clock_Tests, Do_Not_Search) {
    std::map< std::vector< std::string >, bool > searchPaths{
        {{"_G"}, true},
        {{"package", "loaded"}, true},
        {{"package", "searchers"}, true},
        {{"string", "pack"}, false},
        {{"package", "loadlib"}, false},
        {{"next"}, false},
    };
    for (const auto& searchPath: searchPaths) {
        lua_getglobal(lua, "_G"); // -1 = parent = _G
        for (const auto& name: searchPath.first) {
            lua_pushstring(lua, name.c_str()); // -1 = name, -2 = parent
            lua_rawget(lua, -2); // -1 = parent[name], -2 = parent
            lua_remove(lua, -2); // -1 = parent[name]
        }
        EXPECT_EQ(
            searchPath.second,
            MoonClock::DoNotSearch(lua, -1)
        ) << StringExtensions::Join(searchPath.first, ".");
    }
}

TEST_F(Moon_Clock_Tests, Find_Functions_In_Global_Variables_Table) {
    lua_getglobal(lua, "_G"); // -1 = _G
    MoonClock::FindFunctionsInComposite(lua, -1); // -1 = results, -2 = _G
    lua_remove(lua, -2); // -1 = results
    std::set< std::vector< std::string > > expectedPaths{
        {"assert"},
        {"collectgarbage"},
        {"coroutine", "status"},
        {"coroutine", "wrap"},
        {"coroutine", "create"},
        {"coroutine", "isyieldable"},
        {"coroutine", "running"},
        {"coroutine", "yield"},
        {"coroutine", "resume"},
        {"utf8", "char"},
        {"utf8", "codes"},
        {"utf8", "offset"},
        {"utf8", "len"},
        {"utf8", "codepoint"},
        {"table", "insert"},
        {"table", "move"},
        {"table", "remove"},
        {"table", "sort"},
        {"table", "concat"},
        {"table", "unpack"},
        {"table", "pack"},
        {"os", "date"},
        {"os", "difftime"},
        {"os", "time"},
        {"os", "rename"},
        {"os", "tmpname"},
        {"os", "setlocale"},
        {"os", "exit"},
        {"os", "clock"},
        {"os", "remove"},
        {"os", "execute"},
        {"os", "getenv"},
        {"io", "flush"},
        {"io", "read"},
        {"io", "close"},
        {"io", "lines"},
        {"io", "open"},
        {"io", "popen"},
        {"io", "input"},
        {"io", "write"},
        {"io", "type"},
        {"io", "output"},
        {"io", "tmpfile"},
        {"debug", "getuservalue"},
        {"debug", "getregistry"},
        {"debug", "setmetatable"},
        {"debug", "setupvalue"},
        {"debug", "debug"},
        {"debug", "setlocal"},
        {"debug", "getinfo"},
        {"debug", "traceback"},
        {"debug", "getlocal"},
        {"debug", "sethook"},
        {"debug", "upvalueid"},
        {"debug", "setuservalue"},
        {"debug", "gethook"},
        {"debug", "upvaluejoin"},
        {"debug", "getupvalue"},
        {"debug", "getmetatable"},
        {"dofile"},
        {"error"},
        {"getmetatable"},
        {"ipairs"},
        {"load"},
        {"loadfile"},
        {"math", "random"},
        {"math", "exp"},
        {"math", "cos"},
        {"math", "fmod"},
        {"math", "asin"},
        {"math", "ult"},
        {"math", "randomseed"},
        {"math", "abs"},
        {"math", "deg"},
        {"math", "sqrt"},
        {"math", "tointeger"},
        {"math", "rad"},
        {"math", "tan"},
        {"math", "max"},
        {"math", "atan"},
        {"math", "ceil"},
        {"math", "sin"},
        {"math", "acos"},
        {"math", "type"},
        {"math", "modf"},
        {"math", "floor"},
        {"math", "log"},
        {"math", "min"},
        {"next"},
        {"package", "loadlib"},
        {"package", "searchpath"},
        {"pairs"},
        {"pcall"},
        {"print"},
        {"rawequal"},
        {"rawget"},
        {"rawlen"},
        {"rawset"},
        {"require"},
        {"select"},
        {"setmetatable"},
        {"string", "find"},
        {"string", "rep"},
        {"string", "match"},
        {"string", "sub"},
        {"string", "packsize"},
        {"string", "lower"},
        {"string", "unpack"},
        {"string", "dump"},
        {"string", "byte"},
        {"string", "gsub"},
        {"string", "reverse"},
        {"string", "char"},
        {"string", "pack"},
        {"string", "gmatch"},
        {"string", "upper"},
        {"string", "len"},
        {"string", "format"},
        {"tonumber"},
        {"tostring"},
        {"type"},
        {"xpcall"},
    };
    const auto numResults = (int)lua_rawlen(lua, -1);
    for (int i = 1; i <= numResults; ++i) {
        lua_pushinteger(lua, i); // -1 = i, -2 = results
        lua_rawget(lua, -2); // -1 = results[i], -2 = results
        lua_pushstring(lua, "path"); // -1 = "path", -2 = results[i], -3 = results
        lua_rawget(lua, -2); // -1 = results[i].path, -2 = results[i], -3 = results
        const auto path = ReadLuaStringList(lua, -1);
        const auto expectedPathsEntry = expectedPaths.find(path);
        EXPECT_FALSE(expectedPathsEntry == expectedPaths.end()) << "Extra function found: " << StringExtensions::Join(path, ".");
        if (expectedPathsEntry == expectedPaths.end()) {
            lua_pop(lua, 2); // -1 = results
            continue;
        }
        lua_pop(lua, 1); // -1 = results[i], -2 = results
        lua_pushstring(lua, "fn"); // -1 = "fn", -2 = results[i], -3 = results
        lua_rawget(lua, -2); // -1 = results[i].fn, -2 = results[i], -3 = results
        lua_pushstring(lua, "parent"); // -1 = "parent", -2 = results[i].fn, -3 = results[i], -4 = results
        lua_rawget(lua, -3); // -1 = results[i].parent, -2 = results[i].fn, -3 = results[i], -4 = results
        lua_pushstring(lua, path[path.size() - 1].c_str()); // -1 = path[#path], -2 = results[i].parent, -3 = results[i].fn, -4 = results[i], -5 = results
        lua_rawget(lua, -2); // -1 = results[i].parent[path[#path]], -2 = results[i].parent, -3 = results[i].fn, -4 = results[i], -5 = results
        EXPECT_EQ(1, lua_compare(lua, -1, -3, LUA_OPEQ));
        lua_pop(lua, 4); // -1 = results
        (void)expectedPaths.erase(expectedPathsEntry);
    }
    lua_pop(lua, 1); // (stack empty)
    EXPECT_TRUE(expectedPaths.empty()) << "Functions not found but expected: " << StringExtensions::Join(Keys(expectedPaths), ", ");
}

TEST_F(Moon_Clock_Tests, Default_Instruments) {
    // Simulated test case:
    // * We have two functions, "foo" and "bar".
    // * "foo" calls "bar" twice.
    //
    // time   call             total time
    //  0.5   (start instrumentation)
    //  1.0   -> foo
    //  1.2            -> bar
    //  1.3      foo <-        0.1
    //  1.45           -> bar
    //  1.5      foo <-        0.05
    //  1.6   <-               0.6
    //  1.7   (stop instrumentation)
    //
    MoonClock::MoonClock moonClock;
    std::shared_ptr< lua_State > sharedLua(
        lua,
        [](lua_State*){}
    );
    const auto mockClock = std::make_shared< MockClock >();
    moonClock.SetClock(mockClock);
    mockClock->time_ = 0.5;
    moonClock.StartInstrumentation(sharedLua);
    const auto context = moonClock.GetDefaultContext();
    mockClock->time_ = 1.0;
    MoonClock::MoonClock::DefaultBeforeInstrument(lua, context, {"foo"});
    mockClock->time_ = 1.2;
    MoonClock::MoonClock::DefaultBeforeInstrument(lua, context, {"bar"});
    mockClock->time_ = 1.3;
    MoonClock::MoonClock::DefaultAfterInstrument(lua, context, {"bar"});
    mockClock->time_ = 1.45;
    MoonClock::MoonClock::DefaultBeforeInstrument(lua, context, {"bar"});
    mockClock->time_ = 1.5;
    MoonClock::MoonClock::DefaultAfterInstrument(lua, context, {"bar"});
    mockClock->time_ = 1.6;
    MoonClock::MoonClock::DefaultAfterInstrument(lua, context, {"foo"});
    mockClock->time_ = 1.7;
    moonClock.StopInstrumentation();
    const auto report = moonClock.GenerateReport();
    EXPECT_EQ(
        (std::map< MoonClock::Path, MoonClock::FunctionInformation >({
            {{"foo"}, {1, 0.6, 0.6, 0.6, {{{"bar"}, {2, 0.15}}}}},
            {{"bar"}, {2, 0.05, 0.15, 0.1, {}}},
        })),
        report.functionInfo
    );
    EXPECT_NEAR(1.2, report.totalTime, std::numeric_limits< decltype(report.totalTime) >::epsilon() * 2);
}

TEST_F(Moon_Clock_Tests, Default_Instruments_Second_Run) {
    // Simulated test case:
    // * We have two functions, "foo" and "bar".
    // * "foo" calls "bar" twice.
    //
    // time   call             total time
    //  0.5   (start instrumentation)
    //  1.0   -> foo
    //  1.2            -> bar
    //  1.3      foo <-        0.1
    //  1.45           -> bar
    //  1.5      foo <-        0.05
    //  1.6   <-               0.6
    //  1.7   (stop instrumentation)
    //
    //  1.8   (start instrumentation)
    //  1.9   -> foo
    //  2.0            -> bar
    //  2.1      foo <-        0.1
    //  2.2            -> bar
    //  2.3      foo <-        0.1
    //  2.4   <-               0.5
    //  2.5   (stop instrumentation)
    MoonClock::MoonClock moonClock;
    std::shared_ptr< lua_State > sharedLua(
        lua,
        [](lua_State*){}
    );
    const auto mockClock = std::make_shared< MockClock >();
    moonClock.SetClock(mockClock);
    mockClock->time_ = 0.5;
    moonClock.StartInstrumentation(sharedLua);
    auto context = moonClock.GetDefaultContext();
    mockClock->time_ = 1.0;
    MoonClock::MoonClock::DefaultBeforeInstrument(lua, context, {"foo"});
    mockClock->time_ = 1.2;
    MoonClock::MoonClock::DefaultBeforeInstrument(lua, context, {"bar"});
    mockClock->time_ = 1.3;
    MoonClock::MoonClock::DefaultAfterInstrument(lua, context, {"bar"});
    mockClock->time_ = 1.45;
    MoonClock::MoonClock::DefaultBeforeInstrument(lua, context, {"bar"});
    mockClock->time_ = 1.5;
    MoonClock::MoonClock::DefaultAfterInstrument(lua, context, {"bar"});
    mockClock->time_ = 1.6;
    MoonClock::MoonClock::DefaultAfterInstrument(lua, context, {"foo"});
    mockClock->time_ = 1.7;
    moonClock.StopInstrumentation();
    (void)moonClock.GenerateReport();
    mockClock->time_ = 1.8;
    moonClock.StartInstrumentation(sharedLua);
    context = moonClock.GetDefaultContext();
    mockClock->time_ = 1.9;
    MoonClock::MoonClock::DefaultBeforeInstrument(lua, context, {"foo"});
    mockClock->time_ = 2.0;
    MoonClock::MoonClock::DefaultBeforeInstrument(lua, context, {"bar"});
    mockClock->time_ = 2.1;
    MoonClock::MoonClock::DefaultAfterInstrument(lua, context, {"bar"});
    mockClock->time_ = 2.2;
    MoonClock::MoonClock::DefaultBeforeInstrument(lua, context, {"bar"});
    mockClock->time_ = 2.3;
    MoonClock::MoonClock::DefaultAfterInstrument(lua, context, {"bar"});
    mockClock->time_ = 2.4;
    MoonClock::MoonClock::DefaultAfterInstrument(lua, context, {"foo"});
    mockClock->time_ = 2.5;
    moonClock.StopInstrumentation();
    const auto report = moonClock.GenerateReport();
    EXPECT_EQ(
        (std::map< MoonClock::Path, MoonClock::FunctionInformation >({
            {{"foo"}, {1, 0.5, 0.5, 0.5, {{{"bar"}, {2, 0.2}}}}},
            {{"bar"}, {2, 0.1, 0.2, 0.1, {}}},
        })),
        report.functionInfo
    );
    EXPECT_NEAR(0.7, report.totalTime, std::numeric_limits< decltype(report.totalTime) >::epsilon() * 2);
}

TEST_F(Moon_Clock_Tests, Default_Instruments_Recursion) {
    MoonClock::MoonClock moonClock;
    std::shared_ptr< lua_State > sharedLua(
        lua,
        [](lua_State*){}
    );
    const auto mockClock = std::make_shared< MockClock >();
    moonClock.SetClock(mockClock);
    moonClock.StartInstrumentation(sharedLua);
    const auto context = moonClock.GetDefaultContext();
    mockClock->time_ = 1.0;
    MoonClock::MoonClock::DefaultBeforeInstrument(lua, context, {"foo"});
    mockClock->time_ = 1.2;
    MoonClock::MoonClock::DefaultBeforeInstrument(lua, context, {"foo"});
    mockClock->time_ = 1.3;
    MoonClock::MoonClock::DefaultAfterInstrument(lua, context, {"foo"});
    mockClock->time_ = 1.4;
    MoonClock::MoonClock::DefaultAfterInstrument(lua, context, {"foo"});
    auto report = moonClock.GenerateReport();
    auto& fooInfo = report.functionInfo[{"foo"}];
    EXPECT_EQ(2, fooInfo.numCalls);
    EXPECT_NEAR(0.1, fooInfo.minTime, std::numeric_limits< decltype(fooInfo.minTime) >::epsilon() * 2);
    EXPECT_NEAR(0.5, fooInfo.totalTime, std::numeric_limits< decltype(fooInfo.totalTime) >::epsilon() * 2);
    EXPECT_NEAR(0.4, fooInfo.maxTime, std::numeric_limits< decltype(fooInfo.maxTime) >::epsilon() * 2);
    EXPECT_EQ(
        (std::map< MoonClock::Path, MoonClock::CallsInformation >({
            {{"foo"}, {1, 0.1}},
        })),
        fooInfo.calls
    );
}

TEST_F(Moon_Clock_Tests, Instrument_Single_Function) {
    MoonClock::MoonClock moonClock;
    std::shared_ptr< lua_State > sharedLua(
        lua,
        [](lua_State*){}
    );
    lua_pushcfunction(lua, [](lua_State* lua){
        lua_pushinteger(lua, lua_tointeger(lua, -1) * 2);
        return 1;
    });
    lua_setglobal(lua, "foo");
    lua_getglobal(lua, "foo");
    lua_call(lua, 0, 0);
    std::vector< std::string > lines;
    const auto before = [](lua_State* lua, void* context, const MoonClock::Path& path) {
        auto& lines = *(std::vector< std::string >*)context;
        lines.push_back(
            std::string("before: ")
            + StringExtensions::Join(path, ".")
        );
    };
    const auto after = [](lua_State* lua, void* context, const MoonClock::Path& path) {
        auto& lines = *(std::vector< std::string >*)context;
        lines.push_back(
            std::string("after: ")
            + StringExtensions::Join(path, ".")
        );
    };
    moonClock.StartInstrumentation(sharedLua, before, after, &lines);
    for (size_t i = 0; i < 3; ++i) {
        lua_getglobal(lua, "foo");
        lua_pushinteger(lua, i);
        lua_call(lua, 1, 1);
        EXPECT_EQ(i * 2, lua_tointeger(lua, -1));
        lua_pop(lua, 1);
    }
    moonClock.StopInstrumentation();
    lua_getglobal(lua, "foo");
    lua_pushinteger(lua, 42);
    lua_call(lua, 1, 1);
    EXPECT_EQ(84, lua_tointeger(lua, -1));
    lua_pop(lua, 1);
    EXPECT_EQ(
        std::vector< std::string >({
            "before: foo",
            "after: foo",
            "before: foo",
            "after: foo",
            "before: foo",
            "after: foo",
        }),
        lines
    );
}

TEST_F(Moon_Clock_Tests, Instrument_Function_Found_In_Userdata) {
    MoonClock::MoonClock moonClock;
    std::shared_ptr< lua_State > sharedLua(
        lua,
        [](lua_State*){}
    );
    lua_newuserdata(lua, sizeof(void*));
    lua_newtable(lua);
    lua_pushstring(lua, "__pairs");
    lua_pushvalue(lua, -2);
    lua_pushcclosure(
        lua,
        [](lua_State* lua){
            lua_getglobal(lua, "next");
            lua_pushvalue(lua, lua_upvalueindex(1));
            lua_pushnil(lua);
            return 3;
        },
        1
    );
    lua_rawset(lua, -3);
    lua_pushstring(lua, "__index");
    lua_pushvalue(lua, -2);
    lua_rawset(lua, -3);
    lua_pushstring(lua, "__newindex");
    lua_pushvalue(lua, -2);
    lua_rawset(lua, -3);
    lua_pushstring(lua, "bar");
    lua_pushcfunction(lua, [](lua_State* lua){
        lua_pushinteger(lua, lua_tointeger(lua, -1) * 2);
        return 1;
    });
    lua_rawset(lua, -3);
    lua_setmetatable(lua, -2);
    lua_setglobal(lua, "foo");
    lua_getglobal(lua, "foo");
    lua_getfield(lua, -1, "bar");
    lua_remove(lua, -2);
    lua_call(lua, 0, 0);
    std::vector< std::string > lines;
    const auto before = [](lua_State* lua, void* context, const MoonClock::Path& path) {
        auto& lines = *(std::vector< std::string >*)context;
        lines.push_back(
            std::string("before: ")
            + StringExtensions::Join(path, ".")
        );
    };
    const auto after = [](lua_State* lua, void* context, const MoonClock::Path& path) {
        auto& lines = *(std::vector< std::string >*)context;
        lines.push_back(
            std::string("after: ")
            + StringExtensions::Join(path, ".")
        );
    };
    moonClock.StartInstrumentation(sharedLua, before, after, &lines);
    for (size_t i = 0; i < 3; ++i) {
        lua_getglobal(lua, "foo");
        lua_getfield(lua, -1, "bar");
        lua_remove(lua, -2);
        lua_pushinteger(lua, i);
        lua_call(lua, 1, 1);
        EXPECT_EQ(i * 2, lua_tointeger(lua, -1));
        lua_pop(lua, 1);
    }
    moonClock.StopInstrumentation();
    lua_getglobal(lua, "foo");
    lua_getfield(lua, -1, "bar");
    lua_remove(lua, -2);
    lua_pushinteger(lua, 42);
    lua_call(lua, 1, 1);
    EXPECT_EQ(84, lua_tointeger(lua, -1));
    lua_pop(lua, 1);
    EXPECT_EQ(
        std::vector< std::string >({
            "before: foo.bar",
            "after: foo.bar",
            "before: foo.bar",
            "after: foo.bar",
            "before: foo.bar",
            "after: foo.bar",
        }),
        lines
    );
}
