/**
 * metro_ramp_signal_reload
 *
 * Signals on ramps between two underground levels: building them through
 * either level, removing them (also with the ramp) and keeping them over
 * saving and loading.
 *
 * Along row y, ramps from level -1 down to level -2 (descending south-west):
 *   tile x + 2 + 2 * i carries a signal of types[i], variant i % 2; built
 *   through the upper level for even i, the lower level for odd i;
 *   x + 14: a path signal turned round (still one way);
 *   x + 16: a ramp without signals.
 * The ramp along y at (x + 18, y + 2), descending south-east, carries a
 * two-way combo semaphore.
 *
 * The fixture is a savegame written by this script: without the sign
 * "metro_ramp_signal_origin" it builds the ramps, checks them and writes
 * metro_ramp_signal_reload_fixture.sav (copy it from the save directory to
 * fixture.sav to regenerate the fixture); with the sign (the fixture loaded)
 * it checks the signals again.
 */
class MetroRampSignalReload extends GSController {
	types = [GSRail.SIGNALTYPE_NORMAL, GSRail.SIGNALTYPE_ENTRY, GSRail.SIGNALTYPE_EXIT,
		GSRail.SIGNALTYPE_COMBO, GSRail.SIGNALTYPE_PBS, GSRail.SIGNALTYPE_PBS_ONEWAY];

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local origin = null;
		local signs = GSSignList();
		for (local s = signs.Begin(); !signs.IsEnd(); s = signs.Next()) {
			if (GSSign.GetName(s) == "metro_ramp_signal_origin") origin = GSSign.GetLocation(s);
		}
		if (origin == null) {
			this.BuildFixture(MetroTest);
		} else {
			this.Verify(MetroTest, origin);
		}
	}

	/** Check every ramp signal; the same before saving and after loading. */
	function CheckSignals(report, x, y)
	{
		local track = GSRail.RAILTRACK_NE_SW;
		foreach (i, type in this.types) {
			local tile = GSMap.GetTileIndex(x + 2 + 2 * i, y);
			local variant = i % 2 == 0 ? GSMetro.SIGNAL_VARIANT_ELECTRIC : GSMetro.SIGNAL_VARIANT_SEMAPHORE;
			local pbs = type == GSRail.SIGNALTYPE_PBS || type == GSRail.SIGNALTYPE_PBS_ONEWAY;
			foreach (d in [-1, -2]) {
				local key = "r" + i + "d" + (-d);
				report.check_true(key + "_present", GSMetro.HasUndergroundSignal(tile, track, d), "ramp signal at either level");
				report.check_eq(key + "_type", type, GSMetro.GetUndergroundSignalType(tile, track, d), "type");
				report.check_eq(key + "_variant", variant, GSMetro.GetUndergroundSignalVariant(tile, track, d), "variant");
				report.check_eq(key + "_twoway", !pbs, GSMetro.IsUndergroundSignalTwoWay(tile, track, d), "path signals one way, others both ways");
			}
		}
		local turned = GSMap.GetTileIndex(x + 14, y);
		report.check_eq("turned_type", GSRail.SIGNALTYPE_PBS, GSMetro.GetUndergroundSignalType(turned, track, -2), "turned type");
		report.check_true("turned_oneway", !GSMetro.IsUndergroundSignalTwoWay(turned, track, -1), "a turned path signal stays one way");
		local plain = GSMap.GetTileIndex(x + 16, y);
		report.check_true("plain_none", !GSMetro.HasUndergroundSignal(plain, track, -1) && !GSMetro.HasUndergroundSignal(plain, track, -2), "plain ramp has no signal");
		report.check_eq("plain_type", GSRail.SIGNALTYPE_NONE, GSMetro.GetUndergroundSignalType(plain, track, -1), "plain ramp type");
		local ytile = GSMap.GetTileIndex(x + 18, y + 2);
		report.check_eq("y_type", GSRail.SIGNALTYPE_COMBO, GSMetro.GetUndergroundSignalType(ytile, GSRail.RAILTRACK_NW_SE, -2), "y ramp type");
		report.check_eq("y_variant", GSMetro.SIGNAL_VARIANT_SEMAPHORE, GSMetro.GetUndergroundSignalVariant(ytile, GSRail.RAILTRACK_NW_SE, -1), "y ramp variant");
		report.check_true("y_twoway", GSMetro.IsUndergroundSignalTwoWay(ytile, GSRail.RAILTRACK_NW_SE, -1), "y ramp two way");
		report.check_eq("y_wrong_track", GSRail.SIGNALTYPE_NONE, GSMetro.GetUndergroundSignalType(ytile, GSRail.RAILTRACK_NE_SW, -1), "no track across the ramp");
	}

	function BuildFixture(MetroTest)
	{
		local report = MetroTest("metro_ramp_signal_reload", null, 510097);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		try {
			local area = build.find_buildable_rectangle(24, 6);
			report.check_true("build_area_found", area != null, "area");
			local company = companies.ensure_company();
			local cm = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			if (area != null && cm != null) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 2;
				local track = GSRail.RAILTRACK_NE_SW;
				for (local rx = x + 2; rx <= x + 16; rx += 2) {
					report.check_true("ramp" + rx, GSMetro.BuildRamp(build.make_tile(rx, y), GSMetro.DIAGDIR_SW, -1, -2), "ramp " + GSError.GetLastErrorString());
				}
				report.check_true("ramp_y", GSMetro.BuildRamp(build.make_tile(x + 18, y + 2), GSMetro.DIAGDIR_SE, -1, -2), "ramp y " + GSError.GetLastErrorString());

				local signals_before = GSInfrastructure.GetInfrastructurePieceCount(company, GSInfrastructure.INFRASTRUCTURE_SIGNALS);
				foreach (i, type in this.types) {
					local variant = i % 2 == 0 ? GSMetro.SIGNAL_VARIANT_ELECTRIC : GSMetro.SIGNAL_VARIANT_SEMAPHORE;
					report.check_true("built" + i, GSMetro.BuildUndergroundSignal(build.make_tile(x + 2 + 2 * i, y), track, i % 2 == 0 ? -1 : -2, type, variant), "build " + GSError.GetLastErrorString());
				}
				/* Six signals, the four block types facing both ways. */
				report.check_eq("infra_signals", signals_before + 10, GSInfrastructure.GetInfrastructurePieceCount(company, GSInfrastructure.INFRASTRUCTURE_SIGNALS), "ramp signals are counted");

				local turned = build.make_tile(x + 14, y);
				report.check_true("turned", GSMetro.BuildUndergroundSignal(turned, track, -1, GSRail.SIGNALTYPE_PBS) &&
					GSMetro.BuildUndergroundSignal(turned, track, -2), "turn " + GSError.GetLastErrorString());
				report.check_true("ytrack_refused", !GSMetro.BuildUndergroundSignal(build.make_tile(x + 18, y + 2), track, -1, GSRail.SIGNALTYPE_NORMAL), "no signal across a ramp");
				report.check_true("ysignal", GSMetro.BuildUndergroundSignal(build.make_tile(x + 18, y + 2), GSRail.RAILTRACK_NW_SE, -1, GSRail.SIGNALTYPE_COMBO, GSMetro.SIGNAL_VARIANT_SEMAPHORE), "y " + GSError.GetLastErrorString());

				/* Removing the signal, or the ramp with its signals, uncounts them. */
				local plain = build.make_tile(x + 16, y);
				local count = GSInfrastructure.GetInfrastructurePieceCount(company, GSInfrastructure.INFRASTRUCTURE_SIGNALS);
				report.check_true("plain_signal", GSMetro.BuildUndergroundSignal(plain, track, -1, GSRail.SIGNALTYPE_NORMAL), "plain " + GSError.GetLastErrorString());
				report.check_true("plain_signal_removed", GSMetro.RemoveUndergroundSignal(plain, track, -2), "remove " + GSError.GetLastErrorString());
				report.check_eq("infra_after_remove", count, GSInfrastructure.GetInfrastructurePieceCount(company, GSInfrastructure.INFRASTRUCTURE_SIGNALS), "removed signal uncounted");
				local spare = build.make_tile(x + 20, y);
				report.check_true("spare_ramp", GSMetro.BuildRamp(spare, GSMetro.DIAGDIR_SW, -1, -2) && GSMetro.BuildUndergroundSignal(spare, track, -2, GSRail.SIGNALTYPE_EXIT), "spare " + GSError.GetLastErrorString());
				report.check_eq("infra_spare", count + 2, GSInfrastructure.GetInfrastructurePieceCount(company, GSInfrastructure.INFRASTRUCTURE_SIGNALS), "spare ramp signal counted");
				report.check_true("spare_removed", GSMetro.RemoveRamp(spare, -1), "remove ramp " + GSError.GetLastErrorString());
				report.check_eq("infra_spare_removed", count, GSInfrastructure.GetInfrastructurePieceCount(company, GSInfrastructure.INFRASTRUCTURE_SIGNALS), "signals go with the ramp");

				this.CheckSignals(report, x, y);
				report.metric("origin_x", x);
				report.metric("origin_y", y);
				cm = null;
				/* The verify phase finds the ramps by this sign; built as deity, so the game script sees it after loading. */
				report.check_true("origin_sign", GSSign.IsValidSign(GSSign.BuildSign(build.make_tile(x, y), "metro_ramp_signal_origin")), "sign");
				report.check_true("saved", GSTest.SaveGame("metro_ramp_signal_reload_fixture.sav"), "Save should be written.");
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}

	function Verify(MetroTest, origin)
	{
		local report = MetroTest("metro_ramp_signal_reload", "fixture.sav", null);
		try {
			this.CheckSignals(report, GSMap.GetTileX(origin), GSMap.GetTileY(origin));
			/* Ten on the six ramps, one turned path signal, two on the ramp along y. */
			report.check_eq("infra_loaded", 13, GSInfrastructure.GetInfrastructurePieceCount(0, GSInfrastructure.INFRASTRUCTURE_SIGNALS), "ramp signals counted after loading");
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
