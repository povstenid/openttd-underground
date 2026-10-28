/**
 * metro_signal_twoway_terminus
 *
 * A terminus platform behind a two-way passable path signal (SIGTYPE_PBS):
 * a train enters the platform past the signal, turns round at the buffer stop
 * and leaves past the back of the signal, like on the surface. With only
 * one-way path signals the platform could not be left.
 *
 *   depot -- P(SW) -- x+8..x+9 [M M] x+12(two-way block) x+13 x+14(path) [T T T]
 *
 * M = through station, T = terminus. The train runs M - T - M - T ...
 */
class MetroSignalTwowayTerminus extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_signal_twoway_terminus", null, 510008);
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
				local block_sig = build.make_tile(x + 12, y);
				local path_sig = build.make_tile(x + 14, y);

				report.check_true("surface_left", build.build_surface_x(x + 2, x + 6, y) &&
					GSRail.BuildRail(build.make_tile(x + 5, y), depot_junction, depot_front) &&
					GSRail.BuildRailTrack(depot_junction, GSRail.RAILTRACK_SW_SE), "surface");
				report.check_true("depot", GSRail.BuildRail(depot_tile, depot_front, depot_junction) && GSRail.BuildRailDepot(depot_tile, depot_front), "depot");
				report.check_true("portal", metro.build_portal(portal_a, GSMetro.DIAGDIR_SW, d), "portal");
				report.check_true("ug1", metro.build_underground_x(x + 8, x + 9, y, d), "ug1");
				report.check_true("plat_m", metro.build_station_x(build.make_tile(x + 10, y), d), "plat_m");
				local sid_m = GSMetro.GetUndergroundStationID(build.make_tile(x + 10, y), d);
				report.check_true("plat_m2", metro.build_station_x(build.make_tile(x + 11, y), d, sid_m), "plat_m2");
				report.check_true("ug2", metro.build_underground_x(x + 12, x + 14, y, d), "ug2");
				report.check_true("plat_t", metro.build_station_x(build.make_tile(x + 15, y), d), "plat_t");
				local sid_t = GSMetro.GetUndergroundStationID(build.make_tile(x + 15, y), d);
				report.check_true("plat_t23", metro.build_station_x(build.make_tile(x + 16, y), d, sid_t) &&
					metro.build_station_x(build.make_tile(x + 17, y), d, sid_t), "plat_t23");

				/* A two-way block signal between the stations, passed both ways. */
				report.check_true("block_sig", GSMetro.BuildUndergroundSignal(block_sig, GSRail.RAILTRACK_NE_SW, d, GSRail.SIGNALTYPE_NORMAL), "block signal");
				report.check_eq("block_sig_type", GSRail.SIGNALTYPE_NORMAL, GSMetro.GetUndergroundSignalType(block_sig, GSRail.RAILTRACK_NE_SW, d), "block signal type");
				report.check_true("block_sig_twoway", GSMetro.IsUndergroundSignalTwoWay(block_sig, GSRail.RAILTRACK_NE_SW, d), "A new block signal faces both ways.");

				/* The path signal into the terminus: new ones face west; build again to face east (into the terminus). */
				report.check_true("path_sig", GSMetro.BuildUndergroundSignal(path_sig, GSRail.RAILTRACK_NE_SW, d, GSRail.SIGNALTYPE_PBS) &&
					GSMetro.BuildUndergroundSignal(path_sig, GSRail.RAILTRACK_NE_SW, d, GSRail.SIGNALTYPE_PBS), "path signal");
				report.check_eq("path_sig_type", GSRail.SIGNALTYPE_PBS, GSMetro.GetUndergroundSignalType(path_sig, GSRail.RAILTRACK_NE_SW, d), "path signal type");
				report.check_true("path_sig_oneside", !GSMetro.IsUndergroundSignalTwoWay(path_sig, GSRail.RAILTRACK_NE_SW, d), "A path signal faces one way.");

				local v = trains.build_train(depot_tile, engine);
				report.check_true("train", v != GSVehicle.VEHICLE_INVALID, "train");
				trains.append_underground_order(v, sid_m, GSOrder.OF_NON_STOP_INTERMEDIATE);
				trains.append_underground_order(v, sid_t, GSOrder.OF_NON_STOP_INTERMEDIATE);
				GSVehicle.StartStopVehicle(v);

				/* Count the round trips: order T done (back on order M) means the train left the terminus. */
				local left_terminus = 0, reached_terminus = 0, last = -1, crashed = false, max_x = -1;
				for (local t = 0; t < 20000; t += 10) {
					if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) { crashed = true; break; }
					local o = GSOrder.ResolveOrderPosition(v, GSOrder.ORDER_CURRENT);
					if (last == 0 && o == 1) reached_terminus++; /* left M towards T */
					if (last == 1 && o == 0) left_terminus++;    /* arrived at T, now heading back to M */
					last = o;
					if (GSMetro.IsVehicleUnderground(v)) {
						local xx = GSMap.GetTileX(GSMetro.GetVehicleUndergroundTile(v)) - x;
						if (xx > max_x) max_x = xx;
					}
					/* Done once the train has come back to M twice. */
					if (left_terminus >= 2 && o == 1) break;
					GSController.Sleep(10);
				}
				report.metric("trips_to_terminus", reached_terminus);
				report.metric("terminus_departures", left_terminus);
				report.metric("max_x_offset", max_x);
				report.check_true("not_crashed", !crashed, "The train must not crash.");
				report.check_true("reached_terminus", max_x >= 15, "The train passes the path signal into the terminus.");
				report.check_true("left_terminus_twice", left_terminus >= 2, "The train leaves the terminus past the back of the two-way passable path signal.");
				cm = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
