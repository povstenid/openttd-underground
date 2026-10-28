class MetroUndergroundStationBuild extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_underground_station_build", null, 510002);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);

		try {
			local area = build.find_buildable_rectangle(24, 8);
			report.check_true("build_area_found", area != null, "Expected a clear area for underground station construction.");

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

				report.check_true("underground_left_track_built", GSMetro.BuildUndergroundRail(track_left, GSRail.RAILTRACK_NE_SW, depth), "Left underground approach track should be built.");
				report.check_true("underground_right_track_built", GSMetro.BuildUndergroundRail(track_right, GSRail.RAILTRACK_NE_SW, depth), "Right underground approach track should be built.");
				report.check_true("underground_station_built", metro.build_station_x(station_tile, depth), "Underground station tile should be built.");

				local station_id = GSMetro.GetUndergroundStationID(station_tile, depth);
				report.check_true("underground_station_valid", GSStation.IsValidStation(station_id), "Underground station should resolve to a station id.");
				report.check_true("no_exit_before_build", !GSMetro.HasSurfaceExit(station_id), "A platform alone should not create a surface exit.");
				report.check_true("extra_surface_exit_built", GSStation.IsValidStation(station_id) && GSMetro.BuildExitSurface(extra_exit, station_id, GSMetro.EXIT_PAVILION), "Extra surface exit should be linked to the station complex.");

				report.check_true("depth_matches_expected", GSMetro.GetStationComplexDeepestLevel(station_id) == depth, "Station complex should report the underground depth.");
				report.check_true("surface_exit_present", GSMetro.HasSurfaceExit(station_id), "Station complex should have at least one surface exit.");
				report.check_eq("platform_count", 1, GSMetro.GetStationComplexPlatformCount(station_id), "Expected one underground platform in the station complex.");
				report.check_eq("exit_count", 1, GSMetro.GetStationComplexExitCount(station_id), "Expected only the explicitly built surface exit.");

				report.metric("station_id", station_id);
				report.metric("deepest_level", GSMetro.GetStationComplexDeepestLevel(station_id));

				company_mode = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
