class MetroRegressionStationRemovePreservesExit extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_regression_station_remove_preserves_exit", null, 510006);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);

		try {
			local area = build.find_buildable_rectangle(24, 8);
			report.check_true("build_area_found", area != null, "Expected a clear area for underground station removal regression.");

			local company = companies.ensure_company();
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 3;
				local depth = -1;

				local track_left = build.make_tile(x + 8, y);
				local station_tile = build.make_tile(x + 9, y);
				local track_right = build.make_tile(x + 10, y);
				local extra_exit = build.make_tile(x + 11, y);

				local company_mode = GSCompanyMode(company);

				report.check_true("left_track_built", GSMetro.BuildUndergroundRail(track_left, GSRail.RAILTRACK_NE_SW, depth), "Left underground approach should be built.");
				report.check_true("right_track_built", GSMetro.BuildUndergroundRail(track_right, GSRail.RAILTRACK_NE_SW, depth), "Right underground approach should be built.");
				report.check_true("station_built", metro.build_station_x(station_tile, depth), "Underground station should be built.");

				local station_id = GSMetro.GetUndergroundStationID(station_tile, depth);
				report.check_true("station_id_valid", GSStation.IsValidStation(station_id), "Underground platform should resolve to a station id.");
				report.check_true("extra_exit_built", GSStation.IsValidStation(station_id) && GSMetro.BuildExitSurface(extra_exit, station_id, GSMetro.EXIT_PAVILION), "Regression fixture should add a surface exit.");
				report.check_eq("exit_count_before_remove", 1, GSMetro.GetStationComplexExitCount(station_id), "Station complex should have only the explicitly built exit before removal (platforms create no exits).");
				report.check_eq("platform_count_before_remove", 1, GSMetro.GetStationComplexPlatformCount(station_id), "Station complex should contain one underground platform before removal.");

				report.check_true("station_removed", GSMetro.RemoveUndergroundStation(station_tile, depth), "Removing the underground station should succeed.");
				report.check_true("platform_cleared", !GSMetro.IsUndergroundTile(station_tile, depth), "Station tile should no longer contain underground infrastructure.");
				report.check_true("station_lookup_cleared", !GSStation.IsValidStation(GSMetro.GetUndergroundStationID(station_tile, depth)), "Removed underground platform should not resolve to a station id anymore.");
				report.check_true("surface_exit_removed", !GSMetro.HasSurfaceExit(station_id), "The surface exit should go with the last underground platform.");
				report.check_eq("platform_count_after_remove", 0, GSMetro.GetStationComplexPlatformCount(station_id), "Removing the underground platform should leave zero underground platforms.");
				report.check_eq("exit_count_after_remove", 0, GSMetro.GetStationComplexExitCount(station_id), "Removing the last underground platform should remove the surface exit.");
				report.check_true("pavilion_cleared", !GSTile.IsStationTile(extra_exit), "The pavilion building should be gone with the station.");

				report.metric("station_id", station_id);
				report.metric("deepest_level_after_remove", GSMetro.GetStationComplexDeepestLevel(station_id));

				company_mode = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
