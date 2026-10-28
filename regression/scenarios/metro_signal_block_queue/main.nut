/* Probe: three engine-only trains, one signalled line into a 1-tile platform.
 *   depot -- P(SW) -- S1(x+9) ---- S2(x+17) [plat x+18] -- x+19..x+21
 * Train 1 fills the platform (full load, no cargo => stays). Train 2 must wait
 * at S1 or S2 with a reservation; train 3 must wait behind. Any crash = bug. */
class MetroSignalBlockQueue extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_signal_block_queue", null, 510008);
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
			report.check_true("engine", engine != null, "engine");
			if (area != null && engine != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 3;
				local d = -1;
				local depot_junction = build.make_tile(x + 6, y);
				local depot_front = build.make_tile(x + 6, y + 1);
				local depot_tile = build.make_tile(x + 6, y + 2);
				local portal_a = build.make_tile(x + 7, y);
				local s1 = build.make_tile(x + 9, y);
				local s2 = build.make_tile(x + 17, y);
				local plat = build.make_tile(x + 18, y);

				report.check_true("surface_left", build.build_surface_x(x + 2, x + 6, y) &&
					GSRail.BuildRail(build.make_tile(x + 5, y), depot_junction, depot_front) &&
					GSRail.BuildRailTrack(depot_junction, GSRail.RAILTRACK_SW_SE), "surface");
				report.check_true("depot", GSRail.BuildRail(depot_tile, depot_front, depot_junction) && GSRail.BuildRailDepot(depot_tile, depot_front), "depot");
				report.check_true("portal", metro.build_portal(portal_a, GSMetro.DIAGDIR_SW, d), "portal");
				report.check_true("ug1", metro.build_underground_x(x + 8, x + 17, y, d), "ug1");
				report.check_true("plat", metro.build_station_x(plat, d), "plat");
				report.check_true("ug2", metro.build_underground_x(x + 19, x + 21, y, d), "ug2");
				/* Default signal direction is westbound; build twice for eastbound (+x). */
				report.check_true("sig1", GSMetro.BuildUndergroundSignal(s1, GSRail.RAILTRACK_NE_SW, d) && GSMetro.BuildUndergroundSignal(s1, GSRail.RAILTRACK_NE_SW, d), "s1");
				report.check_true("sig2", GSMetro.BuildUndergroundSignal(s2, GSRail.RAILTRACK_NE_SW, d) && GSMetro.BuildUndergroundSignal(s2, GSRail.RAILTRACK_NE_SW, d), "s2");
				local sid = GSMetro.GetUndergroundStationID(plat, d);
				report.check_true("sid", GSStation.IsValidStation(sid), "sid");

				local tv = [];
				for (local i = 0; i < 3; i++) {
					local v = trains.build_train(depot_tile, engine); if (i == 0) { local w = trains.find_rail_wagon(); report.metric("wagon_attached", trains.attach_wagon(v, depot_tile, w) ? 1 : 0); }
					report.check_true("train" + i, v != GSVehicle.VEHICLE_INVALID, "train");
					trains.append_underground_order(v, sid, GSOrder.OF_NON_STOP_INTERMEDIATE | GSOrder.OF_FULL_LOAD_ANY);
					tv.append(v);
				}
				GSVehicle.StartStopVehicle(tv[0]);
				local t0 = trains.wait_for_underground_station(tv[0], sid, 8000);
				report.metric("t0_at_station", t0 == null ? -1 : t0);
				GSVehicle.StartStopVehicle(tv[1]);
				GSController.Sleep(600);
				GSVehicle.StartStopVehicle(tv[2]);

				local max1 = -1, max2 = -1, crashed = 0, crash_tick = -1;
				for (local t = 0; t < 6000; t += 10) {
					foreach (i, v in tv) {
						if (!GSVehicle.IsValidVehicle(v)) continue;
						local xx = GSMetro.IsVehicleUnderground(v) ? GSMap.GetTileX(GSMetro.GetVehicleUndergroundTile(v)) - x : GSMap.GetTileX(GSVehicle.GetLocation(v)) - x;
						if (i == 1 && xx > max1) max1 = xx;
						if (i == 2 && xx > max2) max2 = xx;
					}
					local c = 0;
					foreach (v in tv) if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) c++;
					if (c > 0 && crash_tick < 0) crash_tick = t;
					crashed = c;
					GSController.Sleep(10);
				}
				report.metric("train1_max_x_offset", max1);
				report.metric("train2_max_x_offset", max2);
				report.metric("crashed_trains", crashed);
				report.metric("crash_tick", crash_tick);
				report.check_eq("no_crash", 0, crashed, "No train may crash: the third train must wait at the first signal.");
				report.check_eq("second_train_waits_at_s2", 17, max1, "The second train waits at the signal before the occupied platform.");
				report.check_eq("third_train_waits_at_s1", 9, max2, "The third train must not pass the first signal while the block behind it is held.");
				report.metric("s1_offset", 9);
				report.metric("s2_offset", 17);
				cm = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
