/**
 * metro_signal_presignal_terminus
 *
 * An entry pre-signal in front of a junction to two terminus platforms, each
 * behind an exit pre-signal, like the classic surface terminus:
 *
 *                                   X1(x+13) [P1 P1]     (lane y)
 *   depot -- P(SW) -- x+8 .. E(x+11) J(x+12)
 *                                   X2(x+13) [P2 P2]     (lane y + 1)
 *
 * Trains (engine + wagon, full load, no cargo anywhere) stay in the platform
 * they reach. The first two must take different platforms (the pathfinder
 * avoids the red exit signal); the third must wait at the entry signal, which
 * is red while both exits are red, instead of entering the junction.
 */
class MetroSignalPresignalTerminus extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_signal_presignal_terminus", null, 510008);
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
				local entry = build.make_tile(x + 11, y);
				local exits = [build.make_tile(x + 13, y), build.make_tile(x + 13, y + 1)];

				report.check_true("surface_left", build.build_surface_x(x + 2, x + 6, y) &&
					GSRail.BuildRail(build.make_tile(x + 5, y), depot_junction, depot_front) &&
					GSRail.BuildRailTrack(depot_junction, GSRail.RAILTRACK_SW_SE), "surface");
				report.check_true("depot", GSRail.BuildRail(depot_tile, depot_front, depot_junction) && GSRail.BuildRailDepot(depot_tile, depot_front), "depot");
				report.check_true("portal", metro.build_portal(portal_a, GSMetro.DIAGDIR_SW, d), "portal");
				report.check_true("ug_line", metro.build_underground_x(x + 8, x + 13, y, d), "line");
				/* The junction: lane y turns off to lane y + 1. */
				report.check_true("junction", GSMetro.BuildUndergroundRail(build.make_tile(x + 12, y), GSRail.RAILTRACK_NE_SE, d) &&
					GSMetro.BuildUndergroundRail(build.make_tile(x + 12, y + 1), GSRail.RAILTRACK_NW_SW, d) &&
					GSMetro.BuildUndergroundRail(build.make_tile(x + 13, y + 1), GSRail.RAILTRACK_NE_SW, d), "junction");
				local sid = null;
				foreach (lane in [0, 1]) {
					foreach (off in [14, 15]) {
						local tile = build.make_tile(x + off, y + lane);
						report.check_true("plat_" + lane + "_" + off, metro.build_station_x(tile, d, sid == null ? GSStation.STATION_NEW : sid), "platform");
						if (sid == null) sid = GSMetro.GetUndergroundStationID(tile, d);
					}
				}
				report.check_true("sid", GSStation.IsValidStation(sid), "sid");

				/* Pre-signals face both ways when built; turn them twice to face east only. */
				local one_way_east = function(tile, type) : (d) {
					local ok = true;
					for (local n = 0; n < 3; n++) ok = ok && GSMetro.BuildUndergroundSignal(tile, GSRail.RAILTRACK_NE_SW, d, type);
					return ok;
				};
				report.check_true("entry_sig", one_way_east(entry, GSRail.SIGNALTYPE_ENTRY), "entry signal");
				report.check_eq("entry_sig_type", GSRail.SIGNALTYPE_ENTRY, GSMetro.GetUndergroundSignalType(entry, GSRail.RAILTRACK_NE_SW, d), "entry type");
				foreach (i, tile in exits) {
					report.check_true("exit_sig" + i, one_way_east(tile, GSRail.SIGNALTYPE_EXIT), "exit signal");
					report.check_eq("exit_sig" + i + "_type", GSRail.SIGNALTYPE_EXIT, GSMetro.GetUndergroundSignalType(tile, GSRail.RAILTRACK_NE_SW, d), "exit type");
				}

				local tv = [];
				for (local i = 0; i < 3; i++) {
					local v = trains.build_train(depot_tile, engine);
					report.check_true("train" + i, v != GSVehicle.VEHICLE_INVALID, "train");
					report.check_true("wagon" + i, trains.attach_wagon(v, depot_tile, wagon).attached, "wagon");
					trains.append_underground_order(v, sid, GSOrder.OF_NON_STOP_INTERMEDIATE | GSOrder.OF_FULL_LOAD_ANY);
					tv.append(v);
				}
				local crashed = function() : (tv) {
					local c = 0;
					foreach (v in tv) if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) c++;
					return c;
				};

				local lanes = [];
				for (local i = 0; i < 2; i++) {
					GSVehicle.StartStopVehicle(tv[i]);
					local t = trains.wait_for_underground_station(tv[i], sid, 8000);
					report.metric("train" + i + "_at_station", t == null ? -1 : t);
					local tile = GSMetro.GetVehicleUndergroundTile(tv[i]);
					lanes.append(GSMap.GetTileY(tile) - y);
					report.metric("train" + i + "_lane", lanes[i]);
				}
				report.check_true("different_platforms", lanes[0] != lanes[1], "The second train takes the free platform.");

				GSVehicle.StartStopVehicle(tv[2]);
				local max2 = -1;
				for (local t = 0; t < 5000; t += 10) {
					if (crashed() > 0) break;
					if (GSMetro.IsVehicleUnderground(tv[2])) {
						local xx = GSMap.GetTileX(GSMetro.GetVehicleUndergroundTile(tv[2])) - x;
						if (xx > max2) max2 = xx;
					}
					GSController.Sleep(10);
				}
				report.metric("train2_max_x_offset", max2);
				report.check_eq("no_crash", 0, crashed(), "No train may crash.");
				report.check_eq("train2_waits_at_entry", 11, max2, "With both exit signals red the entry signal is red: the third train waits in front of the junction.");
				cm = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
