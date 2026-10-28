/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file script_json.cpp Shared helpers for converting Squirrel data to JSON. */

#include "../stdafx.h"

#include "script_json.hpp"
#include "script_instance.hpp"
#include "api/script_log.hpp"

#include "../safeguards.h"

bool ScriptConvertToJSON(nlohmann::json &json, HSQUIRRELVM vm, SQInteger index, int depth)
{
	if (depth == SQUIRREL_MAX_DEPTH) {
		ScriptLog::Error("JSON parameters can only be nested to 25 deep. No data sent.");
		return false;
	}

	switch (sq_gettype(vm, index)) {
		case OT_INTEGER: {
			SQInteger res;
			sq_getinteger(vm, index, &res);
			json = res;
			return true;
		}

		case OT_STRING: {
			std::string_view view;
			sq_getstring(vm, index, view);
			json = view;
			return true;
		}

		case OT_ARRAY: {
			json = nlohmann::json::array();

			sq_pushnull(vm);
			while (SQ_SUCCEEDED(sq_next(vm, index - 1))) {
				nlohmann::json tmp;

				bool res = ScriptConvertToJSON(tmp, vm, -1, depth + 1);
				sq_pop(vm, 2);
				if (!res) {
					sq_pop(vm, 1);
					return false;
				}

				json.push_back(std::move(tmp));
			}
			sq_pop(vm, 1);
			return true;
		}

		case OT_TABLE: {
			json = nlohmann::json::object();

			sq_pushnull(vm);
			while (SQ_SUCCEEDED(sq_next(vm, index - 1))) {
				sq_tostring(vm, -2);
				std::string_view view;
				sq_getstring(vm, -1, view);
				std::string key{view};
				sq_pop(vm, 1);

				nlohmann::json value;
				bool res = ScriptConvertToJSON(value, vm, -1, depth + 1);
				sq_pop(vm, 2);
				if (!res) {
					sq_pop(vm, 1);
					return false;
				}

				json[std::move(key)] = std::move(value);
			}
			sq_pop(vm, 1);
			return true;
		}

		case OT_BOOL: {
			SQBool res;
			sq_getbool(vm, index, &res);
			json = res ? true : false;
			return true;
		}

		case OT_NULL:
			json = nullptr;
			return true;

		default:
			ScriptLog::Error("You tried to send an unsupported type. No data sent.");
			return false;
	}
}

static bool ValidateRequiredString(const nlohmann::json &json, std::string_view key, std::string &error)
{
	auto it = json.find(key);
	if (it == json.end() || !it->is_string()) {
		error = fmt::format("Missing or invalid '{}' field.", key);
		return false;
	}

	return true;
}

bool ScriptValidateTestReport(const nlohmann::json &json, std::string &error)
{
	if (!json.is_object()) {
		error = "Scenario report must be a JSON object.";
		return false;
	}

	if (!ValidateRequiredString(json, "suite", error)) return false;
	if (!ValidateRequiredString(json, "status", error)) return false;
	if (!ValidateRequiredString(json, "message", error)) return false;

	auto status_it = json.find("status");
	if (*status_it != "pass" && *status_it != "fail" && *status_it != "error") {
		error = "Scenario report 'status' must be one of: pass, fail, error.";
		return false;
	}

	auto tick_it = json.find("tick");
	if (tick_it == json.end() || !tick_it->is_number_integer()) {
		error = "Missing or invalid 'tick' field.";
		return false;
	}

	auto fixture_it = json.find("fixture");
	if (fixture_it == json.end() || (!fixture_it->is_null() && !fixture_it->is_string())) {
		error = "Missing or invalid 'fixture' field.";
		return false;
	}

	auto seed_it = json.find("seed");
	if (seed_it == json.end() || (!seed_it->is_null() && !seed_it->is_number_integer())) {
		error = "Missing or invalid 'seed' field.";
		return false;
	}

	auto checks_it = json.find("checks");
	if (checks_it == json.end() || !checks_it->is_array()) {
		error = "Missing or invalid 'checks' field.";
		return false;
	}

	for (const auto &check : *checks_it) {
		if (!check.is_object()) {
			error = "Every scenario check must be an object.";
			return false;
		}

		if (!ValidateRequiredString(check, "id", error)) return false;
		if (!ValidateRequiredString(check, "status", error)) return false;
		if (!ValidateRequiredString(check, "expected", error)) return false;
		if (!ValidateRequiredString(check, "actual", error)) return false;
		if (!ValidateRequiredString(check, "message", error)) return false;

		auto check_status = check.find("status");
		if (*check_status != "pass" && *check_status != "fail") {
			error = "Scenario check 'status' must be either pass or fail.";
			return false;
		}
	}

	auto metrics_it = json.find("metrics");
	if (metrics_it == json.end() || !metrics_it->is_object()) {
		error = "Missing or invalid 'metrics' field.";
		return false;
	}

	return true;
}
