/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file script_test.hpp Structured scenario test reporting for scripts. */

#ifndef SCRIPT_TEST_HPP
#define SCRIPT_TEST_HPP

#include "script_object.hpp"

/**
 * Class that handles structured reporting for scenario tests.
 * @api ai game
 */
class ScriptTest : public ScriptObject {
public:
#ifndef DOXYGEN_API
	static SQInteger Report(HSQUIRRELVM vm);
	static SQInteger SaveGame(HSQUIRRELVM vm);
#else
	/**
	 * Emit a single structured scenario test report.
	 * The table is converted to compact JSON and written to the test output as
	 * a single tagged line prefixed with `OTTD_TEST_RESULT`.
	 * @param data Structured report data.
	 * @return True if and only if the report was successfully serialized and
	 *   validated.
	 */
	static bool Report(table data);

	/**
	 * Save the current game to the save directory using the given filename.
	 * Scenario tests can use this to persist the final world for manual
	 * inspection after the test finishes.
	 * @param filename Savegame filename. Directory separators are rejected.
	 *   The `.sav` extension is added automatically when omitted.
	 * @return True if and only if the savegame was written successfully.
	 */
	static bool SaveGame(const char *filename);
#endif /* DOXYGEN_API */
};

#endif /* SCRIPT_TEST_HPP */
