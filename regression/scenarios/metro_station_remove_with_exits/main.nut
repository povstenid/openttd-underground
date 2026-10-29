/*
 * Removing the last platform of an underground station takes its exits with it,
 * so nothing keeps the station alive: it loses its train facility and is deleted
 * like any unused station. A station built again in the same place is then the
 * only one there.
 *
 * Layout (y grows downwards):
 *
 *   py - 2  road ======== U ====         U = underpass exit
 *   py           [A0][A1]                A0, A1 = platforms of one station
 *   py + 2       P                       P = pavilion exit
 */
class MetroStationRemoveWithExits extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_station_remove_with_exits", null, 510091);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();

		try {
			local area = build.find_buildable_rectangle(16, 10);
			report.check_true("build_area_found", area != null, "Expected a clear area for the test layout.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);

			local roadtypes = GSRoadTypeList(GSRoad.ROADTRAMTYPES_ROAD);
			local roadtype = roadtypes.Begin();
			report.check_true("roadtype_available", !roadtypes.IsEnd(), "Expected a road type.");
			if (!roadtypes.IsEnd()) GSRoad.SetCurrentRoadType(roadtype);

			if (area != null && company_mode != null) {
				report.check_true("area_levelled", build.level_area(area, 16, 10), "Test area should be flat.");

				local ax = build.tile_x(area) + 5;
				local py = build.tile_y(area) + 4;
				local a0 = build.make_tile(ax, py);
				local a1 = build.make_tile(ax + 1, py);
				local pavilion = build.make_tile(ax, py + 2);
				local underpass = build.make_tile(ax + 1, py - 2);
				local depth = GSTile.GetCornerHeight(a0, GSTile.CORNER_N) - 1;

				report.check_true("platform_a0_built", GSMetro.BuildUndergroundStation(a0, GSRail.RAILTRACK_NE_SW, depth, GSStation.STATION_NEW), "Platform A0 should be built (err=" + GSError.GetLastErrorString() + ").");
				local station = GSMetro.GetUndergroundStationID(a0, depth);
				report.check_true("platform_a1_built", GSMetro.BuildUndergroundStation(a1, GSRail.RAILTRACK_NE_SW, depth, station), "Platform A1 should join the station (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("one_station", station, GSMetro.GetUndergroundStationID(a1, depth), "Both platforms should be one station.");

				report.check_true("road_built", GSRoad.BuildRoad(build.make_tile(ax - 3, py - 2), build.make_tile(ax + 4, py - 2)), "Road for the underpass should be built.");
				report.check_true("pavilion_built", GSMetro.BuildExitSurface(pavilion, station, GSMetro.EXIT_PAVILION), "Pavilion should be built (err=" + GSError.GetLastErrorString() + ").");
				report.check_true("underpass_built", GSMetro.BuildExitSurface(underpass, station, GSMetro.EXIT_UNDERPASS), "Underpass should be built (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("two_exits", 2, GSMetro.GetStationComplexExitCount(station), "Station should have two exits.");

				/* While a platform is left, the exits stay. */
				report.check_true("remove_a0", GSMetro.RemoveUndergroundStation(a0, depth), "Platform A0 should be removed (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("exits_kept", 2, GSMetro.GetStationComplexExitCount(station), "Exits should stay while a platform is left.");
				report.check_eq("pavilion_kept", GSMetro.EXIT_PAVILION, GSMetro.GetExitType(pavilion), "The pavilion should still be found on its tile.");
				report.check_eq("underpass_kept", GSMetro.EXIT_UNDERPASS, GSMetro.GetExitType(underpass), "The underpass should still be found on its tile.");

				/* The last platform takes the exits with it. */
				report.check_true("remove_a1", GSMetro.RemoveUndergroundStation(a1, depth), "Platform A1 should be removed (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("pavilion_gone", GSMetro.EXIT_INVALID, GSMetro.GetExitType(pavilion), "No exit should be left on the pavilion tile.");
				report.check_true("pavilion_tile_cleared", !GSTile.IsStationTile(pavilion), "The pavilion building should be gone.");
				report.check_eq("underpass_gone", GSMetro.EXIT_INVALID, GSMetro.GetExitType(underpass), "No exit should be left on the road tile.");
				report.check_true("road_kept", GSRoad.IsRoadTile(underpass), "The road of the underpass should stay.");
				report.check_eq("no_exits", 0, GSMetro.GetStationComplexExitCount(station), "The station should have no exits left.");
				report.check_true("no_train_facility", !GSStation.IsValidStation(station) || !GSStation.HasStationType(station, GSStation.STATION_TRAIN), "The station should lose its train facility.");

				/* A station built again in the same place starts afresh. */
				report.check_true("rebuilt", GSMetro.BuildUndergroundStation(a0, GSRail.RAILTRACK_NE_SW, depth, GSStation.STATION_JOIN_ADJACENT), "Platform should be built again (err=" + GSError.GetLastErrorString() + ").");
				local rebuilt = GSMetro.GetUndergroundStationID(a0, depth);
				report.check_true("rebuilt_valid", GSStation.IsValidStation(rebuilt), "The rebuilt platform should have a station.");
				report.check_eq("rebuilt_no_exits", 0, GSMetro.GetStationComplexExitCount(rebuilt), "The rebuilt station should start without exits.");

				/* Nothing keeps the old station alive: it goes like any unused station,
				 * and the rebuilt one is the only station left. */
				if (rebuilt != station) {
					local waited = 0;
					while (GSStation.IsValidStation(station) && waited < 12000) {
						GSController.Sleep(100);
						waited += 100;
					}
					report.check_true("station_deleted", !GSStation.IsValidStation(station), "The emptied station should be deleted.");
				}
				report.check_eq("only_station", 1, GSStationList(GSStation.STATION_ANY).Count(), "The rebuilt station should be the only station.");
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
