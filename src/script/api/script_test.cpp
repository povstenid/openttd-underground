/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file script_test.cpp Implementation of ScriptTest. */

#include "../../stdafx.h"

#include "script_test.hpp"
#include "script_log.hpp"
#include "../script_json.hpp"

#include "../../3rdparty/fmt/format.h"
#include "../../fileio_type.h"
#include "../../saveload/saveload.h"
#include "../../string_func.h"

#include "../../safeguards.h"

/* static */ SQInteger ScriptTest::Report(HSQUIRRELVM vm)
{
	if (sq_gettop(vm) - 1 != 1) return sq_throwerror(vm, "wrong number of parameters");

	if (sq_gettype(vm, 2) != OT_TABLE) {
		return sq_throwerror(vm, "ScriptTest::Report requires a table as first parameter. No data sent.");
	}

	nlohmann::json json;
	if (!ScriptConvertToJSON(json, vm, -1)) {
		sq_pushbool(vm, SQFalse);
		return 1;
	}

	std::string error;
	if (!ScriptValidateTestReport(json, error)) {
		ScriptLog::Error(error);
		sq_pushbool(vm, SQFalse);
		return 1;
	}

	fmt::print(stderr, "OTTD_TEST_RESULT {}\n", json.dump());
	fflush(stderr);

	sq_pushbool(vm, SQTrue);
	return 1;
}

/* static */ SQInteger ScriptTest::SaveGame(HSQUIRRELVM vm)
{
	if (sq_gettop(vm) - 1 != 1) return sq_throwerror(vm, "wrong number of parameters");

	if (sq_gettype(vm, 2) != OT_STRING) {
		return sq_throwerror(vm, "ScriptTest::SaveGame requires a filename string.");
	}

	std::string_view filename_view;
	sq_getstring(vm, 2, filename_view);

	std::string filename = StrMakeValid(filename_view);
	if (filename.empty()) {
		ScriptLog::Error("ScriptTest::SaveGame requires a non-empty filename.");
		sq_pushbool(vm, SQFalse);
		return 1;
	}

	if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos || filename.find(':') != std::string::npos || filename.find("..") != std::string::npos) {
		ScriptLog::Error("ScriptTest::SaveGame only accepts a plain filename without directories.");
		sq_pushbool(vm, SQFalse);
		return 1;
	}

	if (!StrEndsWithIgnoreCase(filename, ".sav")) filename += ".sav";

	SaveOrLoadResult result = SaveOrLoad(filename, SLO_SAVE, DFT_GAME_FILE, SAVE_DIR, false);
	sq_pushbool(vm, result == SL_OK ? SQTrue : SQFalse);
	return 1;
}
