/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file script_admin.cpp Implementation of ScriptAdmin. */

#include "../../stdafx.h"
#include "script_admin.hpp"
#include "script_log.hpp"
#include "../../network/network_admin.h"
#include "../script_instance.hpp"
#include "../script_json.hpp"
#include "../../string_func.h"
#include "../../3rdparty/nlohmann/json.hpp"

#include "../../safeguards.h"

bool ScriptAdminMakeJSON(nlohmann::json &json, HSQUIRRELVM vm, SQInteger index, int depth = 0)
{
	return ScriptConvertToJSON(json, vm, index, depth);
}

/* static */ SQInteger ScriptAdmin::Send(HSQUIRRELVM vm)
{
	if (sq_gettop(vm) - 1 != 1) return sq_throwerror(vm, "wrong number of parameters");

	if (sq_gettype(vm, 2) != OT_TABLE) {
		return sq_throwerror(vm, "ScriptAdmin::Send requires a table as first parameter. No data sent.");
	}

	nlohmann::json json;
	if (!ScriptAdminMakeJSON(json, vm, -1)) {
		sq_pushinteger(vm, 0);
		return 1;
	}

	NetworkAdminGameScript(json.dump());

	sq_pushinteger(vm, 1);
	return 1;
}
