/**
 * metro_signal_remove
 *
 * Signals are removed again from underground track and from a ramp between
 * levels without demolishing either, while a train shuttles across them:
 *
 *   depot -- P(SW, D1) -- x+8..x+9 [M M] x+12(A) x+13(B) x+14 R D1->D2 (R) -- x+15 (D2) -- [T T T]
 *
 * A, B and R are two-way block signals. A goes with a single removal, R with
 * a single removal naming the ramp's upper level, B with a drag removal over
 * the line from M to the ramp. The track and the ramp stay, the signal
 * infrastructure count drops back and the train keeps running M - T - M ...
 * Last, a spare line is partly removed by a drag (Ctrl+drag with the rail tool).
 */
class MetroSignalRemove extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_signal_remove", null, 510008);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();
		try {
			local area = build.find_buildable_rectangle(30, 8);
			report.check_true("build_area_found", area != null, "area");
			local company = companies.ensure_company();
			local cm = GSCompanyMode(company);
			local engine = trains.find_rail_engine();
			report.check_true("engine", engine != null, "engine");
			local x = build.tile_x(area), y = build.tile_y(area) + 3;
			local d1 = -1, d2 = -2;
			local track = GSRail.RAILTRACK_NE_SW;
			local T = function(ax, ay) : (build) { return build.make_tile(ax, ay); };
			local depot_junction = T(x + 6, y), depot_front = T(x + 6, y + 1), depot_tile = T(x + 6, y + 2);
			local sig_a = T(x + 12, y), sig_b = T(x + 13, y), ramp = T(x + 14, y);

			report.check_true("surface", build.build_surface_x(x + 2, x + 6, y) &&
				GSRail.BuildRail(T(x + 5, y), depot_junction, depot_front) &&
				GSRail.BuildRailTrack(depot_junction, GSRail.RAILTRACK_SW_SE), "surface");
			report.check_true("depot", GSRail.BuildRail(depot_tile, depot_front, depot_junction) && GSRail.BuildRailDepot(depot_tile, depot_front), "depot");
			report.check_true("portal", metro.build_portal(T(x + 7, y), GSMetro.DIAGDIR_SW, d1), "portal " + GSError.GetLastErrorString());
			report.check_true("ug1", metro.build_underground_x(x + 8, x + 9, y, d1), "ug1 " + GSError.GetLastErrorString());
			report.check_true("plat_m", metro.build_station_x(T(x + 10, y), d1), "plat_m " + GSError.GetLastErrorString());
			local sid_m = GSMetro.GetUndergroundStationID(T(x + 10, y), d1);
			report.check_true("plat_m2", metro.build_station_x(T(x + 11, y), d1, sid_m), "plat_m2 " + GSError.GetLastErrorString());
			report.check_true("ug2", metro.build_underground_x(x + 12, x + 13, y, d1), "ug2 " + GSError.GetLastErrorString());
			report.check_true("ramp", GSMetro.BuildRamp(ramp, GSMetro.DIAGDIR_SW, d1, d2), "ramp " + GSError.GetLastErrorString());
			report.check_true("ug3", GSMetro.BuildUndergroundRail(T(x + 15, y), track, d2), "ug3 " + GSError.GetLastErrorString());
			report.check_true("plat_t", metro.build_station_x(T(x + 16, y), d2), "plat_t " + GSError.GetLastErrorString());
			local sid_t = GSMetro.GetUndergroundStationID(T(x + 16, y), d2);
			report.check_true("plat_t23", metro.build_station_x(T(x + 17, y), d2, sid_t) && metro.build_station_x(T(x + 18, y), d2, sid_t), "plat_t23");

			local signals_before = GSInfrastructure.GetInfrastructurePieceCount(company, GSInfrastructure.INFRASTRUCTURE_SIGNALS);
			report.check_true("sig_a", GSMetro.BuildUndergroundSignal(sig_a, track, d1, GSRail.SIGNALTYPE_NORMAL), "A " + GSError.GetLastErrorString());
			report.check_true("sig_b", GSMetro.BuildUndergroundSignal(sig_b, track, d1, GSRail.SIGNALTYPE_NORMAL), "B " + GSError.GetLastErrorString());
			report.check_true("sig_ramp", GSMetro.BuildUndergroundSignal(ramp, track, d2, GSRail.SIGNALTYPE_NORMAL), "R " + GSError.GetLastErrorString());
			report.check_true("sig_ramp_upper", GSMetro.HasUndergroundSignal(ramp, track, d1), "The ramp signal shows at its upper level too.");
			local signals_built = GSInfrastructure.GetInfrastructurePieceCount(company, GSInfrastructure.INFRASTRUCTURE_SIGNALS);
			report.metric("signals_before", signals_before);
			report.metric("signals_built", signals_built);
			report.check_true("signals_counted", signals_built > signals_before, "Building signals adds signal infrastructure.");

			local v = trains.build_train(depot_tile, engine);
			report.check_true("train", v != GSVehicle.VEHICLE_INVALID, "train");
			trains.append_underground_order(v, sid_m, GSOrder.OF_NON_STOP_INTERMEDIATE);
			trains.append_underground_order(v, sid_t, GSOrder.OF_NON_STOP_INTERMEDIATE);
			GSVehicle.StartStopVehicle(v);

			/* Run the train until it has reached T a number of times; order T done means it arrived there. */
			local run = function(arrivals, max_ticks) : (v) {
				local count = 0, prev = -1, crashed = false;
				for (local t = 0; t < max_ticks && count < arrivals; t += 10) {
					if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) { crashed = true; break; }
					local o = GSOrder.ResolveOrderPosition(v, GSOrder.ORDER_CURRENT);
					if (prev == 1 && o == 0) count++;
					prev = o;
					GSController.Sleep(10);
				}
				return { arrivals = count, crashed = crashed };
			};
			local before = run(1, 8000);
			report.metric("arrivals_before_removal", before.arrivals);
			report.check_true("runs_with_signals", !before.crashed && before.arrivals >= 1, "The train crosses the signals to T.");

			/* A train may hold the signal's track for a moment; retry like a player would. */
			local retry = function(action) {
				for (local i = 0; i < 400; i++) {
					if (action()) return true;
					if (GSError.GetLastError() != GSError.ERR_VEHICLE_IN_THE_WAY) return false;
					GSController.Sleep(5);
				}
				return false;
			};

			report.check_true("remove_a", retry(function() : (sig_a, track, d1) { return GSMetro.RemoveUndergroundSignal(sig_a, track, d1); }), "remove A " + GSError.GetLastErrorString());
			report.check_true("remove_ramp", retry(function() : (ramp, track, d1) { return GSMetro.RemoveUndergroundSignal(ramp, track, d1); }), "remove R " + GSError.GetLastErrorString());
			report.check_true("remove_line", retry(function() : (T, x, y, track, d1) { return GSMetro.RemoveUndergroundSignalTrack(T(x + 10, y), T(x + 14, y), track, d1); }), "remove line " + GSError.GetLastErrorString());

			report.check_true("a_gone", !GSMetro.HasUndergroundSignal(sig_a, track, d1), "A is removed.");
			report.check_true("b_gone", !GSMetro.HasUndergroundSignal(sig_b, track, d1), "B is removed by the drag.");
			report.check_true("ramp_signal_gone", !GSMetro.HasUndergroundSignal(ramp, track, d1) && !GSMetro.HasUndergroundSignal(ramp, track, d2), "R is removed at both levels.");
			report.check_eq("a_track_stays", GSMetro.TK_TRACK, GSMetro.GetUndergroundTileKind(sig_a, d1), "The track of A stays.");
			report.check_eq("b_track_stays", GSMetro.TK_TRACK, GSMetro.GetUndergroundTileKind(sig_b, d1), "The track of B stays.");
			report.check_eq("ramp_stays", GSMetro.TK_RAMP, GSMetro.GetUndergroundTileKind(ramp, d1), "The ramp stays.");
			report.check_true("remove_again_fails", !GSMetro.RemoveUndergroundSignal(sig_a, track, d1), "No signal left to remove at A.");
			report.check_true("remove_line_again_fails", !GSMetro.RemoveUndergroundSignalTrack(T(x + 10, y), T(x + 14, y), track, d1), "No signal left on the line.");
			local signals_after = GSInfrastructure.GetInfrastructurePieceCount(company, GSInfrastructure.INFRASTRUCTURE_SIGNALS);
			report.metric("signals_after", signals_after);
			report.check_eq("signals_uncounted", signals_before, signals_after, "Removing the signals takes back their infrastructure.");

			local after = run(2, 12000);
			report.metric("arrivals_after_removal", after.arrivals);
			report.check_true("not_crashed", !before.crashed && !after.crashed, "The train must not crash.");
			report.check_true("runs_without_signals", after.arrivals >= 2, "The train keeps running over the track and the ramp without signals.");

			/* Signals can be built again where they were removed. */
			report.check_true("rebuild_ramp_signal", retry(function() : (ramp, track, d2) { return GSMetro.BuildUndergroundSignal(ramp, track, d2, GSRail.SIGNALTYPE_NORMAL); }), "rebuild R " + GSError.GetLastErrorString());

			/* Removing track by dragging (Ctrl+drag with the rail tool) on a spare line. */
			local ys = y + 3;
			report.check_true("spare_line", GSMetro.BuildUndergroundRailTrack(T(x + 10, ys), T(x + 16, ys), track, d1), "spare line " + GSError.GetLastErrorString());
			report.check_true("spare_signal", GSMetro.BuildUndergroundSignal(T(x + 12, ys), track, d1, GSRail.SIGNALTYPE_NORMAL), "spare signal " + GSError.GetLastErrorString());
			report.check_true("remove_rail_line", GSMetro.RemoveUndergroundRailTrack(T(x + 11, ys), T(x + 13, ys), track, d1), "remove rail line " + GSError.GetLastErrorString());
			report.check_eq("rail_line_start_stays", GSMetro.TK_TRACK, GSMetro.GetUndergroundTileKind(T(x + 10, ys), d1), "Track before the dragged part stays.");
			report.check_eq("rail_line_end_stays", GSMetro.TK_TRACK, GSMetro.GetUndergroundTileKind(T(x + 14, ys), d1), "Track after the dragged part stays.");
			local gone = true;
			for (local i = 11; i <= 13; i++) gone = gone && GSMetro.GetUndergroundTileKind(T(x + i, ys), d1) == GSMetro.TK_NONE;
			report.check_true("rail_line_removed", gone, "The dragged part of the line is removed, its signal with it.");
			report.check_true("remove_rail_line_again_fails", !GSMetro.RemoveUndergroundRailTrack(T(x + 11, ys), T(x + 13, ys), track, d1), "Nothing left to remove.");
			report.check_true("remove_rail_line_partial", GSMetro.RemoveUndergroundRailTrack(T(x + 12, ys), T(x + 16, ys), track, d1), "A drag over gaps removes what is there " + GSError.GetLastErrorString());
			report.check_eq("rail_line_all_gone", GSMetro.TK_NONE, GSMetro.GetUndergroundTileKind(T(x + 16, ys), d1), "The rest of the line is removed.");
			cm = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
