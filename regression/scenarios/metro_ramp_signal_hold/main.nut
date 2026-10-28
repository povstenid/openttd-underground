/*
 * metro_ramp_signal_hold
 *
 *   surface -- P(D1) -- (D1) -- R1 D1->D2 with signal -- (D2) -- (D2) -- R2 D2->D3 -- [U] (D3)
 *
 * The only underground signal stands on the ramp R1 (a block signal, built by
 * naming the ramp's lower level). Train A (long, full load) stands with its
 * front on the one-tile platform U and the rest on R2 and the track beyond R1.
 * Train B follows from the surface: it must come down to R1 and wait at its
 * signal, the block beyond it being occupied, instead of driving into A.
 */
class MetroRampSignalHold extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_ramp_signal_hold", null, 510008);
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
			local d1 = -1, d2 = -2, d3 = -3;
			local track = GSRail.RAILTRACK_NE_SW;
			local T = function(ax, ay) : (build) { return build.make_tile(ax, ay); };
			local depot = T(x + 4, y + 2), front = T(x + 4, y + 1), junction = T(x + 4, y);
			report.check_true("surface", build.build_surface_x(x + 2, x + 8, y) && GSRail.BuildRail(T(x + 3, y), junction, front) && GSRail.BuildRailTrack(junction, GSRail.RAILTRACK_SW_SE), "surface");
			report.check_true("depot", GSRail.BuildRail(depot, front, junction) && GSRail.BuildRailDepot(depot, front), "depot");
			report.check_true("signal", GSRail.BuildSignal(T(x + 6, y), T(x + 5, y), GSRail.SIGNALTYPE_NORMAL), "signal " + GSError.GetLastErrorString());
			report.check_true("portal", GSMetro.BuildPortal(T(x + 9, y), GSMetro.DIAGDIR_SW, d1), "portal " + GSError.GetLastErrorString());
			report.check_true("ug1", GSMetro.BuildUndergroundRail(T(x + 10, y), track, d1), "ug1 " + GSError.GetLastErrorString());
			report.check_true("ramp1", GSMetro.BuildRamp(T(x + 11, y), GSMetro.DIAGDIR_SW, d1, d2), "ramp1 " + GSError.GetLastErrorString());
			report.check_true("ug2", metro.build_underground_x(x + 12, x + 13, y, d2), "ug2 " + GSError.GetLastErrorString());
			report.check_true("ramp2", GSMetro.BuildRamp(T(x + 14, y), GSMetro.DIAGDIR_SW, d2, d3), "ramp2 " + GSError.GetLastErrorString());
			report.check_true("platform", GSMetro.BuildUndergroundStation(T(x + 15, y), track, d3, GSStation.STATION_NEW), "platform " + GSError.GetLastErrorString());

			/* A ramp takes signals on its one track only, named by either of its levels. */
			report.check_true("ramp_wrong_track", !GSMetro.BuildUndergroundSignal(T(x + 11, y), GSRail.RAILTRACK_NW_SE, d2, GSRail.SIGNALTYPE_NORMAL), "no signal across the ramp");
			report.check_true("ramp_signal", GSMetro.BuildUndergroundSignal(T(x + 11, y), track, d2, GSRail.SIGNALTYPE_NORMAL), "ramp signal " + GSError.GetLastErrorString());
			report.check_eq("ramp_signal_type_lower", GSRail.SIGNALTYPE_NORMAL, GSMetro.GetUndergroundSignalType(T(x + 11, y), track, d2), "type at the lower level");
			report.check_eq("ramp_signal_type_upper", GSRail.SIGNALTYPE_NORMAL, GSMetro.GetUndergroundSignalType(T(x + 11, y), track, d1), "type at the upper level");
			report.check_true("ramp_signal_twoway", GSMetro.IsUndergroundSignalTwoWay(T(x + 11, y), track, d1), "a new block signal faces both ways");

			local su = GSMetro.GetUndergroundStationID(T(x + 15, y), d3);
			local a_train = trains.build_train(depot, engine);
			for (local i = 0; i < 4; i++) trains.attach_wagon(a_train, depot, wagon);
			local b_train = trains.build_train(depot, engine);
			trains.append_underground_order(a_train, su, GSOrder.OF_NON_STOP_INTERMEDIATE | GSOrder.OF_FULL_LOAD_ANY);
			trains.append_underground_order(b_train, su, GSOrder.OF_NON_STOP_INTERMEDIATE);
			GSVehicle.StartStopVehicle(a_train);
			local t = 0;
			while (t < 4000 && GSVehicle.GetState(a_train) != GSVehicle.VS_AT_STATION) { GSController.Sleep(10); t += 10; }
			report.metric("a_at_station_tick", t);
			report.check_true("a_arrived", GSVehicle.GetState(a_train) == GSVehicle.VS_AT_STATION, "A passes the ramp signal and reaches the platform");
			GSController.Sleep(100);
			GSVehicle.StartStopVehicle(b_train);
			local crashed = 0, b_max = -99, b_end = -99;
			for (local i = 0; i < 4000; i += 10) {
				foreach (v in [a_train, b_train]) if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) crashed = 1;
				if (crashed) break;
				b_end = GSMap.GetTileX(GSMetro.IsVehicleUnderground(b_train) ? GSMetro.GetVehicleUndergroundTile(b_train) : GSVehicle.GetLocation(b_train)) - x;
				if (b_end > b_max) b_max = b_end;
				GSController.Sleep(10);
			}
			report.metric("crashed", crashed);
			report.metric("b_max_x", b_max);
			report.metric("b_end_x", b_end);
			report.check_eq("no_crash", 0, crashed, "B must not drive into A beyond the ramp signal.");
			report.check_eq("b_waits_at_ramp_signal", 11, b_max, "B comes down to the ramp and waits at its signal.");
			report.check_eq("b_stays_at_ramp_signal", 11, b_end, "B still waits at the ramp signal.");
			cm = null;
		} catch (e) { report.check_true("exc", false, e); }
		report.finish();
	}
}
