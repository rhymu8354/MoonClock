/**
 * @file MoonClock.cpp
 *
 * This module contains the implementation of the MoonClock::MoonClock class.
 *
 * © 2019 by Richard Walters
 */

#include <limits>
#include <math.h>
#include <MoonClock/MoonClock.hpp>
#include <set>
#include <stack>
#include <string>
#include <SystemAbstractions/StringExtensions.hpp>
#include <vector>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace {

    /**
     * Push onto the Lua stack a new list containing the strings
     * in the given vector.
     *
     * @param[in] list
     *     This is the sequence of strings to convert into a Lua list.
     */
    void PushLuaStringList(lua_State* lua, const std::vector< std::string >& list) {
        lua_newtable(lua); // -1 = list
        for (size_t i = 0; i < list.size(); ++i) {
            lua_pushinteger(lua, i + 1); // -1 = i+1, -2 = list
            lua_pushstring(lua, list[i].c_str()); // -1 = list[i], -2 = i+1, -3 = list
            lua_rawset(lua, -3); // -1 = list
        }
    }

    std::vector< std::string > ReadLuaStringList(lua_State* lua, int listIndex) {
        if (
            (listIndex < 0)
            && (listIndex >= -LUAI_MAXSTACK)
        ) {
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

}

namespace MoonClock {

    bool CallsInformation::operator==(const CallsInformation& other) const {
        return (
            (numCalls == other.numCalls)
            && (fabs(totalTime - other.totalTime) <= std::numeric_limits< decltype(totalTime) >::epsilon() * 2)
        );
    }

    void PrintTo(
        const CallsInformation& callsInformation,
        std::ostream* os
    ) {
        *os << "{numCalls=" << callsInformation.numCalls;
        *os << ", totalTime=" << callsInformation.totalTime;
        *os << "}";
    }

    bool FunctionInformation::operator==(const FunctionInformation& other) const {
        return (
            (numCalls == other.numCalls)
            && (fabs(minTime - other.minTime) <= std::numeric_limits< decltype(minTime) >::epsilon() * 2)
            && (fabs(totalTime - other.totalTime) <= std::numeric_limits< decltype(totalTime) >::epsilon() * 2)
            && (fabs(maxTime - other.maxTime) <= std::numeric_limits< decltype(maxTime) >::epsilon() * 2)
            && (calls == other.calls)
        );
    }

    void PrintTo(
        const FunctionInformation& functionInformation,
        std::ostream* os
    ) {
        *os << "{numCalls=" << functionInformation.numCalls;
        *os << ", minTime=" << functionInformation.minTime;
        *os << ", totalTime=" << functionInformation.totalTime;
        *os << ", maxTime=" << functionInformation.maxTime;
        *os << ", calls=";
        *os << "(";
        for (const auto& entry: functionInformation.calls) {
            *os << "{ ";
            bool isFirstStep = true;
            for (const auto& step: entry.first) {
                if (!isFirstStep) {
                    *os << ", ";
                }
                isFirstStep = false;
                *os << "\"" << step << "\"";
            }
            *os << " }, ";
            PrintTo(entry.second, os);
        }
        *os << ")";
        *os << "}";
    }

    void FindFunctionsInCompositeLuaTable(
        lua_State* lua,
        int tableIndex,
        int resultsIndex,
        std::vector< std::string >& path
    ) {
        if (tableIndex < 0) {
            tableIndex = lua_gettop(lua) + tableIndex + 1;
        }
        lua_pushnil(lua);  // -1 = old key
        while (lua_next(lua, tableIndex) != 0) { // -1 = new value, -2 = new key
            if (!DoNotSearch(lua, -1)) {
                if (lua_istable(lua, -1)) {
                    path.push_back(lua_tostring(lua, -2));
                    FindFunctionsInCompositeLuaTable(lua, -1, resultsIndex, path);
                    path.pop_back();
                } else if (lua_isfunction(lua, -1)) {
                    const auto nextResultsEntryIndex = lua_rawlen(lua, resultsIndex) + 1;
                    lua_pushinteger(lua, nextResultsEntryIndex); // -1 = #results+1, -2 = new value, -3 = new key
                    lua_newtable(lua); // -1 = resultsEntry, -2 = #results+1, -3 = new value, -4 = new key
                    lua_pushstring(lua, "path"); // -1 = "path", -2 = resultsEntry, -3 = #results+1, -4 = new value, -5 = new key
                    path.push_back(lua_tostring(lua, -5));
                    PushLuaStringList(lua, path); // -1 = path, -2 = "path", -3 = resultsEntry, -4 = #results+1, -5 = new value, -6 = new key
                    path.pop_back();
                    lua_rawset(lua, -3); // -1 = resultsEntry, -2 = #results+1, -3 = new value, -4 = new key
                    lua_pushstring(lua, "fn"); // -1 = "fn", -2 = resultsEntry, -3 = #results+1, -4 = new value, -5 = new key
                    lua_pushvalue(lua, -4); // -1 = new value, -2 = "fn", -3 = resultsEntry, -4 = #results+1, -5 = new value, -6 = new key
                    lua_rawset(lua, -3); // -1 = resultsEntry, -2 = #results+1, -3 = new value, -4 = new key
                    lua_pushstring(lua, "table"); // -1 = "table", -2 = resultsEntry, -3 = #results+1, -4 = new value, -5 = new key
                    lua_pushvalue(lua, tableIndex); // -1 = tableIndex, -2 = "table", -3 = resultsEntry, -4 = #results+1, -5 = new value, -6 = new key
                    lua_rawset(lua, -3); // -1 = resultsEntry, -2 = #results+1, -3 = new value, -4 = new key
                    lua_rawset(lua, resultsIndex); // -1 = new value, -2 = new key
                }
            }
            lua_pop(lua, 1); // -1 = old key
        } // (stack empty)
    }

    void FindFunctionsInCompositeLuaTable(lua_State* lua, int tableIndex) {
        if (tableIndex < 0) {
            tableIndex = lua_gettop(lua) + tableIndex + 1;
        }
        std::vector< std::string > path;
        lua_newtable(lua); // -1 = results
        const auto resultsIndex = lua_gettop(lua);
        FindFunctionsInCompositeLuaTable(lua, tableIndex, resultsIndex, path);
    }

    bool DoNotSearch(lua_State* lua, int tableIndex) {
        if (tableIndex < 0) {
            tableIndex = lua_gettop(lua) + tableIndex + 1;
        }
        static std::set< std::vector< std::string > > tablePathsToAvoid{
            {"_G"},
            {"package", "loaded"},
            {"package", "searchers"},
        };
        for (const auto& tablePathToAvoid: tablePathsToAvoid) {
            lua_getglobal(lua, "_G"); // -1 = parent = _G
            for (const auto& name: tablePathToAvoid) {
                lua_pushstring(lua, name.c_str()); // -1 = name, -2 = parent
                lua_rawget(lua, -2); // -1 = parent[name], -2 = parent
                lua_remove(lua, -2); // -1 = parent[name]
            }
            const auto comparison = lua_compare(lua, -1, tableIndex, LUA_OPEQ);
            lua_pop(lua, 1); // (stack empty)
            if (comparison == 1) {
                return true;
            }
        }
        return false;
    }

   /**
     * This contains the private properties of a MoonClock instance.
     */
    struct MoonClock::Impl {
        // Types

        struct CallStackLocation {
            double start = 0.0;
            Path path;
        };
        using CallStack = std::stack< CallStackLocation >;

        // Properties

        CallStack callStack;
        Report report;
        std::shared_ptr< lua_State > lua;
        std::shared_ptr< Timekeeping::Clock > clock;

        /**
         * This is the Lua registry index of the table of instrumented
         * functions.
         */
        int luaRegistryIndex = 0;

        // Lifecycle management

        ~Impl() noexcept = default;
        Impl(const Impl&) = delete;
        Impl(Impl&&) noexcept = default;
        Impl& operator=(const Impl&) = delete;
        Impl& operator=(Impl&&) noexcept = default;

        // Methods

        /**
         * This is the default constructor.
         */
        Impl() = default;

        void StartInstrumentation(
            const std::shared_ptr< lua_State >& lua,
            Instrument before,
            Instrument after,
            void* context
        ) {
            if (luaRegistryIndex != 0) {
                return;
            }
            this->lua = lua;
            const auto beforeWrapper = (Instrument*)lua_newuserdata(lua.get(), sizeof(Instrument)); // -1 = beforeWrapper
            *beforeWrapper = before;
            const auto afterWrapper = (Instrument*)lua_newuserdata(lua.get(), sizeof(Instrument)); // -1 = afterWrapper, -2 = beforeWrapper
            *afterWrapper = after;
            const auto contextWrapper = (void**)lua_newuserdata(lua.get(), sizeof(void*)); // -1 = contextWrapper, -2 = afterWrapper, -3 = beforeWrapper
            *contextWrapper = context;
            const auto instrumentationFactory = [](lua_State* lua){
                const auto closure = [](lua_State* lua){
                    const auto before = *(Instrument*)lua_touserdata(lua, lua_upvalueindex(3));
                    const auto after = *(Instrument*)lua_touserdata(lua, lua_upvalueindex(4));
                    const auto context = *(void**)lua_touserdata(lua, lua_upvalueindex(5));
                    const auto path = ReadLuaStringList(lua, lua_upvalueindex(1));
                    before(lua, context, path);
                    const auto numArgs = lua_gettop(lua);
                    lua_pushvalue(lua, lua_upvalueindex(2));
                    lua_insert(lua, 1);
                    lua_call(lua, numArgs, LUA_MULTRET);
                    after(lua, context, path);
                    return lua_gettop(lua);
                };
                lua_pushvalue(lua, lua_upvalueindex(1));
                lua_pushvalue(lua, lua_upvalueindex(2));
                lua_pushvalue(lua, lua_upvalueindex(3));
                lua_pushcclosure(lua, closure, 5);
                return 1;
            };
            lua_pushcclosure(lua.get(), instrumentationFactory, 3); // -1 = instrumentationFactory
            lua_getglobal(lua.get(), "_G"); // -1 = _G, -2 = instrumentationFactory
            FindFunctionsInCompositeLuaTable(lua.get(), -1); // -1 = functions, -2 = _G, -3 = instrumentationFactory
            lua_remove(lua.get(), -2); // -1 = functions, -2 = instrumentationFactory
            const auto numFunctions = lua_rawlen(lua.get(), -1);
            for (size_t i = 0; i < numFunctions; ++i) {
                // Look up the next function's information.
                lua_pushinteger(lua.get(), i + 1); // -1 = i+1, -2 = functions, -3 = instrumentationFactory
                lua_rawget(lua.get(), -2); // -1 = functions[i+1], -2 = functions, -3 = instrumentationFactory

                // Get the path of the function and construct
                // the instrumented wrapper for it.
                lua_pushstring(lua.get(), "path"); // -1 = "path", -2 = functions[i+1], -3 = functions, -4 = instrumentationFactory
                lua_rawget(lua.get(), -2); // -1 = functions[i+1].path, -2 = functions[i+1], -3 = functions, -4 = instrumentationFactory
                lua_pushvalue(lua.get(), -4); // -1 = instrumentationFactory, -2 = functions[i+1].path, -3 = functions[i+1], -4 = functions, -5 = instrumentationFactory
                lua_pushvalue(lua.get(), -2); // -1 = functions[i+1].path, -2 = instrumentationFactory, -3 = functions[i+1].path, -4 = functions[i+1], -5 = functions, -6 = instrumentationFactory
                lua_pushstring(lua.get(), "fn"); // -1 = "fn", -2 = functions[i+1].path, -3 = instrumentationFactory, -4 = functions[i+1].path, -5 = functions[i+1], -6 = functions, -7 = instrumentationFactory
                lua_rawget(lua.get(), -5); // -1 = functions[i+1].fn, -2 = functions[i+1].path, -3 = instrumentationFactory, -4 = functions[i+1].path, -5 = functions[i+1], -6 = functions, -7 = instrumentationFactory
                lua_call(lua.get(), 2, 1); // -1 = instrumented_fn, -2 = functions[i+1].path, -3 = functions[i+1], -4 = functions, -5 = instrumentationFactory

                // Find the table containing the function and replace
                // the function with its instrumented wrapper.
                lua_pushstring(lua.get(), "table"); // -1 = "table", -2 = instrumented_fn, -3 = functions[i+1].path, -4 = functions[i+1], -5 = functions, -6 = instrumentationFactory
                lua_rawget(lua.get(), -4); // -1 = functions[i+1].table, -2 = instrumented_fn, -3 = functions[i+1].path, -4 = functions[i+1], -5 = functions, -6 = instrumentationFactory
                lua_pushinteger(lua.get(), lua_rawlen(lua.get(), -3)); // -1 = #functions[i+1].path, -2 = functions[i+1].table, -3 = instrumented_fn, -4 = functions[i+1].path, -5 = functions[i+1], -6 = functions, -7 = instrumentationFactory
                lua_rawget(lua.get(), -4); // -1 = functions[i+1].path[#functions[i+1].path], -2 = functions[i+1].table, -3 = instrumented_fn, -4 = functions[i+1].path, -5 = functions[i+1], -6 = functions, -7 = instrumentationFactory
                lua_pushvalue(lua.get(), -3); // -1 = instrumented_fn, -2 = functions[i+1].path[#functions[i+1].path], -3 = functions[i+1].table, -4 = instrumented_fn, -5 = functions[i+1].path, -6 = functions[i+1], -7 = functions, -8 = instrumentationFactory
                lua_rawset(lua.get(), -3);// -1 = functions[i+1].table, -2 = instrumented_fn, -3 = functions[i+1].path, -4 = functions[i+1], -5 = functions, -6 = instrumentationFactory
                lua_pop(lua.get(), 4); // -1 = functions, -2 = instrumentationFactory
            }
            luaRegistryIndex = luaL_ref(lua.get(), LUA_REGISTRYINDEX); // -1 = instrumentationFactory
            lua_pop(lua.get(), 1); // (stack empty)
        }

        void StopInstrumentation() {
            if (luaRegistryIndex == 0) {
                return;
            }
            lua_rawgeti(lua.get(), LUA_REGISTRYINDEX, luaRegistryIndex); // -1 = functions
            const auto numFunctions = lua_rawlen(lua.get(), -1);
            for (size_t i = 0; i < numFunctions; ++i) {
                // Look up the next function's information.
                lua_pushinteger(lua.get(), i + 1); // -1 = i+1, -2 = functions
                lua_rawget(lua.get(), -2); // -1 = functions[i+1], -2 = functions

                // Get the path of the function to know its name.
                lua_pushstring(lua.get(), "path"); // -1 = "path", -2 = functions[i+1], -3 = functions
                lua_rawget(lua.get(), -2); // -1 = functions[i+1].path, -2 = functions[i+1], -3 = functions
                lua_pushinteger(lua.get(), lua_rawlen(lua.get(), -1)); // -1 = #functions[i+1].path, -2 = functions[i+1].path, -3 = functions[i+1], -4 = functions
                lua_rawget(lua.get(), -2); // -1 = functions[i+1].path[#functions[i+1].path], -2 = functions[i+1].path, -3 = functions[i+1], -4 = functions
                lua_remove(lua.get(), -2); // -1 = functions[i+1].path[#functions[i+1].path], -2 = functions[i+1], -3 = functions

                // Find the function and the table containing it, and reinstall
                // the original function without its instrumented wrapper.
                lua_pushstring(lua.get(), "fn"); // -1 = "fn", -2 = functions[i+1].path[#functions[i+1].path], -3 = functions[i+1], -4 = functions
                lua_rawget(lua.get(), -3); // -1 = functions[i+1].fn, -2 = functions[i+1].path[#functions[i+1].path], -3 = functions[i+1], -4 = functions
                lua_pushstring(lua.get(), "table"); // -1 = "table", -2 = functions[i+1].fn, -3 = functions[i+1].path[#functions[i+1].path], -4 = functions[i+1], -5 = functions
                lua_rawget(lua.get(), -4); // -1 = functions[i+1].table, -2 = functions[i+1].fn, -3 = functions[i+1].path[#functions[i+1].path], -4 = functions[i+1], -5 = functions
                lua_insert(lua.get(), -3); // -1 = functions[i+1].fn, -2 = functions[i+1].path[#functions[i+1].path], -3 = functions[i+1].table, -4 = functions[i+1], -5 = functions
                lua_rawset(lua.get(), -3); // -1 = functions[i+1].table, -2 = functions[i+1], -3 = functions
                lua_pop(lua.get(), 2); // -1 = functions
            }
            luaL_unref(lua.get(), LUA_REGISTRYINDEX, luaRegistryIndex);
            luaRegistryIndex = 0;
            lua.reset();
        }
    };

    MoonClock::~MoonClock() noexcept = default;

    MoonClock::MoonClock(MoonClock&& other) noexcept
        : impl_(std::move(other.impl_))
    {
    }

    MoonClock& MoonClock::operator=(MoonClock&& other) noexcept {
        if (this != &other) {
            impl_ = std::move(other.impl_);
        }
        return *this;
    }

    MoonClock::MoonClock()
        : impl_(new Impl())
    {
    }

    void MoonClock::DefaultBeforeInstrument(lua_State* lua, void* context, const Path& path) {
        const auto self = (MoonClock::Impl*)context;
        if (!self->callStack.empty()) {
            const auto& callerCallStackEntry = self->callStack.top();
            auto& callerFunctionInfo = self->report.functionInfo[callerCallStackEntry.path];
            auto& calleeCallInfo = callerFunctionInfo.calls[path];
            ++calleeCallInfo.numCalls;
        }
        auto& functionInfo = self->report.functionInfo[path];
        ++functionInfo.numCalls;
        Impl::CallStackLocation call;
        call.start = self->clock->GetCurrentTime();
        call.path = path;
        self->callStack.push(std::move(call));
    }

    void MoonClock::DefaultAfterInstrument(lua_State* lua, void* context, const Path& path) {
        const auto self = (MoonClock::Impl*)context;
        const auto& call = self->callStack.top();
        const auto finish = self->clock->GetCurrentTime();
        auto& functionInfo = self->report.functionInfo[path];
        const auto total = finish - call.start;
        functionInfo.minTime = std::min(functionInfo.minTime, total);
        functionInfo.totalTime += total;
        functionInfo.maxTime = std::max(functionInfo.maxTime, total);
        self->callStack.pop();
        if (!self->callStack.empty()) {
            const auto& callerCallStackEntry = self->callStack.top();
            auto& callerFunctionInfo = self->report.functionInfo[callerCallStackEntry.path];
            auto& calleeCallInfo = callerFunctionInfo.calls[path];
            calleeCallInfo.totalTime += total;
        }
    }

    void* MoonClock::GetDefaultContext() {
        return impl_.get();
    }

    void MoonClock::SetClock(std::shared_ptr< Timekeeping::Clock > clock) {
        impl_->clock = std::move(clock);
    }

    void MoonClock::StartInstrumentation(
        const std::shared_ptr< lua_State >& lua,
        Instrument before,
        Instrument after,
        void* context
    ) {
        if (context == nullptr) {
            context = GetDefaultContext();
        }
        impl_->StartInstrumentation(lua, before, after, context);
    }

    void MoonClock::StopInstrumentation() {
        impl_->StopInstrumentation();
    }

    auto MoonClock::GenerateReport() const -> Report {
        return impl_->report;
    }

}
