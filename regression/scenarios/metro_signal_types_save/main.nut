/**
 * metro_signal_types_save
 *
 * Builds every underground signal type in both variants (light and
 * semaphore), checks them through the API and writes a savegame. The same
 * line is built by the build phase of metro_signal_types_reload, whose
 * savegame is that scenario's fixture.
 *
 * Underground line at level -1 along row y, tiles x+2 .. x+25:
 *   tile x + 2 + 2 * (type * 2 + variant) carries a signal of that type and
 *   variant; the block signals face both ways, path signals one way. Two more
 *   tiles hold signals turned round: x+27 (block, turned to one way) and x+29
 *   (path, turned to the other side).
 */
class MetroSignalTypesSave extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_signal_types_save", null, 510008);
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
				report.check_true("saved", GSTest.SaveGame("metro_signal_types_save.sav"), "Save should be written.");
				cm = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
