/**
 * metro_portal_exit_head_on
 *
 * A single surface track leads to a portal that trains both come out of and
 * dive into; below ground the line splits right behind the mouth:
 *
 *   D1 -- [SA SA] -- sig(x0+5) -- [SB SB] -- x0+8..x0+11 -- M(x0+12)
 *                                                            ~~ J(x0+13) ~~ x0+14 ~ sig(x0+15) ~ .. x0+19 ~~ P2(x0+20) -- D2
 *                                                                 \__ (x0+13, y+1) ~~ x0+14..x0+15 ~~ [V V]
 *
 * The surface signal at x0+5 is a path signal, so the stretch in front of M is
 * a path signal block. Train Y waits for a full load in SB, on the way to the
 * mouth. Train X comes from D2 through P2 and wants to come out of M and go to
 * SA, through SB. It must wait below ground at the signal at x0+15: its way on
 * from the mouth cannot be reserved while Y stands in SB.
 *
 * Before the fix X came out of the mouth without a path (only the mouth tile
 * itself was checked) and stood there with its head on the surface for good,
 * the way on held by Y; once sent on, Y could not dive into the mouth X stood
 * in: the two stood head to head (the deadlock in front of the portal of the
 * user's game, where the train in front of the mouth had broken down).
 * Now Y dives past the waiting X to V, then X comes out and reaches SA.
 */
