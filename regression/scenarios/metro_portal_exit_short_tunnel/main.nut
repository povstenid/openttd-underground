/**
 * metro_portal_exit_short_tunnel
 *
 * Two lanes, each with a short tunnel right in front of a station:
 *
 *   depot(x+1) -- x+2..x+6 -- P(x+7) ~~ x+8..x+10 ~~ P(x+11) -- x+12 -- [S2 S2] -- x+15 -- sig(x+16) -- x+17..x+19 -- [S3 S3]
 *
 * Lane 0 has a block signal at x+16, lane 1 a path signal. Train A waits for
 * a full load in S2, just past the mouth of the exit portal; train B follows it
 * through the tunnel. B is longer than the tunnel: when its front reaches the
 * exit its tail is still on the surface at the dive portal.
 *
 * Before the fix B drove out of the tunnel into A: with block signals the
 * check of the block in front of the mouth was skipped for a train with a part
 * on the surface (it would have seen itself), with path signals only the mouth
 * tile itself was checked and B came out without a path, then went on anyway.
 * Now B waits below ground until A has left, then comes out and calls at S2.
 */
class MetroPortalExitShortTunnel extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_portal_exit_short_tunnel", null, 510131);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();
		try {
			local area = build.find_buildable_rectangle(30, 10);
			report.check_true("build_area_found", area != null, "area");
			local company = companies.ensure_company();
			local cm = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			if (area != null && cm != null) report.check_true("area_level", build.level_area_min_height(area, 30, 10, 1), "level the area");
			local engine = cm == null ? null : trains.find_rail_engine();
			local wagon = cm == null ? null : trains.find_rail_wagon();
			report.check_true("engine", engine != null && wagon != null, "engine and wagon");
			if (area != null && engine != null && wagon != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local d = -1;
				local T = function(ax, ay) : (build) { return build.make_tile(ax, ay); };
				local lanes = [];
				foreach (i, sig_type in [GSRail.SIGNALTYPE_NORMAL, GSRail.SIGNALTYPE_PBS_ONEWAY]) {
					local y = build.tile_y(area) + 2 + 5 * i;
					local depot = T(x + 1, y);
					local s2 = T(x + 13, y);
					local s3 = T(x + 20, y);
					local ok = build.build_surface_x(x + 2, x + 6, y);
					ok = ok && GSRail.BuildRailDepot(depot, T(x + 2, y));
					ok = ok && metro.build_portal(T(x + 7, y), GSMetro.DIAGDIR_SW, d);
					ok = ok && metro.build_underground_x(x + 8, x + 10, y, d);
					ok = ok && metro.build_portal(T(x + 11, y), GSMetro.DIAGDIR_NE, d);
					ok = ok && build.build_surface_x(x + 12, x + 12, y);
					ok = ok && build.build_surface_station_x(s2);
					ok = ok && build.build_surface_x(x + 15, x + 19, y);
					ok = ok && build.build_surface_station_x(s3);
					/* The signal lets trains pass going +x: its "front" is the west neighbour. */
					ok = ok && GSRail.BuildSignal(T(x + 16, y), T(x + 15, y), sig_type);
					report.check_true("lane" + i + "_built", ok, "lane " + i + ": " + GSError.GetLastErrorString());
					report.check_true("lane" + i + "_link", GSMetro.HasPortalConnection(T(x + 7, y), T(x + 11, y), d), "lane " + i + ": portal link");

					local a = trains.build_train(depot, engine);
					local b = trains.build_train(depot, engine);
					local wa = trains.attach_wagon(a, depot, wagon);
					local long_b = true;
					for (local w = 0; w < 10; w++) long_b = long_b && trains.attach_wagon(b, depot, wagon).attached;
					report.check_true("lane" + i + "_trains", a != GSVehicle.VEHICLE_INVALID && b != GSVehicle.VEHICLE_INVALID && wa.attached && long_b, "trains");
					local flags = GSOrder.OF_NON_STOP_INTERMEDIATE;
					/* A waits for a full load at S2 (there is no cargo) until it is sent on. */
					trains.append_surface_order(a, s2, flags | GSOrder.OF_FULL_LOAD_ANY);
					trains.append_surface_order(a, s3, flags);
					trains.append_surface_order(b, s2, flags);
					trains.append_surface_order(b, s3, flags);
					lanes.append({ a = a, b = b, s2 = s2, sig = sig_type });
				}

				local crashed = function() : (lanes) {
					foreach (l in lanes) {
						foreach (v in [l.a, l.b]) {
							if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) return true;
						}
					}
					return false;
				};

				/* A goes through the tunnel to S2 and waits there. */
				foreach (l in lanes) GSVehicle.StartStopVehicle(l.a);
				local stopped = [false, false];
				for (local t = 0; t < 6000 && !(stopped[0] && stopped[1]); t += 5) {
					foreach (i, l in lanes) {
						if (!stopped[i] && GSVehicle.GetState(l.a) == GSVehicle.VS_AT_STATION &&
								GSStation.GetStationID(GSVehicle.GetLocation(l.a)) == GSStation.GetStationID(l.s2)) {
							stopped[i] = true;
						}
					}
					GSController.Sleep(5);
				}
				report.check_true("a_at_s2", stopped[0] && stopped[1], "Both trains A reached S2.");
				foreach (i, l in lanes) {
					report.metric("lane" + i + "_a_x", build.tile_x(GSVehicle.GetLocation(l.a)) - x);
					report.metric("lane" + i + "_a_state", GSVehicle.GetState(l.a));
					report.metric("lane" + i + "_a_underground", GSMetro.IsVehicleUnderground(l.a) ? 1 : 0);
				}

				/* B follows; it must wait below ground while A stands in front of the mouth. */
				foreach (l in lanes) GSVehicle.StartStopVehicle(l.b);
				local crash = false;
				for (local t = 0; t < 4000 && !crash; t += 5) {
					crash = crashed();
					GSController.Sleep(5);
				}
				report.check_true("no_crash_while_blocked", !crash, "A train coming out of the short tunnel must not drive into the train standing in front of the mouth.");
				foreach (i, l in lanes) {
					if (crash) break;
					/* In the tunnel, or in front of it: there is no signal below ground. */
					local bx = build.tile_x(GSVehicle.GetLocation(l.b)) - x;
					report.check_true("lane" + i + "_b_waits", bx <= 10 && GSVehicle.GetCurrentSpeed(l.b) == 0,
						"lane " + i + ": B waits before the exit (x=+" + bx + " underground=" + GSMetro.IsVehicleUnderground(l.b) + " speed=" + GSVehicle.GetCurrentSpeed(l.b) + ")");
				}

				/* A leaves for S3: B comes out and calls at S2. */
				local b_calls = [false, false];
				if (!crash) {
					foreach (l in lanes) GSOrder.SkipToOrder(l.a, 1);
					for (local t = 0; t < 8000 && !crash && !(b_calls[0] && b_calls[1]); t += 5) {
						crash = crashed();
						foreach (i, l in lanes) {
							/* An engine without cargo calls only for a moment: the order moving on to S3 tells. */
							if (GSOrder.ResolveOrderPosition(l.b, GSOrder.ORDER_CURRENT) == 1) b_calls[i] = true;
						}
						GSController.Sleep(5);
					}
				}
				report.check_true("no_crash", !crash, "No train crashed.");
				foreach (i, l in lanes) {
					report.metric("lane" + i + "_b_x", build.tile_x(GSVehicle.GetLocation(l.b)) - x);
					report.metric("lane" + i + "_b_state", GSVehicle.GetState(l.b));
					report.metric("lane" + i + "_b_order", GSOrder.ResolveOrderPosition(l.b, GSOrder.ORDER_CURRENT));
					report.metric("lane" + i + "_a_x_end", build.tile_x(GSVehicle.GetLocation(l.a)) - x);
				}
				report.check_true("lane0_b_comes_out", b_calls[0], "Block signals: B comes out once A has left and calls at S2.");
				report.check_true("lane1_b_comes_out", b_calls[1], "Path signals: B comes out once A has left and calls at S2.");
				cm = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
