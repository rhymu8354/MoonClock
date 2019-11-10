#pragma once

/**
 * @file MoonClock.hpp
 *
 * This module declares the MoonClock::MoonClock class.
 *
 * © 2019 by Richard Walters
 */

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <Timekeeping/Clock.hpp>
#include <vector>

/**
 * Forward-declare the Lua interpreter state structure in order to correctly
 * specify the interface without requiring the user to know the implementation.
 */
struct lua_State;

namespace MoonClock {

    /**
     * This type is used to represent the path to a Lua function from
     * a reference point such as the global Lua variables.
     */
    using Path = std::vector< std::string >;

    /**
     * This is the type of function which can be called either before or
     * after an instrumented Lua function, to perform the instrumentation.
     *
     * @param[in,out] lua
     *     This points to the Lua interpreter's state.
     *
     * @param[in,out] context
     *     This points to context information shared by the instrumentation.
     *
     * @param[in] path
     *     This represents the path to the Lua function being instrumented,
     *     from a reference point such as the global Lua variables.
     */
    using Instrument = void (*)(lua_State* lua, void* context, const Path& path);

    /**
     * This collects information about other Lua functions called from a given
     * Lua function.
     */
    struct CallsInformation {
        /**
         * This is the number of times the function was called by the caller.
         */
        size_t numCalls = 0;

        /**
         * This is the total amount of time elapsed, in seconds, during
         * all calls to this function from the caller.
         */
        double totalTime = 0.0;

        /**
         * This is the equality operator.
         *
         * @param[in] other
         *     This is the other value to which to compare the subject.
         *
         * @return
         *     An indication of whether or not the two values are equal
         *     is returned.
         */
        bool operator==(const CallsInformation& other) const;
    };

    /**
     * This is a support function for Google Test to print out
     * values of the CallsInformation structure.
     *
     * @param[in] CallsInformation
     *     This is the function information value to print.
     *
     * @param[in] os
     *     This points to the stream to which to print the
     *     function information value.
     */
    void PrintTo(
        const CallsInformation& callsInformation,
        std::ostream* os
    );

    /**
     * This collects information about a Lua function called.
     */
    struct FunctionInformation {
        /**
         * This is the number of times the function was called.
         */
        size_t numCalls = 0;

        /**
         * This is the amount of time elapsed, in seconds, during the call
         * which took the least amount of time.
         */
        double minTime = std::numeric_limits< decltype(minTime) >::max();

        /**
         * This is the total amount of time elapsed, in seconds, during
         * all calls to this function.
         */
        double totalTime = 0.0;

        /**
         * This is the amount of time elapsed, in seconds, during the call
         * which took the most amount of time.
         */
        double maxTime = 0.0;

        /**
         * This holds information about all the Lua functions called
         * from this function.
         */
        std::map< Path, CallsInformation > calls;

        /**
         * This is the equality operator.
         *
         * @param[in] other
         *     This is the other value to which to compare the subject.
         *
         * @return
         *     An indication of whether or not the two values are equal
         *     is returned.
         */
        bool operator==(const FunctionInformation& other) const;
    };

    /**
     * This is a support function for Google Test to print out
     * values of the FunctionInformation structure.
     *
     * @param[in] functionInformation
     *     This is the function information value to print.
     *
     * @param[in] os
     *     This points to the stream to which to print the
     *     function information value.
     */
    void PrintTo(
        const FunctionInformation& functionInformation,
        std::ostream* os
    );

    /**
     * This holds all information collected by the default instruments,
     * if they are used.
     */
    struct Report {
        /**
         * This holds information about each Lua function called.
         */
        std::map< Path, FunctionInformation > functionInfo;
    };

    /**
     * Search a Lua composite (table or value supporting the __pairs, __index,
     * and __newindex metamethods) hierarchy for Lua functions.  Push onto the
     * Lua stack a list of tables, each containing the following, one for every
     * function found within the hierarchy:
     * - parent: the composite containing the function (could be nested)
     * - path: list of table keys to use to locate the function within
     *         the table hierarchy
     * - fn: the function itself
     *
     * @param[in] lua
     *     This is the state of the Lua interpreter to use.
     *
     * @param[in] compositeIndex
     *     This is the index into the Lua stack where the composite to search
     *     can be found.
     */
    void FindFunctionsInComposite(lua_State* lua, int compositeIndex);

