#pragma once

/**
 * @file MoonClock.hpp
 *
 * This module declares the MoonClock::MoonClock class.
 *
 * © 2019 by Richard Walters
 */

#include <memory>
#include <string>
#include <vector>

/**
 * Forward-declare the Lua interpreter state structure in order to correctly
 * specify the interface without requiring the user to know the implementation.
 */
struct lua_State;

namespace MoonClock {

    std::vector< std::string > EnumerateLuaFunctions(lua_State* lua);

    /**
     * Search a Lua table's hierarchy for Lua functions.  Add to a result list
     * table, each containing the following, one for every function found
     * within the table hierarchy:
     * - table: the table containing the function (could be nested)
     * - path: list of table keys to use to locate the function within
     *         the table hierarchy
     * - fn: the function itself
     *
     * @param[in] lua
     *     This is the state of the Lua interpreter to use.
     *
     * @param[in] tableIndex
     *     This is the index into the Lua stack where the table to search
     *     can be found.
     *
     * @param[in] resultsIndex
     *     This is the index into the Lua stack where the table to which
     *     results should be added can be found.
     *
     * @param[in,out] path
     *     This is used to keep track of the current path through the
     *     table hierarchy.
     */
    void FindFunctionsInCompositeLuaTable(
        lua_State* lua,
        int tableIndex,
        int resultsIndex,
        std::vector< std::string >& path
    );

    /**
     * Search a Lua table's hierarchy for Lua functions.  Push onto the Lua
     * stack a list of tables, each containing the following, one for every
     * function found within the table hierarchy:
     * - table: the table containing the function (could be nested)
     * - path: list of table keys to use to locate the function within
     *         the table hierarchy
     * - fn: the function itself
     *
     * @param[in] lua
     *     This is the state of the Lua interpreter to use.
     *
     * @param[in] tableIndex
     *     This is the index into the Lua stack where the table to search
     *     can be found.
     */
    void FindFunctionsInCompositeLuaTable(lua_State* lua, int tableIndex);

    /**
     * Determine whether or not the given table should be searched for
     * functions to instrument, if encountered as nested tables.
     *
     * Some tables, such as _G and package.loaded, should not be searched
     * because they are referenced from other tables.  Other tables,
     * such as package.searchers, should not be searched because modifying
     * functions in them alters the way Lua itself functions.
     *
     * @param[in] lua
     *     This is the state of the Lua interpreter to use.
     *
     * @param[in] tableIndex
     *     This is the index into the Lua stack where the table to search
     *     can be found.
     *
     * @return
     *     If the given table should not be searched for functions to
     *     instrument, true is returned.  Otherwise, false is returned.
     */
    bool DoNotSearch(lua_State* lua, int tableIndex);

    /**
     * This class represents a suite of tools used to measure the performance
     * of Lua functions.
     */
    class MoonClock {
        // Types
    public:
        struct Report {
            std::vector< std::string > lines;
        };

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

        void StartInstrumentation(std::shared_ptr< lua_State > lua);

        void StopInstrumentation();

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
