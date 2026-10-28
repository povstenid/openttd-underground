/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file script_json.hpp Shared helpers for converting Squirrel data to JSON. */

#ifndef SCRIPT_JSON_HPP
#define SCRIPT_JSON_HPP

#include "../3rdparty/nlohmann/json.hpp"

#include <squirrel.h>

bool ScriptConvertToJSON(nlohmann::json &json, HSQUIRRELVM vm, SQInteger index, int depth = 0);
bool ScriptValidateTestReport(const nlohmann::json &json, std::string &error);

#endif /* SCRIPT_JSON_HPP */
