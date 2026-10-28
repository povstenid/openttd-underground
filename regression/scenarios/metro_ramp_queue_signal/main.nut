/*
 * metro_ramp_queue_signal
 *
 *   surface -- P(D1) -- S> (D1) -- ramp D1->D2 -- ramp D2->D3 -- [U] (D3)
 *
 * Train A (long, full load) stands with its front on the one-tile platform U
 * and the rest on the two ramps. Train B follows from the surface. Ramps carried
 * no reservation: the signal S in front of them showed green and B drove into
 * A on the ramps.
 */
class MetroRampQueueSignal extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_ramp_queue_signal", null, 510008);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();
		try {
			local area = build.find_buildable_rectangle(24, 8);
			local company = companies.ensure_company();
			local cm = GSCompanyMode(company);
			local engine = trains.find_rail_engine();
			local wagon = trains.find_rail_wagon();
			local x = build.tile_x(area), y = build.tile_y(area) + 3;
			local h = GSTile.GetMinHeight(build.make_tile(x + 9, y));
			local d1 = -1, d2 = -2, d3 = -3; report.metric("h", h);
			local T = function(ax, ay) : (build) { return build.make_tile(ax, ay); };
			local depot = T(x + 4, y + 2), front = T(x + 4, y + 1), junction = T(x + 4, y);
			report.check_true("surface", build.build_surface_x(x + 2, x + 8, y) && GSRail.BuildRail(T(x + 3, y), junction, front) && GSRail.BuildRailTrack(junction, GSRail.RAILTRACK_SW_SE), "surface");
			report.check_true("depot", GSRail.BuildRail(depot, front, junction) && GSRail.BuildRailDepot(depot, front), "depot");
			report.check_true("signal", GSRail.BuildSignal(T(x + 6, y), T(x + 5, y), GSRail.SIGNALTYPE_NORMAL), "signal " + GSError.GetLastErrorString());
			report.check_true("portal", GSMetro.BuildPortal(T(x + 9, y), GSMetro.DIAGDIR_SW, d1), "portal " + GSError.GetLastErrorString());
			report.check_true("ug", GSMetro.BuildUndergroundRail(T(x + 10, y), GSRail.RAILTRACK_NE_SW, d1), "ug " + GSError.GetLastErrorString());
			report.check_true("ug_signal", GSMetro.BuildUndergroundSignal(T(x + 10, y), GSRail.RAILTRACK_NE_SW, d1) && GSMetro.BuildUndergroundSignal(T(x + 10, y), GSRail.RAILTRACK_NE_SW, d1), "ug signal " + GSError.GetLastErrorString());
			report.check_true("ramp1", GSMetro.BuildRamp(T(x + 11, y), GSMetro.DIAGDIR_SW, d1, d2), "ramp1 " + GSError.GetLastErrorString());
			report.check_true("ramp2", GSMetro.BuildRamp(T(x + 12, y), GSMetro.DIAGDIR_SW, d2, d3), "ramp2 " + GSError.GetLastErrorString());
			report.check_true("platform", GSMetro.BuildUndergroundStation(T(x + 13, y), GSRail.RAILTRACK_NE_SW, d3, GSStation.STATION_NEW), "platform " + GSError.GetLastErrorString());
			local su = GSMetro.GetUndergroundStationID(T(x + 13, y), d3);
			local a_train = trains.build_train(depot, engine);
			for (local i = 0; i < 4; i++) trains.attach_wagon(a_train, depot, wagon);
			local b_train = trains.build_train(depot, engine);
			trains.append_underground_order(a_train, su, GSOrder.OF_NON_STOP_INTERMEDIATE | GSOrder.OF_FULL_LOAD_ANY);
			trains.append_underground_order(b_train, su, GSOrder.OF_NON_STOP_INTERMEDIATE);
			GSVehicle.StartStopVehicle(a_train);
			local t = 0;
			while (t < 4000 && GSVehicle.GetState(a_train) != GSVehicle.VS_AT_STATION) { GSController.Sleep(10); t += 10; }
			report.metric("a_at_station_tick", t);
			report.metric("a_length", GSVehicle.GetLength(a_train));
			GSController.Sleep(100);
			GSVehicle.StartStopVehicle(b_train);
			local crashed = 0, b_max = -99;
			for (local i = 0; i < 4000; i += 10) {
				foreach (v in [a_train, b_train]) if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) crashed = 1;
				if (crashed) break;
				local bx = GSMap.GetTileX(GSMetro.IsVehicleUnderground(b_train) ? GSMetro.GetVehicleUndergroundTile(b_train) : GSVehicle.GetLocation(b_train)) - x;
				if (bx > b_max) b_max = bx;
				GSController.Sleep(10);
			}
			report.metric("crashed", crashed);
			report.metric("b_max_x", b_max);
			report.check_eq("no_crash", 0, crashed, "B must not drive into A standing on the ramps.");
			report.check_true("b_waits_at_signal", b_max <= 10, "B waits at the signal in front of the ramps (b_max_x=" + b_max + ").");
			cm = null;
		} catch (e) { report.check_true("exc", false, e); }
		report.finish();
	}
}
