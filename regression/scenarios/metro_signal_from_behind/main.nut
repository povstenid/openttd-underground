/**
 * metro_signal_from_behind
 *
 * One-way underground signals cannot be passed from behind, also by a train
 * that has no path: like on the surface the signal ends the line for it.
 *
 * One line at level -1 (row y):
 *
 *   [A A A A] ---- S< ---- (portal surface depot portal) ---- [B B B B]
 *
 * S< = one-way signal for westbound trains only. The train leaves the depot
 * westwards, passes S along its direction and calls at A. Its next order is
 * B, behind S: there is no path, and it must never pass S eastwards.
 * At A the train stops with its front on the far end tile of the platform.
 */
class MetroSignalFromBehind extends GSController {
	report = null;
	fails = null;

	function T(x, y) { return GSMap.GetTileIndex(x, y); }

	function fail(key)
	{
		if (!(key in this.fails)) this.fails[key] <- 0;
		this.fails[key]++;
		if (this.fails[key] <= 2) this.report.metric("fail_" + key + "_" + this.fails[key], GSError.GetLastErrorString());
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		this.report = MetroTest("metro_signal_from_behind", null, 510933);
		this.fails = {};
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local world = MetroTest.MetroWorldLib(build);
		local trains = MetroTest.MetroTrainLib();

		try {
			local company = companies.ensure_company();
			local cargo = world.find_passenger_cargo();
			local cm = GSCompanyMode(company);

			local y = 130, d = -1, P = 4;
			local s = 60, e = 150, h0 = 118, h1 = 128, sig = 90;
			build.level_area_min_height(T(s - 4, y - 4), e - s + 9, 10, 1);

			local sids = [];
			foreach (m in [s, e - P + 1]) {
				local sid = null;
				for (local a = m; a < m + P; a++) {
					if (!GSMetro.BuildUndergroundStation(T(a, y), GSRail.RAILTRACK_NE_SW, d, sid == null ? GSStation.STATION_NEW : sid)) fail("platform");
					if (sid == null) sid = GSMetro.GetUndergroundStationID(T(a, y), d);
				}
				sids.append(sid);
			}
			for (local a = s + P; a <= e - P; a++) {
				if (a >= h0 && a <= h1) continue;
				if (!GSMetro.BuildUndergroundRail(T(a, y), GSRail.RAILTRACK_NE_SW, d)) fail("track");
			}
			/* The default direction is for westbound traffic. */
			report.check_true("signal", GSMetro.BuildUndergroundSignal(T(sig, y), GSRail.RAILTRACK_NE_SW, d), "One-way signal.");

			if (!GSMetro.BuildPortal(T(h0, y), GSMetro.DIAGDIR_NE, d)) fail("portal");
			if (!GSMetro.BuildPortal(T(h1, y), GSMetro.DIAGDIR_SW, d)) fail("portal");
			for (local a = h0 + 1; a < h1; a++) if (!GSRail.BuildRailTrack(T(a, y), GSRail.RAILTRACK_NE_SW)) fail("surface");
			local depot = T(123, y - 1);
			if (!(GSRail.BuildRail(T(122, y), T(123, y), depot) && GSRail.BuildRail(depot, T(123, y), T(124, y)) && GSRail.BuildRailDepot(depot, T(123, y)))) fail("depot");

			local v = trains.build_train(depot, trains.find_rail_engine());
			report.check_true("train", v != GSVehicle.VEHICLE_INVALID, "Train built.");
			if (v != GSVehicle.VEHICLE_INVALID) {
				for (local i = 0; i < 3; i++) trains.attach_cargo_wagon(v, depot, cargo);
				trains.append_underground_order(v, sids[0], GSOrder.OF_NON_STOP_INTERMEDIATE);
				trains.append_underground_order(v, sids[1], GSOrder.OF_NON_STOP_INTERMEDIATE);
				GSVehicle.StartStopVehicle(v);

				local visited_a = false, behind = 0, first_behind = -1, stop_x = -1;
				for (local t = 0; t < 20000; t += 10) {
					local o = GSOrder.ResolveOrderPosition(v, GSOrder.ORDER_CURRENT);
					if (!visited_a && GSMetro.IsVehicleUnderground(v)) {
						local fx = GSMap.GetTileX(GSMetro.GetVehicleUndergroundTile(v));
						if (stop_x < 0 || fx < stop_x) stop_x = fx;
					}
					if (o == 1) visited_a = true;
					if (visited_a && GSMetro.IsVehicleUnderground(v) && GSMap.GetTileX(GSMetro.GetVehicleUndergroundTile(v)) > sig) {
						behind++;
						if (first_behind < 0) first_behind = t;
					}
					if (visited_a && !GSMetro.IsVehicleUnderground(v) && GSMap.GetTileX(GSVehicle.GetLocation(v)) > sig) {
						behind++;
						if (first_behind < 0) first_behind = t;
					}
					GSController.Sleep(10);
				}
				report.check_true("visited_a", visited_a, "The train should call at A, passing the signal along its direction.");
				report.check_eq("stops_at_platform_end", s, stop_x, "At A the front should get to the far end tile of the platform, the train within it.");
				report.check_eq("never_behind_signal", 0, behind, "After A the train must never pass the signal from behind (first at t=" + first_behind + ").");
				report.check_true("intact", GSVehicle.IsValidVehicle(v) && GSVehicle.GetState(v) != GSVehicle.VS_CRASHED, "Train should not crash.");
			}
			foreach (k, n in this.fails) report.metric("failcount_" + k, n);
			cm = null;
		} catch (ex) {
			report.check_true("unexpected_exception", false, ex);
		}
		report.finish();
	}
}
