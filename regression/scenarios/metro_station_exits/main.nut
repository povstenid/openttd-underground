/*
 * Station exits of underground stations come in two types:
 *  - pavilion: a bus station tile of the same station on a free tile, catchment 3;
 *  - underpass: stairs on a straight road tile that stays a road, catchment 2.
 * Both must lie within construction.max_station_exit_distance (default 2,
 * DistanceMax) of a platform of the station.
 *
 * Layout (platform A at (ax, py), platform B at (bx, py), y grows downwards):
 *
 *   py      A                      B
 *   py + 2  pavilion (dist 2)      underpass on road (dist 2)
 *   py + 3  pavilion refused (3)
 *   py + 4                         road for the second underpass (dist 4, setting 4)
 *   py + 5  probe: dist 3 from the exits, 5 from the platforms
 *
 * Platforms catch nothing on their own: until an exit is built a station has
 * an empty coverage, and all catchment comes from the exits.
 */
class MetroStationExits extends GSController {
	report = null;

	function expect_error(id, ok, expected)
	{
		local err = GSError.GetLastError();
		this.report.check_true(id + "_refused", !ok, "Command should be refused.");
		this.report.check_eq(id + "_error", expected, err, "Unexpected error code (got " + GSError.GetLastErrorString() + ").");
	}

