/* Probe: a line that is no loop. One train shuttles between a through
 * platform A and a terminus B on a single underground track:
 *   depot -- P(SW) -- x+8..x+10 -- [A x+11..x+12] -- x+13..x+16 -- [B x+17..x+18] |
 * Orders A, B. At A with B behind it the train must turn round there (not
 * wander out through the portal); at B it reverses at the buffer stop. */
class MetroShuttleNoLoop extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_shuttle_no_loop", null, 510008);
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

				report.check_true("surface_left", build.build_surface_x(x + 2, x + 6, y) &&
					GSRail.BuildRail(build.make_tile(x + 5, y), depot_junction, depot_front) &&
					GSRail.BuildRailTrack(depot_junction, GSRail.RAILTRACK_SW_SE), "surface");
				report.check_true("depot", GSRail.BuildRail(depot_tile, depot_front, depot_junction) && GSRail.BuildRailDepot(depot_tile, depot_front), "depot");
				report.check_true("portal", metro.build_portal(portal_a, GSMetro.DIAGDIR_SW, d), "portal");
				report.check_true("ug1", metro.build_underground_x(x + 8, x + 10, y, d), "ug1");
				local sa = null;
				for (local a = x + 11; a <= x + 12; a++) {
					if (!GSMetro.BuildUndergroundStation(build.make_tile(a, y), GSRail.RAILTRACK_NE_SW, d, sa == null ? GSStation.STATION_NEW : sa)) report.check_true("platA", false, "platA");
					if (sa == null) sa = GSMetro.GetUndergroundStationID(build.make_tile(a, y), d);
				}
				report.check_true("ug2", metro.build_underground_x(x + 13, x + 16, y, d), "ug2");
				local sb = null;
				for (local a = x + 17; a <= x + 18; a++) {
					if (!GSMetro.BuildUndergroundStation(build.make_tile(a, y), GSRail.RAILTRACK_NE_SW, d, sb == null ? GSStation.STATION_NEW : sb)) report.check_true("platB", false, "platB");
					if (sb == null) sb = GSMetro.GetUndergroundStationID(build.make_tile(a, y), d);
				}
				report.check_true("sids", GSStation.IsValidStation(sa) && GSStation.IsValidStation(sb), "sids");

				local v = trains.build_train(depot_tile, engine);
				report.check_true("train", v != GSVehicle.VEHICLE_INVALID, "train");
				trains.append_underground_order(v, sa, GSOrder.OF_NON_STOP_INTERMEDIATE);
				trains.append_underground_order(v, sb, GSOrder.OF_NON_STOP_INTERMEDIATE);
				GSVehicle.StartStopVehicle(v);

				local visits = 0, was = false, surfaced_after_entry = 0, entered = false, crashed = 0;
				local seq = "";
				for (local t = 0; t < 20000; t += 10) {
					if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) { crashed = 1; break; }
					local ug = GSMetro.IsVehicleUnderground(v);
					if (ug) entered = true;
					if (entered && !ug) surfaced_after_entry++;
					local at = GSVehicle.GetState(v) == GSVehicle.VS_AT_STATION && ug;
					if (at && !was) {
						visits++;
						local o = GSOrder.ResolveOrderPosition(v, GSOrder.ORDER_CURRENT);
						seq += (GSMap.GetTileX(GSMetro.GetVehicleUndergroundTile(v)) - x) + "/o" + o + "/d" + GSVehicle.GetState(v) + "@" + t + ":";
					}
					was = at;
					GSController.Sleep(10);
				}
				report.metric("visits", visits);
				report.metric("visit_positions", seq);
				report.metric("ticks_on_surface_after_entry", surfaced_after_entry * 10);
				report.metric("crashed", crashed);
				report.check_eq("no_crash", 0, crashed, "The shuttle must not crash.");
				report.check_true("keeps_shuttling", visits >= 6, "The train must keep calling at both platforms (visits=" + visits + ").");
				report.check_eq("never_surfaces", 0, surfaced_after_entry, "The train must turn round at the through platform, not run out through the portal.");
				cm = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
