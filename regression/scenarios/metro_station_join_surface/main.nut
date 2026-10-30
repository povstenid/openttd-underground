/*
 * Underground platforms and surface stations make one station when joined:
 *
 *  1. an airport first, then a platform below it joining the airport's station;
 *  2. a platform first, then a surface rail station next to it joining the
 *     underground station;
 *  3. a separate underground station below the airport, joined to the
 *     airport's station before its first exit (the GUI offers it when the
 *     first pavilion is placed).
 *
 * Both are distant joins by station id (the join window of the GUI offers them).
 */
class MetroStationJoinSurface extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_station_join_surface", null, 510093);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();

		try {
			local area = build.find_buildable_rectangle(24, 14);
			report.check_true("build_area_found", area != null, "Expected a clear area.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);

			if (area != null && company_mode != null) {
				report.check_true("area_levelled", build.level_area_min_height(area, 24, 14, 3), "The area should be flat at height >= 3.");
				local L = build.tile_x(area);
				local T = build.tile_y(area);
				local depth = GSTile.GetMinHeight(build.make_tile(L + 1, T + 1)) - 1;

				/* 1. Airport first, then a platform below it. */
				local airport_tile = build.make_tile(L + 1, T + 1);
				report.check_true("airport_built", GSAirport.BuildAirport(airport_tile, GSAirport.AT_LARGE, GSStation.STATION_NEW), "Airport should be built (err=" + GSError.GetLastErrorString() + ").");
				local airport_station = GSStation.GetStationID(airport_tile);
				report.check_true("airport_station_valid", GSStation.IsValidStation(airport_station), "The airport should be a station.");
				local below = build.make_tile(L + 3, T + 3);
				report.check_true("platform_joins_airport", GSMetro.BuildUndergroundStation(below, GSRail.RAILTRACK_NE_SW, depth, airport_station), "A platform below should join the airport (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("platform_station_is_airport", airport_station, GSMetro.GetUndergroundStationID(below, depth), "The platform should belong to the airport's station.");
				report.check_true("joined_has_airport", GSStation.HasStationType(airport_station, GSStation.STATION_AIRPORT), "The joined station should still have its airport.");
				report.check_true("joined_has_train", GSStation.HasStationType(airport_station, GSStation.STATION_TRAIN), "The joined station should serve trains.");

				/* 2. Platform first, then a surface rail station next to it. */
				local platform = build.make_tile(L + 14, T + 4);
				report.check_true("metro_built", GSMetro.BuildUndergroundStation(platform, GSRail.RAILTRACK_NE_SW, depth, GSStation.STATION_NEW), "The metro platform should be built (err=" + GSError.GetLastErrorString() + ").");
				local metro_station = GSMetro.GetUndergroundStationID(platform, depth);
				local rail_tile = build.make_tile(L + 14, T + 7);
				report.check_true("rail_station_joins_metro", GSRail.BuildRailStation(rail_tile, GSRail.RAILTRACK_NE_SW, 1, 3, metro_station), "A surface rail station should join the metro station (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("rail_station_is_metro", metro_station, GSStation.GetStationID(rail_tile), "The rail station should belong to the metro station.");
				report.check_eq("metro_still_metro", metro_station, GSMetro.GetUndergroundStationID(platform, depth), "The platform should keep its station.");
				report.check_eq("station_count", 2, GSStationList(GSStation.STATION_ANY).Count(), "There should be two stations: airport+metro and metro+rail.");

				/* 3. A separate underground station below the airport, joined to it before its first exit. */
				local separate = build.make_tile(L + 5, T + 6);
				report.check_true("separate_built", GSMetro.BuildUndergroundStation(separate, GSRail.RAILTRACK_NE_SW, depth, GSStation.STATION_NEW), "A separate platform should be built (err=" + GSError.GetLastErrorString() + ").");
				local separate_station = GSMetro.GetUndergroundStationID(separate, depth);
				report.check_true("separate_is_separate", separate_station != airport_station, "The platform should be its own station.");
				report.check_true("save_before_join", GSTest.SaveGame("metro_station_join_surface_before.sav"), "The state before the join should be saved for trying the GUI.");
				report.metric("separate", separate);
				report.metric("airport", airport_tile);
				report.check_true("joined_into_airport", GSMetro.JoinUndergroundStation(separate_station, airport_station), "The underground station should join the airport (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("separate_now_airport", airport_station, GSMetro.GetUndergroundStationID(separate, depth), "The platform should now belong to the airport's station.");
				local pavilion = build.make_tile(L + 5, T + 8);
				report.check_true("pavilion_for_airport", GSMetro.BuildExitSurface(pavilion, airport_station, GSMetro.EXIT_PAVILION), "The first exit should be built for the joined station (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("pavilion_station", airport_station, GSStation.GetStationID(pavilion), "The pavilion should belong to the airport's station.");
				report.check_true("join_twice_fails", !GSMetro.JoinUndergroundStation(airport_station, metro_station), "A station that is not only underground cannot be joined.");
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