	function covered(station_id, tile)
	{
		return GSTileList_StationCoverage(station_id).HasItem(tile);
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		this.report = MetroTest("metro_station_exits", null, 510071);
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();

		try {
			local area = build.find_buildable_rectangle(30, 16);
			report.check_true("build_area_found", area != null, "Expected a clear area for the test layout.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);

			local roadtypes = GSRoadTypeList(GSRoad.ROADTRAMTYPES_ROAD);
			local roadtype = roadtypes.Begin();
			report.check_true("roadtype_available", !roadtypes.IsEnd(), "Expected a road type.");
			if (!roadtypes.IsEnd()) GSRoad.SetCurrentRoadType(roadtype);

			if (area != null && company_mode != null) {
				report.check_true("area_levelled", build.level_area(area, 30, 16), "Test area should be flat.");
				report.check_eq("default_exit_distance", 2, GSGameSettings.GetValue("construction.max_station_exit_distance"), "Default exit distance limit should be 2.");

				local x = build.tile_x(area) + 2;
				local py = build.tile_y(area) + 3;
				local ax = x + 3;
				local bx = x + 18;

				local platform_a = build.make_tile(ax, py);
				local platform_b = build.make_tile(bx, py);
				local depth = GSTile.GetCornerHeight(platform_a, GSTile.CORNER_N) - 1;

				report.check_true("station_a_built", GSMetro.BuildUndergroundStation(platform_a, GSRail.RAILTRACK_NE_SW, depth, GSStation.STATION_NEW), "Station A should be built (err=" + GSError.GetLastErrorString() + ").");
				report.check_true("station_b_built", GSMetro.BuildUndergroundStation(platform_b, GSRail.RAILTRACK_NE_SW, depth, GSStation.STATION_NEW), "Station B should be built (err=" + GSError.GetLastErrorString() + ").");
				local station_a = GSMetro.GetUndergroundStationID(platform_a, depth);
				local station_b = GSMetro.GetUndergroundStationID(platform_b, depth);
				report.check_true("stations_valid", GSStation.IsValidStation(station_a) && GSStation.IsValidStation(station_b) && station_a != station_b, "Both platforms should be separate stations.");

				local pavilion = build.make_tile(ax, py + 2);
				local pavilion_far = build.make_tile(ax, py + 3);
				local probe_a = build.make_tile(ax, py + 5);
				local probe_a_out = build.make_tile(ax, py + 6);

				/* No exit yet: the stations exist but cover nothing, not even the tile above the platform. */
				report.check_true("no_exit_a_initially", !GSMetro.HasSurfaceExit(station_a) && GSMetro.GetStationComplexExitCount(station_a) == 0, "A platform alone should not create an exit.");
				report.check_true("no_exit_coverage_a", GSTileList_StationCoverage(station_a).IsEmpty(), "Station without exits should have an empty coverage.");
				report.check_true("no_exit_coverage_b", GSTileList_StationCoverage(station_b).IsEmpty(), "Station without exits should have an empty coverage.");
				report.check_true("probe_a_not_covered_initially", !this.covered(station_a, probe_a), "Platform alone should not cover a tile 5 tiles away.");

				/* Pavilion within the limit: becomes a bus station tile of station A. */
				report.check_true("pavilion_built", GSMetro.BuildExitSurface(pavilion, station_a, GSMetro.EXIT_PAVILION), "Pavilion 2 tiles from the platform should be built (err=" + GSError.GetLastErrorString() + ").");
				report.check_true("pavilion_is_station_tile", GSTile.IsStationTile(pavilion), "Pavilion tile should be a station tile.");
				report.check_eq("pavilion_station", station_a, GSStation.GetStationID(pavilion), "Pavilion tile should belong to the underground station.");
				report.check_true("pavilion_is_bus_stop", GSRoad.IsRoadStationTile(pavilion), "Pavilion should be a bus station tile.");
				report.check_eq("pavilion_exit_type", GSMetro.EXIT_PAVILION, GSMetro.GetExitType(pavilion), "Exit type should be pavilion.");
				report.check_true("pavilion_has_exit", GSMetro.HasSurfaceExit(station_a), "Station should report a surface exit.");

				/* Pavilion catchment: radius 3. */
				report.check_true("pavilion_covers_distance_3", this.covered(station_a, probe_a), "Pavilion should cover a tile 3 tiles away.");
				report.check_true("pavilion_not_distance_4", !this.covered(station_a, probe_a_out), "Pavilion should not cover a tile 4 tiles away.");

				/* The pavilion tile can only be removed together with its exit. */
				this.expect_error("pavilion_demolish", GSTile.DemolishTile(pavilion), GSMetro.ERR_UNDERGROUND_MUST_REMOVE_EXIT_FIRST);
				this.expect_error("pavilion_remove_road_station", GSRoad.RemoveRoadStation(pavilion), GSMetro.ERR_UNDERGROUND_MUST_REMOVE_EXIT_FIRST);
				report.check_true("pavilion_survives", GSTile.IsStationTile(pavilion) && GSMetro.GetExitType(pavilion) == GSMetro.EXIT_PAVILION, "Pavilion should survive demolition attempts.");

				/* Too far: 3 tiles with the default limit of 2; and a second exit on the same tile. */
				this.expect_error("pavilion_too_far", GSMetro.BuildExitSurface(pavilion_far, station_a, GSMetro.EXIT_PAVILION), GSMetro.ERR_UNDERGROUND_EXIT_TOO_FAR);
				report.check_true("pavilion_far_untouched", !GSTile.IsStationTile(pavilion_far), "Refused pavilion should not build anything.");
				this.expect_error("pavilion_twice", GSMetro.BuildExitSurface(pavilion, station_a, GSMetro.EXIT_PAVILION), GSMetro.ERR_UNDERGROUND_EXIT_ALREADY_BUILT);

				/* Underpass: straight road 2 tiles from platform B. */
				local road_y = py + 2;
				local underpass = build.make_tile(bx, road_y);
				local probe_b = build.make_tile(bx, py + 5);
				report.check_true("road_built", GSRoad.BuildRoad(build.make_tile(bx - 1, road_y), build.make_tile(bx + 1, road_y)), "Straight road should be built (err=" + GSError.GetLastErrorString() + ").");
				this.expect_error("underpass_on_grass", GSMetro.BuildExitSurface(build.make_tile(bx + 1, py + 1), station_b, GSMetro.EXIT_UNDERPASS), GSMetro.ERR_UNDERGROUND_EXIT_NEEDS_STRAIGHT_ROAD);
				this.expect_error("underpass_on_road_end", GSMetro.BuildExitSurface(build.make_tile(bx + 1, road_y), station_b, GSMetro.EXIT_UNDERPASS), GSMetro.ERR_UNDERGROUND_EXIT_NEEDS_STRAIGHT_ROAD);
				report.check_true("underpass_built", GSMetro.BuildExitSurface(underpass, station_b, GSMetro.EXIT_UNDERPASS), "Underpass on a straight road should be built (err=" + GSError.GetLastErrorString() + ").");
				report.check_true("underpass_road_kept", GSRoad.IsRoadTile(underpass) && !GSTile.IsStationTile(underpass), "Underpass tile should stay a plain road.");
				report.check_true("underpass_road_connected", GSRoad.AreRoadTilesConnected(underpass, build.make_tile(bx + 1, road_y)), "Road through the underpass should stay connected.");
				report.check_eq("underpass_exit_type", GSMetro.EXIT_UNDERPASS, GSMetro.GetExitType(underpass), "Exit type should be underpass.");

				/* Underpass catchment: radius 2, so the probe the pavilion catches is not covered. */
				report.check_true("underpass_covers_distance_2", this.covered(station_b, build.make_tile(bx, road_y + 2)), "Underpass should cover a tile 2 tiles away.");
				report.check_true("underpass_not_distance_3", !this.covered(station_b, probe_b), "Underpass should not cover a tile 3 tiles away.");
				this.expect_error("underpass_too_far", GSMetro.BuildExitSurface(build.make_tile(bx, py + 3), station_b, GSMetro.EXIT_UNDERPASS), GSMetro.ERR_UNDERGROUND_EXIT_TOO_FAR);

				/* Removal restores the tiles. */
				report.check_true("pavilion_removed", GSMetro.RemoveExitSurface(pavilion), "Pavilion should be removed (err=" + GSError.GetLastErrorString() + ").");
				report.check_true("pavilion_tile_cleared", !GSTile.IsStationTile(pavilion) && GSTile.IsBuildable(pavilion), "Pavilion tile should be free again.");
				report.check_eq("pavilion_exit_gone", GSMetro.EXIT_INVALID, GSMetro.GetExitType(pavilion), "No exit should be left on the pavilion tile.");
				report.check_true("pavilion_catchment_gone", !this.covered(station_a, probe_a), "Removing the pavilion should drop its catchment.");
				this.expect_error("remove_missing_exit", GSMetro.RemoveExitSurface(pavilion), GSMetro.ERR_UNDERGROUND_NO_EXIT);

				report.check_true("underpass_removed", GSMetro.RemoveExitSurface(underpass), "Underpass should be removed (err=" + GSError.GetLastErrorString() + ").");
				report.check_true("underpass_road_after_remove", GSRoad.IsRoadTile(underpass), "Road should remain after removing the underpass.");
				report.check_eq("underpass_exit_gone", GSMetro.EXIT_INVALID, GSMetro.GetExitType(underpass), "No exit should be left on the road tile.");

				/* A larger limit allows exits further away. */
				report.check_true("setting_changed", GSGameSettings.SetValue("construction.max_station_exit_distance", 4), "Exit distance setting should be changeable.");
				report.check_eq("setting_value", 4, GSGameSettings.GetValue("construction.max_station_exit_distance"), "Exit distance setting should read back 4.");
				report.check_true("pavilion_far_built_with_limit_4", GSMetro.BuildExitSurface(pavilion_far, station_a, GSMetro.EXIT_PAVILION), "Pavilion 3 tiles away should be built with limit 4 (err=" + GSError.GetLastErrorString() + ").");
				report.check_true("pavilion_far_is_station_tile", GSTile.IsStationTile(pavilion_far) && GSStation.GetStationID(pavilion_far) == station_a, "Far pavilion should be a station tile of station A.");

				local road2_y = py + 4;
				local underpass_far = build.make_tile(bx, road2_y);
				report.check_true("road2_built", GSRoad.BuildRoad(build.make_tile(bx - 1, road2_y), build.make_tile(bx + 1, road2_y)), "Second straight road should be built.");
				report.check_true("underpass_far_built", GSMetro.BuildExitSurface(underpass_far, station_b, GSMetro.EXIT_UNDERPASS), "Underpass 4 tiles away should be built with limit 4 (err=" + GSError.GetLastErrorString() + ").");
				report.check_true("underpass_far_covers_2", this.covered(station_b, build.make_tile(bx, road2_y + 2)), "Far underpass should cover a tile 2 tiles beyond it (6 from the platform).");
				report.check_true("underpass_far_not_3", !this.covered(station_b, build.make_tile(bx, road2_y + 3)), "Far underpass should not cover a tile 3 tiles beyond it.");
				this.expect_error("pavilion_too_far_limit_4", GSMetro.BuildExitSurface(build.make_tile(ax, py + 5), station_a, GSMetro.EXIT_PAVILION), GSMetro.ERR_UNDERGROUND_EXIT_TOO_FAR);

				report.check_true("setting_restored", GSGameSettings.SetValue("construction.max_station_exit_distance", 2), "Exit distance setting should be restorable.");

				report.metric("station_a", station_a);
				report.metric("station_b", station_b);
				report.metric("exit_count_a", GSMetro.GetStationComplexExitCount(station_a));
				report.metric("exit_count_b", GSMetro.GetStationComplexExitCount(station_b));
			}

			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