class MetroPortalExitHeadOn extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_portal_exit_head_on", null, 510133);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();
		try {
			local area = build.find_buildable_rectangle(30, 8);
			report.check_true("build_area_found", area != null, "area");
			local company = companies.ensure_company();
			local cm = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			if (area != null && cm != null) report.check_true("area_level", build.level_area_min_height(area, 30, 8, 1), "level the area");
			local engine = cm == null ? null : trains.find_rail_engine();
			local wagon = cm == null ? null : trains.find_rail_wagon();
			report.check_true("engine", engine != null && wagon != null, "engine and wagon");
			if (area != null && engine != null && wagon != null && company != GSCompany.COMPANY_INVALID) {
				local x0 = build.tile_x(area) + 1;
				local y = build.tile_y(area) + 2;
				local d = -1;
				local T = function(ax, ay) : (build) { return build.make_tile(ax, ay); };
				local d1 = T(x0, y);
				local sa = T(x0 + 2, y);
				local sb = T(x0 + 6, y);
				local mouth = T(x0 + 12, y);
				local d2 = T(x0 + 22, y);

				local ok = build.build_surface_x(x0 + 1, x0 + 1, y);
				ok = ok && GSRail.BuildRailDepot(d1, T(x0 + 1, y));
				ok = ok && build.build_surface_station_x(sa);
				ok = ok && build.build_surface_x(x0 + 4, x0 + 5, y);
				ok = ok && build.build_surface_station_x(sb);
				ok = ok && build.build_surface_x(x0 + 8, x0 + 11, y);
				ok = ok && GSRail.BuildSignal(T(x0 + 5, y), T(x0 + 4, y), GSRail.SIGNALTYPE_PBS);
				report.check_true("surface_built", ok, "surface: " + GSError.GetLastErrorString());

				ok = metro.build_portal(mouth, GSMetro.DIAGDIR_SW, d);
				ok = ok && metro.build_underground_x(x0 + 13, x0 + 19, y, d);
				ok = ok && GSMetro.BuildUndergroundRail(T(x0 + 13, y), GSRail.RAILTRACK_NE_SE, d);
				ok = ok && GSMetro.BuildUndergroundRail(T(x0 + 13, y + 1), GSRail.RAILTRACK_NW_SW, d);
				ok = ok && metro.build_underground_x(x0 + 14, x0 + 15, y + 1, d);
				/* A new path signal on an X track faces trains running NE (towards the mouth). */
				ok = ok && GSMetro.BuildUndergroundSignal(T(x0 + 15, y), GSRail.RAILTRACK_NE_SW, d, GSRail.SIGNALTYPE_PBS_ONEWAY);
				ok = ok && metro.build_portal(T(x0 + 20, y), GSMetro.DIAGDIR_NE, d);
				ok = ok && build.build_surface_x(x0 + 21, x0 + 21, y);
				ok = ok && GSRail.BuildRailDepot(d2, T(x0 + 21, y));
				report.check_true("underground_built", ok, "underground: " + GSError.GetLastErrorString());
				local v = null;
				for (local a = x0 + 16; a <= x0 + 17; a++) {
					if (!GSMetro.BuildUndergroundStation(T(a, y + 1), GSRail.RAILTRACK_NE_SW, d, v == null ? GSStation.STATION_NEW : v)) report.check_true("platform", false, "platform " + GSError.GetLastErrorString());
					if (v == null) v = GSMetro.GetUndergroundStationID(T(a, y + 1), d);
				}
				report.check_true("station_v", v != null && GSStation.IsValidStation(v), "underground station V");
				report.check_true("x_link", GSMetro.HasPortalConnection(T(x0 + 20, y), mouth, d), "P2 leads out of M");

				local tx = trains.build_train(d2, engine);
				local ty = trains.build_train(d1, engine);
				local wx = trains.attach_wagon(tx, d2, wagon);
				local wy = trains.attach_wagon(ty, d1, wagon);
				report.check_true("trains", tx != GSVehicle.VEHICLE_INVALID && ty != GSVehicle.VEHICLE_INVALID && wx.attached && wy.attached, "trains");
				local flags = GSOrder.OF_NON_STOP_INTERMEDIATE;
				trains.append_surface_order(ty, sb, flags | GSOrder.OF_FULL_LOAD_ANY);
				trains.append_underground_order(ty, v, flags);
				trains.append_surface_order(tx, sa, flags);

				local crashed = function() : (tx, ty) {
					foreach (t in [tx, ty]) {
						if (!GSVehicle.IsValidVehicle(t) || GSVehicle.GetState(t) == GSVehicle.VS_CRASHED) return true;
					}
					return false;
				};

				/* Y goes to SB and waits there for a load that never comes. */
				GSVehicle.StartStopVehicle(ty);
				local y_waits = false;
				for (local t = 0; t < 3000 && !y_waits; t += 5) {
					y_waits = GSVehicle.GetState(ty) == GSVehicle.VS_AT_STATION && GSStation.GetStationID(GSVehicle.GetLocation(ty)) == GSStation.GetStationID(sb);
					GSController.Sleep(5);
				}
				report.check_true("y_waits_in_sb", y_waits, "Y waits in SB.");

				/* X comes through the tunnel towards the mouth: it has to wait below ground. */
				GSVehicle.StartStopVehicle(tx);
				GSController.Sleep(2500);
				report.check_true("x_waits_below", GSMetro.IsVehicleUnderground(tx) && GSVehicle.GetCurrentSpeed(tx) == 0,
					"X waits below ground (underground=" + GSMetro.IsVehicleUnderground(tx) + " x=+" + (build.tile_x(GSVehicle.GetLocation(tx)) - x0) + ")");

				/* Y is sent on: it dives past X, then X comes out. */
				GSOrder.SkipToOrder(ty, 1);
				local y_at_v = false, x_at_sa = false, crash = false;
				for (local t = 0; t < 8000 && !crash && !(y_at_v && x_at_sa); t += 5) {
					crash = crashed();
					/* An engine without cargo calls only for a moment: being at the platforms tells. */
					if (GSMetro.IsVehicleUnderground(ty) && build.tile_x(GSVehicle.GetLocation(ty)) >= x0 + 16) y_at_v = true;
					if (!GSMetro.IsVehicleUnderground(tx) && GSStation.GetStationID(GSVehicle.GetLocation(tx)) == GSStation.GetStationID(sa)) x_at_sa = true;
					GSController.Sleep(5);
				}
				report.metric("x_x", build.tile_x(GSVehicle.GetLocation(tx)) - x0);
				report.metric("x_underground", GSMetro.IsVehicleUnderground(tx) ? 1 : 0);
				report.metric("y_x", build.tile_x(GSVehicle.GetLocation(ty)) - x0);
				report.metric("y_underground", GSMetro.IsVehicleUnderground(ty) ? 1 : 0);
				report.check_true("no_crash", !crash, "No train crashed.");
				report.check_true("y_reaches_v", y_at_v, "Y dives past the waiting X and reaches V.");
				report.check_true("x_reaches_sa", x_at_sa, "X comes out of the mouth after Y and reaches SA.");
				cm = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
