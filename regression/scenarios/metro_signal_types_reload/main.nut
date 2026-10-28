/**
 * metro_signal_types_reload
 *
 * The fixture is a savegame written by this script (see below): every
 * underground signal type in both variants, plus a turned and a converted
 * signal. After loading, types, variants and directions must be unchanged.
 * The line is found by the sign "metro_signal_origin" at its origin.
 *
 * A loaded game restarts the game script stored in it, so this one script has
 * two phases: without the origin sign it builds the line like
 * metro_signal_types_save and writes metro_signal_types_reload_fixture.sav
 * (copy it from the save directory to fixture.sav to regenerate the fixture);
 * with the sign (the fixture loaded) it checks the signals.
 */
class MetroSignalTypesReload extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local origin = null;
		local signs = GSSignList();
		for (local s = signs.Begin(); !signs.IsEnd(); s = signs.Next()) {
			if (GSSign.GetName(s) == "metro_signal_origin") origin = GSSign.GetLocation(s);
		}
		if (origin == null) {
			this.BuildFixture(MetroTest);
		} else {
			this.Verify(MetroTest, origin);
		}
	}

	function BuildFixture(MetroTest)
	{
		local report = MetroTest("metro_signal_types_reload", null, 510096);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		try {
			local area = build.find_buildable_rectangle(34, 4);
			report.check_true("build_area_found", area != null, "area");
			local company = companies.ensure_company();
			local cm = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			if (area != null && cm != null) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 2;
				local d = -1;
				local track = GSRail.RAILTRACK_NE_SW;
				report.check_true("line", metro.build_underground_x(x + 1, x + 30, y, d), "line");

				local types = [GSRail.SIGNALTYPE_NORMAL, GSRail.SIGNALTYPE_ENTRY, GSRail.SIGNALTYPE_EXIT,
					GSRail.SIGNALTYPE_COMBO, GSRail.SIGNALTYPE_PBS, GSRail.SIGNALTYPE_PBS_ONEWAY];
				local variants = [GSMetro.SIGNAL_VARIANT_ELECTRIC, GSMetro.SIGNAL_VARIANT_SEMAPHORE];
				foreach (ti, type in types) {
					foreach (vi, variant in variants) {
						local tile = build.make_tile(x + 2 + 2 * (ti * 2 + vi), y);
						local key = "t" + type + "v" + variant;
						report.check_true(key + "_built", GSMetro.BuildUndergroundSignal(tile, track, d, type, variant), "build");
						report.check_eq(key + "_type", type, GSMetro.GetUndergroundSignalType(tile, track, d), "type");
						report.check_eq(key + "_variant", variant, GSMetro.GetUndergroundSignalVariant(tile, track, d), "variant");
						local pbs = type == GSRail.SIGNALTYPE_PBS || type == GSRail.SIGNALTYPE_PBS_ONEWAY;
						report.check_eq(key + "_twoway", !pbs, GSMetro.IsUndergroundSignalTwoWay(tile, track, d), "path signals one way, others both ways");
					}
				}

				/* Turned round and converted signals. */
				local turned_block = build.make_tile(x + 27, y);
				report.check_true("turned_block", GSMetro.BuildUndergroundSignal(turned_block, track, d, GSRail.SIGNALTYPE_COMBO, GSMetro.SIGNAL_VARIANT_SEMAPHORE) &&
					GSMetro.BuildUndergroundSignal(turned_block, track, d, GSRail.SIGNALTYPE_COMBO, GSMetro.SIGNAL_VARIANT_SEMAPHORE), "turn block");
				report.check_true("turned_block_oneway", !GSMetro.IsUndergroundSignalTwoWay(turned_block, track, d), "turned block one way");
				local converted = build.make_tile(x + 29, y);
				report.check_true("converted", GSMetro.BuildUndergroundSignal(converted, track, d) &&
					GSMetro.BuildUndergroundSignal(converted, track, d) &&
					GSMetro.BuildUndergroundSignal(converted, track, d, GSRail.SIGNALTYPE_EXIT, GSMetro.SIGNAL_VARIANT_SEMAPHORE), "convert");
				report.check_eq("converted_type", GSRail.SIGNALTYPE_EXIT, GSMetro.GetUndergroundSignalType(converted, track, d), "converted type");
				report.check_eq("converted_variant", GSMetro.SIGNAL_VARIANT_SEMAPHORE, GSMetro.GetUndergroundSignalVariant(converted, track, d), "converted variant");
				report.check_true("converted_oneway", !GSMetro.IsUndergroundSignalTwoWay(converted, track, d), "converting keeps the direction");
				report.check_eq("plain_track_none", GSRail.SIGNALTYPE_NONE, GSMetro.GetUndergroundSignalType(build.make_tile(x + 1, y), track, d), "no signal");

				report.metric("origin_x", x);
				report.metric("origin_y", y);
				cm = null;
				/* The verify phase finds the line by this sign; built as deity, so the game script sees it after loading. */
				report.check_true("origin_sign", GSSign.IsValidSign(GSSign.BuildSign(build.make_tile(x, y), "metro_signal_origin")), "sign");
				report.check_true("saved", GSTest.SaveGame("metro_signal_types_reload_fixture.sav"), "Save should be written.");
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}

	function Verify(MetroTest, origin)
	{
		local report = MetroTest("metro_signal_types_reload", "fixture.sav", null);
		try {
			if (origin != null) {
				local x = GSMap.GetTileX(origin);
				local y = GSMap.GetTileY(origin);
				local d = -1;
				local track = GSRail.RAILTRACK_NE_SW;
				local T = function(xx) : (y) { return GSMap.GetTileIndex(xx, y); };

				local types = [GSRail.SIGNALTYPE_NORMAL, GSRail.SIGNALTYPE_ENTRY, GSRail.SIGNALTYPE_EXIT,
					GSRail.SIGNALTYPE_COMBO, GSRail.SIGNALTYPE_PBS, GSRail.SIGNALTYPE_PBS_ONEWAY];
				local variants = [GSMetro.SIGNAL_VARIANT_ELECTRIC, GSMetro.SIGNAL_VARIANT_SEMAPHORE];
				foreach (ti, type in types) {
					foreach (vi, variant in variants) {
						local tile = T(x + 2 + 2 * (ti * 2 + vi));
						local key = "t" + type + "v" + variant;
						report.check_true(key + "_present", GSMetro.HasUndergroundSignal(tile, track, d), "signal survives");
						report.check_eq(key + "_type", type, GSMetro.GetUndergroundSignalType(tile, track, d), "type survives");
						report.check_eq(key + "_variant", variant, GSMetro.GetUndergroundSignalVariant(tile, track, d), "variant survives");
						local pbs = type == GSRail.SIGNALTYPE_PBS || type == GSRail.SIGNALTYPE_PBS_ONEWAY;
						report.check_eq(key + "_twoway", !pbs, GSMetro.IsUndergroundSignalTwoWay(tile, track, d), "direction survives");
						/* The tile between two signals stays plain track. */
						report.check_eq(key + "_gap", GSRail.SIGNALTYPE_NONE, GSMetro.GetUndergroundSignalType(T(x + 3 + 2 * (ti * 2 + vi)), track, d), "no signal between");
					}
				}
				report.check_eq("turned_block_type", GSRail.SIGNALTYPE_COMBO, GSMetro.GetUndergroundSignalType(T(x + 27), track, d), "turned type");
				report.check_eq("turned_block_variant", GSMetro.SIGNAL_VARIANT_SEMAPHORE, GSMetro.GetUndergroundSignalVariant(T(x + 27), track, d), "turned variant");
				report.check_true("turned_block_oneway", !GSMetro.IsUndergroundSignalTwoWay(T(x + 27), track, d), "turned direction");
				report.check_eq("converted_type", GSRail.SIGNALTYPE_EXIT, GSMetro.GetUndergroundSignalType(T(x + 29), track, d), "converted type");
				report.check_eq("converted_variant", GSMetro.SIGNAL_VARIANT_SEMAPHORE, GSMetro.GetUndergroundSignalVariant(T(x + 29), track, d), "converted variant");
				report.check_true("converted_oneway", !GSMetro.IsUndergroundSignalTwoWay(T(x + 29), track, d), "converted direction");

				/* Turning the loaded path signal round still works: one-way path signals go one side -> the other. */
				local cm = GSCompanyMode(0);
				local pbs_tile = T(x + 2 + 2 * (5 * 2));
				report.check_true("turn_loaded", GSMetro.BuildUndergroundSignal(pbs_tile, track, d), "turn");
				report.check_eq("turn_loaded_type", GSRail.SIGNALTYPE_PBS_ONEWAY, GSMetro.GetUndergroundSignalType(pbs_tile, track, d), "type after turning");
				report.check_true("turn_loaded_oneway", !GSMetro.IsUndergroundSignalTwoWay(pbs_tile, track, d), "path signals never become two-way by turning");
				cm = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
