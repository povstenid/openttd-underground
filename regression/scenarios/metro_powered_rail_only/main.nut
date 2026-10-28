/**
 * metro_powered_rail_only
 *
 * Underground trains are routed only over rail they have power on, like on the
 * surface. The line of metro_terminus_crossover, but the platform of terminus B
 * straight ahead of the arriving trains is built with rail they have no power on:
 *
 *   [A A A A]X ----- [M M M M] ---- (portal surface depots portal) ---- X[b b b b]   lane 0, b = other rail
 *   [A A A A]X ----- [M M M M] ---- (portal surface depots portal) ---- X[B B B B]   lane 1
 *
 * Both trains must keep turning at both termini, crossing over to platform B,
 * and never enter platform b: there they would be stranded without power.
 */
class MetroPoweredRailOnly extends GSController {
	report = null;
	fails = null;

	function T(x, y) { return GSMap.GetTileIndex(x, y); }

	function fail(key)
	{
		if (!(key in this.fails)) this.fails[key] <- 0;
		this.fails[key]++;
		if (this.fails[key] <= 2) this.report.metric("fail_" + key + "_" + this.fails[key], GSError.GetLastErrorString());
	}

	function Rail(tile, track, depth, key) { if (!GSMetro.BuildUndergroundRail(tile, track, depth)) fail(key); }

	function Signal(tile, dir, depth)
	{
		if (!GSMetro.BuildUndergroundSignal(tile, GSRail.RAILTRACK_NE_SW, depth)) { fail("signal"); return; }
		/* The default faces westbound traffic; building again turns it. */
		if (dir > 0) GSMetro.BuildUndergroundSignal(tile, GSRail.RAILTRACK_NE_SW, depth);
	}