    /**
     * Determine whether or not the given composite should be searched for
     * functions to instrument, if encountered as nested composites.
     *
     * Some composites, such as _G and package.loaded, should not be searched
     * because they are referenced from other composites.  Other composites,
     * such as package.searchers, should not be searched because modifying
     * functions in them alters the way Lua itself functions.
     *
     * @param[in] lua
     *     This is the state of the Lua interpreter to use.
     *
     * @param[in] compositeIndex
     *     This is the index into the Lua stack where the composite to search
     *     can be found.
     *
     * @return
     *     If the given composite should not be searched for functions to
     *     instrument, true is returned.  Otherwise, false is returned.
     */
    bool DoNotSearch(lua_State* lua, int compositeIndex);

    /**
     * This class represents a suite of tools used to measure the performance
     * of Lua functions.
     */
    class MoonClock {
        // Lifecycle management
    public:
        ~MoonClock() noexcept;
        MoonClock(const MoonClock&) = delete;
        MoonClock(MoonClock&&) noexcept;
        MoonClock& operator=(const MoonClock&) = delete;
        MoonClock& operator=(MoonClock&&) noexcept;

        // Public methods
    public:
        /**
         * This is the default constructor for the class.
         */
        MoonClock();

        /**
         * This is the default instrumentation to apply at the beginning
         * of each Lua function call.
         *
         * @param[in,out] lua
         *     This points to the Lua interpreter's state.
         *
         * @param[in,out] context
         *     This points to context information shared by the
         *     instrumentation, which must be the value returned by the
         *     GetDefaultContext method.
         *
         * @param[in] path
         *     This represents the path to the Lua function being instrumented,
         *     from a reference point such as the global Lua variables.
         */
        static void DefaultBeforeInstrument(lua_State* lua, void* context, const Path& path);

        /**
         * This is the default instrumentation to apply at the end
         * of each Lua function call.
         *
         * @param[in,out] lua
         *     This points to the Lua interpreter's state.
         *
         * @param[in,out] context
         *     This points to context information shared by the
         *     instrumentation, which must be the value returned by the
         *     GetDefaultContext method.
         *
         * @param[in] path
         *     This represents the path to the Lua function being instrumented,
         *     from a reference point such as the global Lua variables.
         */
        static void DefaultAfterInstrument(lua_State* lua, void* context, const Path& path);

        /**
         * Return the context to use when using the default
         * before/after instruments.
         *
         * @return
         *     The context ot use when using the default before/after
         *     instruments is returned.
         */
        void* GetDefaultContext();

        /**
         * Set the object the default instruments should use
         * to measure real time.  It must be called before
         * StartInstrumentation if the default instruments are used.
         *
         * @param[in] clock
         *     This is the object the default instruments should use
         *     to measure real time.
         */
        void SetClock(std::shared_ptr< Timekeeping::Clock > clock);

        /**
         * Attach instruments to all Lua functions.  The default instruments
         * collect the information returned by GenerateReport.  Any Lua
         * functions called after this function returns, and before the
         * StopInstrumentation function is called, will invoke the
         * given instrumentation.
         *
         * @note
         *     If the default instrumentation is used, SetClock must be
         *     called first to provide the means of measuring real time.
         *
         * @parma[in,out] lua
         *     This points to the Lua interpreter's state.
         *
         * @param[in] before
         *     This is the instrumentation to apply at the beginning
         *     of each Lua function call.
         *
         * @param[in] after
         *     This is the instrumentation to apply at the end
         *     of each Lua function call.
         *
         * @param[in] context
         *     This is the pointer to provide to the before and after
         *     instruments whenever they are called.  If nullptr,
         *     the value returned by GetDefaultContext is returned.
         */
        void StartInstrumentation(
            const std::shared_ptr< lua_State >& lua,
            Instrument before = DefaultBeforeInstrument,
            Instrument after = DefaultAfterInstrument,
            void* context = nullptr
        );

        /**
         * Remove any instrumentation applied by the last StartInstrumentation
         * function call.
         */
        void StopInstrumentation();

        /**
         * Return a copy of the information collected by the default
         * instrumentation, if it was used.
         *
         * @note
         *     This function returns no useful information if the default
         *     instrumentation was not used with the StartInstrumentation
         *     call, or if StartInstrumentation and StopInstrumentation were
         *     not called.
         *
         * @return
         *     A copy of the information collected by the default
         *     instrumentation is returned.
         */
        Report GenerateReport() const;

        // Private properties
    private:
        /**
         * This is the type of structure that contains the private
         * properties of the instance.  It is defined in the implementation
         * and declared here to ensure that it is scoped inside the class.
         */
        struct Impl;

        /**
         * This contains the private properties of the instance.
         */
        std::unique_ptr< Impl > impl_;
    };

}
