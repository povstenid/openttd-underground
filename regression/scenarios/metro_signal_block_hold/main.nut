/**
 * metro_signal_block_hold
 *
 * Underground block signals (SIGTYPE_BLOCK) hold following trains while the
 * block behind them is occupied and let them go once it is free again.
 *
 *   depot -- P(SW) -- S1(x+9) ---- S2(x+16) [P P] S3(x+19) x+20 [Q Q]
 *
 * One-way eastbound block signals S1, S2, S3. Train 0 (engine + wagon, full
 * load, no cargo anywhere) fills platform P; train 1 must wait at S2, train 2
 * at S1. Then train 0 is released to Q: S2 turns green, train 1 enters P and
 * train 2 follows up to S2.
 */
class MetroSignalBlockHold extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_signal_block_hold", null, 510008);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();
		try {
			local area = build.find_buildable_rectangle(30, 8);
			report.check_true("build_area_found", area != null, "area");
			local company = companies.ensure_company();
			local cm = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			local engine = cm == null ? null : trains.find_rail_engine();
			local wagon = cm == null ? null : trains.find_rail_wagon();
			report.check_true("engine", engine != null && wagon != null, "engine and wagon");
			if (area != null && engine != null && wagon != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 3;
				local d = -1;
				local depot_junction = build.make_tile(x + 6, y);
				local depot_front = build.make_tile(x + 6, y + 1);
				local depot_tile = build.make_tile(x + 6, y + 2);
				local portal_a = build.make_tile(x + 7, y);

				report.check_true("surface_left", build.build_surface_x(x + 2, x + 6, y) &&
					GSRail.BuildRail(build.make_tile(x + 5, y), depot_junction, depot_front) &&
					GSRail.BuildRailTrack(depot_junction, GSRail.RAILTRACK_SW_SE), "surface");
				report.check_true("depot", GSRail.BuildRail(depot_tile, depot_front, depot_junction) && GSRail.BuildRailDepot(depot_tile, depot_front), "depot");
				report.check_true("portal", metro.build_portal(portal_a, GSMetro.DIAGDIR_SW, d), "portal");
				report.check_true("ug1", metro.build_underground_x(x + 8, x + 16, y, d), "ug1");
				report.check_true("plat_p", metro.build_station_x(build.make_tile(x + 17, y), d), "plat_p");
				local sid_p = GSMetro.GetUndergroundStationID(build.make_tile(x + 17, y), d);
				report.check_true("plat_p2", metro.build_station_x(build.make_tile(x + 18, y), d, sid_p), "plat_p2");
				report.check_true("ug2", metro.build_underground_x(x + 19, x + 20, y, d), "ug2");
				report.check_true("plat_q", metro.build_station_x(build.make_tile(x + 21, y), d), "plat_q");
				local sid_q = GSMetro.GetUndergroundStationID(build.make_tile(x + 21, y), d);
				report.check_true("plat_q2", metro.build_station_x(build.make_tile(x + 22, y), d, sid_q), "plat_q2");

				/* A new block signal faces both ways; building again turns it westbound, once more eastbound. */
				foreach (i, off in [9, 16, 19]) {
					local tile = build.make_tile(x + off, y);
					local ok = true;
					for (local n = 0; n < 3; n++) ok = ok && GSMetro.BuildUndergroundSignal(tile, GSRail.RAILTRACK_NE_SW, d, GSRail.SIGNALTYPE_NORMAL);
					report.check_true("sig" + (i + 1), ok, "block signal");
					report.check_eq("sig" + (i + 1) + "_type", GSRail.SIGNALTYPE_NORMAL, GSMetro.GetUndergroundSignalType(tile, GSRail.RAILTRACK_NE_SW, d), "block signal type");
					report.check_true("sig" + (i + 1) + "_oneway", !GSMetro.IsUndergroundSignalTwoWay(tile, GSRail.RAILTRACK_NE_SW, d), "turned to one way");
				}

				local tv = [];
				for (local i = 0; i < 3; i++) {
					local v = trains.build_train(depot_tile, engine);
					report.check_true("train" + i, v != GSVehicle.VEHICLE_INVALID, "train");
					if (i == 0) report.check_true("wagon", trains.attach_wagon(v, depot_tile, wagon).attached, "wagon");
					trains.append_underground_order(v, sid_p, GSOrder.OF_NON_STOP_INTERMEDIATE | GSOrder.OF_FULL_LOAD_ANY);
					trains.append_underground_order(v, sid_q, GSOrder.OF_NON_STOP_INTERMEDIATE | GSOrder.OF_FULL_LOAD_ANY);
					tv.append(v);
				}
				GSVehicle.StartStopVehicle(tv[0]);
				local t0 = trains.wait_for_underground_station(tv[0], sid_p, 8000);
				report.metric("t0_at_station", t0 == null ? -1 : t0);
				GSVehicle.StartStopVehicle(tv[1]);
				GSController.Sleep(600);
				GSVehicle.StartStopVehicle(tv[2]);

				local xoff = function(v) : (x) {
					return GSMetro.IsVehicleUnderground(v) ? GSMap.GetTileX(GSMetro.GetVehicleUndergroundTile(v)) - x : GSMap.GetTileX(GSVehicle.GetLocation(v)) - x;
				};
				local crashed = function() : (tv) {
					local c = 0;
					foreach (v in tv) if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) c++;
					return c;
				};

				/* Phase 1: held. */
				local max1 = -1, max2 = -1;
				for (local t = 0; t < 5000; t += 10) {
					if (crashed() > 0) break;
					local a = xoff(tv[1]), b = xoff(tv[2]);
					if (a > max1) max1 = a;
					if (b > max2) max2 = b;
					GSController.Sleep(10);
				}
				report.metric("held_train1_max_x_offset", max1);
				report.metric("held_train2_max_x_offset", max2);
				report.check_eq("no_crash_held", 0, crashed(), "No train may crash.");
				report.check_eq("train1_held_at_s2", 16, max1, "The second train waits at the block signal before the occupied platform.");
				report.check_eq("train2_held_at_s1", 9, max2, "The third train waits at the first block signal while the block ahead is occupied.");

				/* Phase 2: release train 0 to Q. */
				report.check_true("release", GSOrder.SetOrderFlags(tv[0], 0, GSOrder.OF_NON_STOP_INTERMEDIATE), "release order");
				local max0 = -1;
				max1 = -1;
				max2 = -1;
				for (local t = 0; t < 8000; t += 10) {
					if (crashed() > 0) break;
					local z = xoff(tv[0]), a = xoff(tv[1]), b = xoff(tv[2]);
					if (z > max0) max0 = z;
					if (a > max1) max1 = a;
					if (b > max2) max2 = b;
					GSController.Sleep(10);
				}
				report.metric("released_train0_max_x_offset", max0);
				report.metric("released_train1_max_x_offset", max1);
				report.metric("released_train2_max_x_offset", max2);
				report.check_eq("no_crash_released", 0, crashed(), "No train may crash.");
				report.check_true("train0_to_q", max0 >= 21, "The first train moves on to the second station.");
				report.check_true("train1_released", max1 >= 18, "The second train enters the platform once its block is free.");
				report.check_true("train2_released", max2 >= 16, "The third train follows up to the next block signal.");
				cm = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
