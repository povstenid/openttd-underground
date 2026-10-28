/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file test_script_json.cpp Tests for shared script JSON helpers. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"
#include "../3rdparty/fmt/format.h"

#include "../game/game_instance.hpp"
#include "../script/api/script_object.hpp"
#include "../script/script_json.hpp"
#include "../script/squirrel.hpp"

#include <squirrel.h>

#include "../safeguards.h"

class TestScriptJSONController {
public:
	GameInstance game{};
	ScriptObject::ActiveInstance active{game};

	Squirrel engine{"test-json"};
	ScriptAllocatorScope scope{&engine};
};

static std::optional<std::string> TestConvertToJSON(std::string_view squirrel)
{
	auto vm = sq_open(1024);
	std::string buffer = fmt::format("return {}", squirrel);

	sq_pushroottable(vm);
	sq_pushstring(vm, "DummyClass");
	sq_newclass(vm, SQFalse);
	sq_newslot(vm, -3, SQFalse);
	sq_pop(vm, 1);

	if (SQ_FAILED(sq_compilebuffer(vm, buffer.data(), static_cast<SQInteger>(buffer.size()), "test", SQTrue))) {
		sq_close(vm);
		FAIL("Unable to compile test squirrel snippet");
	}
	sq_pushroottable(vm);
	if (SQ_FAILED(sq_call(vm, 1, SQTrue, SQTrue))) {
		sq_close(vm);
		FAIL("Unable to execute test squirrel snippet");
	}

	nlohmann::json json;
	if (!ScriptConvertToJSON(json, vm, -1)) {
		sq_close(vm);
		return std::nullopt;
	}

	sq_close(vm);
	return json.dump();
}

TEST_CASE("ScriptConvertToJSON converts supported values")
{
	TestScriptJSONController controller;

	CHECK(TestConvertToJSON(R"sq({ test = null })sq") == R"json({"test":null})json");
	CHECK(TestConvertToJSON(R"sq({ test = 1 })sq") == R"json({"test":1})json");
	CHECK(TestConvertToJSON(R"sq({ test = "a" })sq") == R"json({"test":"a"})json");
	CHECK(TestConvertToJSON(R"sq({ test = [ 1, 2, 3 ] })sq") == R"json({"test":[1,2,3]})json");
	CHECK(TestConvertToJSON(R"sq({ test = { nested = true, list = [] } })sq") == R"json({"test":{"list":[],"nested":true}})json");
}

TEST_CASE("ScriptConvertToJSON rejects unsupported values")
{
	TestScriptJSONController controller;

	CHECK(TestConvertToJSON(R"sq({ test = DummyClass })sq") == std::nullopt);
	CHECK(TestConvertToJSON(R"sq({ test = [ DummyClass ] })sq") == std::nullopt);
}

TEST_CASE("ScriptValidateTestReport validates schema")
{
	std::string error;

	nlohmann::json valid = {
		{"suite", "vehicle_runtime"},
		{"fixture", nullptr},
		{"seed", 12345},
		{"tick", 42},
		{"status", "pass"},
		{"message", "all checks passed"},
		{"checks", nlohmann::json::array({
			{
				{"id", "vehicle_started"},
				{"status", "pass"},
				{"expected", "true"},
				{"actual", "true"},
				{"message", ""}
			}
		})},
		{"metrics", {
			{"vehicle_id", 1},
			{"speed", 55}
		}}
	};

	CHECK(ScriptValidateTestReport(valid, error));
	CHECK(error.empty());
}

TEST_CASE("ScriptValidateTestReport rejects invalid schema")
{
	std::string error;

	nlohmann::json missing_status = {
		{"suite", "vehicle_runtime"},
		{"fixture", nullptr},
		{"seed", 12345},
		{"tick", 42},
		{"message", "oops"},
		{"checks", nlohmann::json::array()},
		{"metrics", nlohmann::json::object()}
	};
	CHECK_FALSE(ScriptValidateTestReport(missing_status, error));
	CHECK_FALSE(error.empty());

	nlohmann::json invalid_check = {
		{"suite", "vehicle_runtime"},
		{"fixture", nullptr},
		{"seed", 12345},
		{"tick", 42},
		{"status", "pass"},
		{"message", "oops"},
		{"checks", nlohmann::json::array({
			{
				{"id", "vehicle_started"},
				{"status", "maybe"},
				{"expected", "true"},
				{"actual", "true"},
				{"message", ""}
			}
		})},
		{"metrics", nlohmann::json::object()}
	};
	error.clear();
	CHECK_FALSE(ScriptValidateTestReport(invalid_check, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("ScriptValidateTestReport accepts large payloads")
{
	std::string error;
	std::string large_message(4096, 'x');

	nlohmann::json valid = {
		{"suite", "large_payload"},
		{"fixture", "fixture.sav"},
		{"seed", nullptr},
		{"tick", 7},
		{"status", "error"},
		{"message", large_message},
		{"checks", nlohmann::json::array({
			{
				{"id", "payload"},
				{"status", "fail"},
				{"expected", "small"},
				{"actual", large_message},
				{"message", large_message}
			}
		})},
		{"metrics", {
			{"payload_size", static_cast<int>(large_message.size())}
		}}
	};

	CHECK(ScriptValidateTestReport(valid, error));
	CHECK(error.empty());
}
