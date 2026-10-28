class MetroRegressionSignalRoundtrip extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_regression_signal_roundtrip", null, 510007);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);

		try {
			local area = build.find_buildable_rectangle(24, 8);
			report.check_true("build_area_found", area != null, "Expected a clear area for underground signal regression.");

			local company = companies.ensure_company();
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 3;
				local depth = -1;

				local signal_tile = build.make_tile(x + 9, y);

				local company_mode = GSCompanyMode(company);

				report.check_true("underground_chain_built", metro.build_underground_x(x + 8, x + 10, y, depth), "Underground test chain should be built.");
				report.check_true("signal_built", GSMetro.BuildUndergroundSignal(signal_tile, GSRail.RAILTRACK_NE_SW, depth), "Underground PBS signal should be placed.");
				report.check_true("signal_visible", GSMetro.HasUndergroundSignal(signal_tile, GSRail.RAILTRACK_NE_SW, depth), "Underground signal should be queryable after placement.");
				report.check_true("track_present_before_remove", GSMetro.HasUndergroundRail(signal_tile, depth), "Track should still exist underneath the signal.");
				report.check_true("signal_removed", GSMetro.RemoveUndergroundSignal(signal_tile, GSRail.RAILTRACK_NE_SW, depth), "Removing the underground signal should succeed.");
				report.check_true("signal_absent_after_remove", !GSMetro.HasUndergroundSignal(signal_tile, GSRail.RAILTRACK_NE_SW, depth), "Underground signal should be gone after removal.");
				report.check_true("track_preserved_after_remove", GSMetro.HasUndergroundRail(signal_tile, depth), "Removing the underground signal must not remove the track.");
				report.check_eq("track_bits_preserved", GSRail.RAILTRACK_NE_SW, GSMetro.GetUndergroundTrackBits(signal_tile, depth), "Track bits should remain unchanged after signal round-trip.");

				report.metric("signal_tile", signal_tile);

				company_mode = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