	/**
	 * Two rail types and an engine with power on the first one only (railway
	 * and monorail; on the metro it was third rail and ordinary track).
	 */
	function FindRailTypes()
	{
		local el = null, plain = null;
		foreach (rt, _ in GSRailTypeList()) {
			local name = GSRail.GetName(rt);
			if (name == "Railway construction") el = rt;
			if (name == "Monorail construction") plain = rt;
		}
		if (el == null || plain == null) return null;
		/* The first such engine, as metro_terminus_crossover picks it. */
		local best = null;
		foreach (engine, _ in GSEngineList(GSVehicle.VT_RAIL)) {
			if (!GSEngine.IsBuildable(engine) || GSEngine.IsWagon(engine)) continue;
			if (!GSEngine.HasPowerOnRail(engine, el) || GSEngine.HasPowerOnRail(engine, plain)) continue;
			best = engine;
			break;
		}
		return best == null ? null : { el = el, other = plain, engine = best };
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		this.report = MetroTest("metro_powered_rail_only", null, 510933);
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

			local types = this.FindRailTypes();
			report.check_true("rail_types", types != null, "An engine with power on one rail type and not on another.");
			if (types == null) throw "no rail types";
			report.metric("powered", GSRail.GetName(types.el));
			report.metric("unpowered", GSRail.GetName(types.other));
			report.metric("engine", GSEngine.GetName(types.engine));
			GSRail.SetCurrentRailType(types.el);

			local y = 130, d = -1, P = 4;
			local s = 60, e = 150, mid = 100, h0 = 118, h1 = 128;
			build.level_area_min_height(T(s - 4, y - 4), e - s + 9, 10, 1);

			/* Platforms: start terminus, middle station, end terminus (lane 1 of the end terminus unpowered). */
			local sids = [];
			foreach (m in [s, mid, e - P + 1]) {
				local sid = null;
				for (local a = m; a < m + P; a++) for (local lane = 0; lane < 2; lane++) {
					GSRail.SetCurrentRailType(m == e - P + 1 && lane == 0 ? types.other : types.el);
					if (!GSMetro.BuildUndergroundStation(T(a, y + lane), GSRail.RAILTRACK_NE_SW, d, sid == null ? GSStation.STATION_NEW : sid)) fail("platform");
					if (sid == null) sid = GSMetro.GetUndergroundStationID(T(a, y + lane), d);
				}
				sids.append(sid);
			}
			GSRail.SetCurrentRailType(types.el);
			report.check_eq("unpowered_platform", types.other, GSMetro.GetUndergroundRailType(T(e, y), d), "Platform b should be of the other rail type.");

			/* Scissors crossovers right in front of the termini. */
			foreach (a in [s + P, e - P]) {
				foreach (t in [GSRail.RAILTRACK_NE_SW, GSRail.RAILTRACK_NE_SE, GSRail.RAILTRACK_SW_SE]) Rail(T(a, y), t, d, "scissors");
				foreach (t in [GSRail.RAILTRACK_NE_SW, GSRail.RAILTRACK_NW_SW, GSRail.RAILTRACK_NW_NE]) Rail(T(a, y + 1), t, d, "scissors");
			}
			for (local a = s; a <= e; a++) {
				if ((a >= s && a < s + P) || (a >= mid && a < mid + P) || a > e - P || a == s + P || a == e - P || (a >= h0 && a <= h1)) continue;
				for (local lane = 0; lane < 2; lane++) Rail(T(a, y + lane), GSRail.RAILTRACK_NE_SW, d, "track");
			}
			foreach (a in [s + P + 1, e - P - 1, mid - 1, mid + P, h0 - 2, h1 + 2, 80, 140]) {
				Signal(T(a, y), 1, d);
				Signal(T(a, y + 1), -1, d);
			}
			/* Surface section with depots. */
			local depots = [];
			for (local lane = 0; lane < 2; lane++) {
				if (!GSMetro.BuildPortal(T(h0, y + lane), GSMetro.DIAGDIR_NE, d)) fail("portal");
				if (!GSMetro.BuildPortal(T(h1, y + lane), GSMetro.DIAGDIR_SW, d)) fail("portal");
				for (local a = h0 + 1; a < h1; a++) if (!GSRail.BuildRailTrack(T(a, y + lane), GSRail.RAILTRACK_NE_SW)) fail("surface");
				local depot = T(123, lane == 0 ? y - 1 : y + 2);
				if (!(GSRail.BuildRail(T(122, y + lane), T(123, y + lane), depot) && GSRail.BuildRail(depot, T(123, y + lane), T(124, y + lane)) && GSRail.BuildRailDepot(depot, T(123, y + lane)))) fail("depot");
				depots.append(depot);
			}
			foreach (a in [h0 + 2, h1 - 2]) {
				if (!GSRail.BuildSignal(T(a, y), T(a - 1, y), GSRail.SIGNALTYPE_PBS_ONEWAY)) fail("ssignal");
				if (!GSRail.BuildSignal(T(a, y + 1), T(a + 1, y + 1), GSRail.SIGNALTYPE_PBS_ONEWAY)) fail("ssignal");
			}

			local fleet = [];
			foreach (depot in depots) {
				local v = trains.build_train(depot, types.engine);
				if (v == GSVehicle.VEHICLE_INVALID) { fail("train"); continue; }
				for (local i = 0; i < 3; i++) trains.attach_cargo_wagon(v, depot, cargo);
				foreach (sid in [sids[0], sids[1], sids[2], sids[1]]) trains.append_underground_order(v, sid, GSOrder.OF_NON_STOP_INTERMEDIATE);
				fleet.append(v);
			}
			report.check_eq("fleet", 2, fleet.len(), "Two trains");
			foreach (v in fleet) { GSVehicle.StartStopVehicle(v); GSController.Sleep(200); }

			/* Run: count the turns at each terminus and every visit to platform b. */
			local turns = {}, last = {}, on_b = {};
			foreach (v in fleet) { turns[v] <- [0, 0]; last[v] <- -1; on_b[v] <- 0; }
			for (local t = 0; t < 60000; t += 10) {
				foreach (v in fleet) {
					if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) continue;
					local o = GSOrder.ResolveOrderPosition(v, GSOrder.ORDER_CURRENT);
					if (last[v] == 0 && o == 1) turns[v][0]++;
					if (last[v] == 2 && o == 3) turns[v][1]++;
					last[v] = o;
					if (GSMetro.IsVehicleUnderground(v)) {
						local ut = GSMetro.GetVehicleUndergroundTile(v);
						if (GSMap.GetTileY(ut) == y && GSMap.GetTileX(ut) > e - P) on_b[v]++;
					}
				}
				GSController.Sleep(10);
			}
			foreach (v in fleet) {
				local alive = GSVehicle.IsValidVehicle(v) && GSVehicle.GetState(v) != GSVehicle.VS_CRASHED;
				report.check_true("train_" + v + "_intact", alive, "Train should not crash");
				report.metric("turns_" + v, turns[v][0] + "/" + turns[v][1]);
				report.check_true("train_" + v + "_turns", turns[v][0] >= 3 && turns[v][1] >= 3, "Train should keep turning at both termini");
				report.check_eq("train_" + v + "_never_unpowered", 0, on_b[v], "Train should never enter the platform it has no power on");
			}
			foreach (k, n in this.fails) report.metric("failcount_" + k, n);
			cm = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.check_true("final_save_written", GSTest.SaveGame("metro_powered_rail_only_final.sav"), "Save should be written.");
		report.finish();
	}
}
