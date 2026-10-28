/*
 * metro_free_platform_choice
 *
 *   surface -- P(x+7) -- x+8..x+9 -- J(x+10) -- [S S] row y     (straight)
 *                                       \------ [S S] row y + 1 (curve)
 *
 * One station, two terminus platforms behind a switch. Train A (full load,
 * never fills) takes the straight platform, the cheaper way, and stays. Train
 * B must take the free platform beside it instead of waiting for A: the
 * underground pathfinder used to see no difference between a held and a free
 * platform.
 */
class MetroFreePlatformChoice extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_free_platform_choice", null, 510096);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();
		try {
			local area = build.find_buildable_rectangle(24, 8);
			report.check_true("build_area_found", area != null, "area");
			local company = companies.ensure_company();
			local cm = GSCompanyMode(company);
			local engine = trains.find_rail_engine();
			local x = build.tile_x(area), y = build.tile_y(area) + 3, d = -1;
			local T = function(ax, ay) : (build) { return build.make_tile(ax, ay); };
			local depot = T(x + 4, y + 2), front = T(x + 4, y + 1), junction = T(x + 4, y);
			report.check_true("surface", build.build_surface_x(x + 2, x + 6, y) && GSRail.BuildRail(T(x + 3, y), junction, front) && GSRail.BuildRailTrack(junction, GSRail.RAILTRACK_SW_SE), "surface");
			report.check_true("depot", GSRail.BuildRail(depot, front, junction) && GSRail.BuildRailDepot(depot, front), "depot");
			report.check_true("portal", metro.build_portal(T(x + 7, y), GSMetro.DIAGDIR_SW, d), "portal");
			report.check_true("ug", metro.build_underground_x(x + 8, x + 10, y, d), "ug");
			report.check_true("switch", GSMetro.BuildUndergroundRail(T(x + 10, y), GSRail.RAILTRACK_NE_SE, d) &&
				GSMetro.BuildUndergroundRail(T(x + 10, y + 1), GSRail.RAILTRACK_NW_SW, d), "switch " + GSError.GetLastErrorString());
			local sid = null;
			foreach (row in [y, y + 1]) {
				for (local a = x + 11; a <= x + 12; a++) {
					if (!GSMetro.BuildUndergroundStation(T(a, row), GSRail.RAILTRACK_NE_SW, d, sid == null ? GSStation.STATION_NEW : sid)) report.check_true("platform", false, "platform " + GSError.GetLastErrorString());
					if (sid == null) sid = GSMetro.GetUndergroundStationID(T(a, row), d);
				}
			}
			report.check_true("station_valid", GSStation.IsValidStation(sid), "station");

			local a_train = trains.build_train(depot, engine);
			/* A cargo wagon: at a station without passengers A waits for its full load for good. */
			local world = MetroTest.MetroWorldLib(build);
			report.check_true("a_wagon", trains.attach_cargo_wagon(a_train, depot, world.find_passenger_cargo()).attached, "A gets a passenger wagon");
			local b_train = trains.build_train(depot, engine);
			trains.append_underground_order(a_train, sid, GSOrder.OF_NON_STOP_INTERMEDIATE | GSOrder.OF_FULL_LOAD_ANY);
			trains.append_underground_order(b_train, sid, GSOrder.OF_NON_STOP_INTERMEDIATE);
			GSVehicle.StartStopVehicle(a_train);
			local t = 0;
			while (t < 4000 && GSVehicle.GetState(a_train) != GSVehicle.VS_AT_STATION) { GSController.Sleep(10); t += 10; }
			report.check_true("a_at_station", GSVehicle.GetState(a_train) == GSVehicle.VS_AT_STATION, "A arrives");
			local a_row = GSMap.GetTileY(GSMetro.GetVehicleUndergroundTile(a_train)) - y;
			report.metric("a_row", a_row);
			GSVehicle.StartStopVehicle(b_train);

			/* B calls at the station while A still stands there. */
			local b_called = false, b_row = -1, crashed = 0;
			for (local i = 0; i < 6000 && !b_called; i += 10) {
				foreach (v in [a_train, b_train]) if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) crashed = 1;
				if (crashed) break;
				if (GSMetro.IsVehicleUnderground(b_train)) {
					local bt = GSMetro.GetVehicleUndergroundTile(b_train);
					if (GSMap.GetTileX(bt) >= x + 11) b_row = GSMap.GetTileY(bt) - y;
				}
				if (GSVehicle.GetState(b_train) == GSVehicle.VS_AT_STATION && GSVehicle.GetState(a_train) == GSVehicle.VS_AT_STATION) b_called = true;
				GSController.Sleep(10);
			}
			report.metric("b_row", b_row);
			report.check_eq("no_crash", 0, crashed, "No train may crash.");
			report.check_true("b_called", b_called, "B must call at the free platform instead of waiting for A.");
			report.check_true("b_other_platform", b_row >= 0 && b_row != a_row, "B uses the other platform (A row " + a_row + ", B row " + b_row + ").");
			cm = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
