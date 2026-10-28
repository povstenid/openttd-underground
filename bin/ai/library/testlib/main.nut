class TestLib {
	_suite = null;
	_fixture = null;
	_seed = null;
	_checks = null;
	_metrics = null;
	_finished = false;

	constructor(suite, fixture = null, seed = null)
	{
		this._suite = suite;
		this._fixture = fixture;
		this._seed = seed;
		this._checks = [];
		this._metrics = {};
	}

	function _stringify(value)
	{
		if (value == null) return "null";
		if (typeof value == "bool") return value ? "true" : "false";
		return value.tostring();
	}

	function _tick()
	{
		return AIController.GetTick();
	}

	function _append_check(id, ok, expected, actual, message)
	{
		this._checks.append({
			id = id,
			status = ok ? "pass" : "fail",
			expected = this._stringify(expected),
			actual = this._stringify(actual),
			message = message == null ? "" : this._stringify(message)
		});
	}

	function check_true(id, cond, message = "")
	{
		this._append_check(id, cond, true, cond, message);
		return cond;
	}

	function check_eq(id, expected, actual, message = "")
	{
		local ok = expected == actual;
		this._append_check(id, ok, expected, actual, message);
		return ok;
	}

	function check_range(id, min_value, max_value, actual, message = "")
	{
		local ok = actual >= min_value && actual <= max_value;
		this._append_check(id, ok, min_value + ".." + max_value, actual, message);
		return ok;
	}

	function metric(name, value)
	{
		this._metrics[name] <- value;
	}

	function finish(message = null)
	{
		if (this._finished) throw "TestLib.finish() can only be called once";
		this._finished = true;

		local failed = false;
		foreach (check in this._checks) {
			if (check.status != "pass") {
				failed = true;
				break;
			}
		}

		local status = failed ? "fail" : "pass";
		local final_message = message;
		if (final_message == null) {
			final_message = failed ? "one or more checks failed" : "all checks passed";
		}

		local report = {
			suite = this._suite,
			fixture = this._fixture,
			seed = this._seed,
			tick = this._tick(),
			status = status,
			message = final_message,
			checks = this._checks,
			metrics = this._metrics
		};

		if (!ScriptTest.Report(report)) {
			throw "Unable to emit structured scenario report";
		}

		return !failed;
	}
}
