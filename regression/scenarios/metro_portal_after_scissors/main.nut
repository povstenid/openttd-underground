/**
 * metro_portal_after_scissors
 *
 * A surface section with the depots right behind a terminus: between the
 * scissors crossover in front of the platforms and the portal there is only
 * one plain tile, no signal.
 *
 * Two such double-track lines at level -1, one along X and one along Y
 * (lane 0 carries traffic towards higher coordinates, lane 1 back):
 *
 *   [A A A A]X - (portal depots portal) S ---- [M M M M] ---- X[B B B B]
 *
 * X = scissors crossover, S = one-way signals. The trains are almost as long
 * as the platforms, with a cab at both ends. Two trains leave each depot;
 * the lane 1 ones first head for terminus A, the lane 0 ones for M. Every
 * train must keep calling at its stations: from a lane 1 platform of A the
 * straight way to M runs out of the lane 1 portal the wrong way, into the
 * back of the surface one-way signals; a train sent that way turned round
 * and was sent out there again, for good.
 */
class MetroPortalAfterScissors extends GSController {
	report = null;
	fails = null;

	function T(x, y) { return GSMap.GetTileIndex(x, y); }

	/** Tile at \a along and cross coordinate \a c of a line. */
	function AT(line, a, c) { return line.axis == "x" ? T(a, c) : T(c, a); }
	function LT(line, a, lane) { return AT(line, a, line.lane0 + lane); }
	function AxisTrack(axis) { return axis == "x" ? GSRail.RAILTRACK_NE_SW : GSRail.RAILTRACK_NW_SE; }

	function fail(key)
	{
		if (!(key in this.fails)) this.fails[key] <- 0;
		this.fails[key]++;
		if (this.fails[key] <= 2) this.report.metric("fail_" + key + "_" + this.fails[key], GSError.GetLastErrorString());
	}

	function Rail(tile, track, depth, key) { if (!GSMetro.BuildUndergroundRail(tile, track, depth)) fail(key); }

	function Signal(tile, axis, dir, depth)
	{
		local track = AxisTrack(axis);
		if (!GSMetro.BuildUndergroundSignal(tile, track, depth)) { fail("signal"); return; }
		/* The default faces westbound (X) / southbound (Y) traffic; building again turns it. */
		if (axis == "x" ? dir > 0 : dir < 0) GSMetro.BuildUndergroundSignal(tile, track, depth);
	}

	function Scissors(line, a, d)
	{
		if (line.axis == "x") {
			foreach (t in [GSRail.RAILTRACK_NE_SW, GSRail.RAILTRACK_NE_SE, GSRail.RAILTRACK_SW_SE]) Rail(LT(line, a, 0), t, d, "scissors");
			foreach (t in [GSRail.RAILTRACK_NE_SW, GSRail.RAILTRACK_NW_SW, GSRail.RAILTRACK_NW_NE]) Rail(LT(line, a, 1), t, d, "scissors");
		} else {
			foreach (t in [GSRail.RAILTRACK_NW_SE, GSRail.RAILTRACK_NW_SW, GSRail.RAILTRACK_SW_SE]) Rail(LT(line, a, 0), t, d, "scissors");
			foreach (t in [GSRail.RAILTRACK_NW_SE, GSRail.RAILTRACK_NE_SE, GSRail.RAILTRACK_NW_NE]) Rail(LT(line, a, 1), t, d, "scissors");
		}
	}

	/** Build a line; returns its station ids and the depots of lanes 0 and 1. */
	function BuildLine(line, build)
	{
		local d = -1, P = 4;
		local s = 60, e = 120, mid = 90;
		local sc0 = s + P, sc1 = e - P;
		local h0 = sc0 + 2, h1 = h0 + 6;
		local c0 = line.lane0;

		/* The map may have water here: clear it first. */
		for (local a = s - 4; a < e + 5; a++) for (local c = c0 - 4; c < c0 + 6; c++) if (GSTile.IsWaterTile(AT(line, a, c))) GSTile.DemolishTile(AT(line, a, c));
		if (line.axis == "x") build.level_area_min_height(T(s - 4, c0 - 4), e - s + 9, 10, 1);
		else build.level_area_min_height(T(c0 - 4, s - 4), 10, e - s + 9, 1);

		/* Platforms: start terminus, middle station, end terminus. */
		local sids = [];
		foreach (m in [s, mid, e - P + 1]) {
			local sid = null;
			for (local a = m; a < m + P; a++) for (local lane = 0; lane < 2; lane++) {
				if (!GSMetro.BuildUndergroundStation(LT(line, a, lane), AxisTrack(line.axis), d, sid == null ? GSStation.STATION_NEW : sid)) fail("platform");
				if (sid == null) sid = GSMetro.GetUndergroundStationID(LT(line, a, lane), d);
			}
			sids.append(sid);
		}
		/* Scissors crossovers right in front of the termini. */
		Scissors(line, sc0, d);
		Scissors(line, sc1, d);
		for (local a = s; a <= e; a++) {
			if ((a >= s && a < s + P) || (a >= mid && a < mid + P) || a > e - P || a == sc0 || a == sc1 || (a >= h0 && a <= h1)) continue;
			for (local lane = 0; lane < 2; lane++) Rail(LT(line, a, lane), AxisTrack(line.axis), d, "track");
		}
		/* One-way signals round the surface section and the middle station, and in front
		 * of terminus B. None on the one tile between the crossover and the portal. */
		foreach (a in [h1 + 2, mid - 1, mid + P, sc1 - 1, 105]) {
			Signal(LT(line, a, 0), line.axis, 1, d);
			Signal(LT(line, a, 1), line.axis, -1, d);
		}
		/* Surface section with a depot beside each track. */
		local into_low = line.axis == "x" ? GSMetro.DIAGDIR_NE : GSMetro.DIAGDIR_NW;
		local into_high = line.axis == "x" ? GSMetro.DIAGDIR_SW : GSMetro.DIAGDIR_SE;
		local dm = (h0 + h1) / 2;
		local depots = [];
		for (local lane = 0; lane < 2; lane++) {
			if (!GSMetro.BuildPortal(LT(line, h0, lane), into_low, d)) fail("portal");
			if (!GSMetro.BuildPortal(LT(line, h1, lane), into_high, d)) fail("portal");
			for (local a = h0 + 1; a < h1; a++) if (!GSRail.BuildRailTrack(LT(line, a, lane), AxisTrack(line.axis))) fail("surface");
			local depot = AT(line, dm, lane == 0 ? c0 - 1 : c0 + 2);
			if (!(GSRail.BuildRail(LT(line, dm - 1, lane), LT(line, dm, lane), depot) && GSRail.BuildRail(depot, LT(line, dm, lane), LT(line, dm + 1, lane)) && GSRail.BuildRailDepot(depot, LT(line, dm, lane)))) fail("depot");
			depots.append(depot);
		}
		foreach (a in [h0 + 2, h1 - 2]) {
			if (!GSRail.BuildSignal(LT(line, a, 0), LT(line, a - 1, 0), GSRail.SIGNALTYPE_PBS_ONEWAY)) fail("ssignal");
			if (!GSRail.BuildSignal(LT(line, a, 1), LT(line, a + 1, 1), GSRail.SIGNALTYPE_PBS_ONEWAY)) fail("ssignal");
		}
		return { sids = sids, depots = depots, P = P };
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		this.report = MetroTest("metro_portal_after_scissors", null, 510941);
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
			local engine = trains.find_rail_engine();

			local fleet = [];
			foreach (line in [{ axis = "x", lane0 = 130 }, { axis = "y", lane0 = 180 }]) {
				local l = BuildLine(line, build);
				foreach (lane, depot in l.depots) {
					for (local k = 0; k < 2; k++) {
						local v = trains.build_train(depot, engine);
						if (v == GSVehicle.VEHICLE_INVALID) { fail("train"); continue; }
						/* As long as fits a platform, with a cab at both ends. */
						local head = GSVehicle.GetLength(v);
						for (local i = 0; i < 20; i++) {
							if (!trains.attach_cargo_wagon(v, depot, cargo).attached) { fail("wagon"); break; }
							if (GSVehicle.GetLength(v) + head > l.P * 16 - 6) { GSVehicle.SellWagon(v, GSVehicle.GetNumWagons(v) - 1); break; }
						}
						local tail = trains.build_train(depot, engine);
						if (tail == GSVehicle.VEHICLE_INVALID || !GSVehicle.MoveWagon(tail, 0, v, GSVehicle.GetNumWagons(v) - 1)) fail("tail");
						foreach (sid in [l.sids[0], l.sids[1], l.sids[2], l.sids[1]]) trains.append_underground_order(v, sid, GSOrder.OF_NON_STOP_INTERMEDIATE);
						/* Lane 0 trains head for M first, lane 1 ones for terminus A. */
						if (lane == 0) GSOrder.SkipToOrder(v, 1);
						fleet.append(v);
					}
				}
			}
			report.metric("train_length", GSVehicle.GetLength(fleet[0]) + " wagons " + GSVehicle.GetNumWagons(fleet[0]));
			report.check_eq("fleet", 8, fleet.len(), "Eight trains");
			foreach (v in fleet) { GSVehicle.StartStopVehicle(v); GSController.Sleep(30); }

			/* Run: count the orders each train completes and its longest wait for the next one. */
			local done = {}, last = {}, since = {}, worst = {}, trace = {};
			foreach (v in fleet) { done[v] <- 0; last[v] <- GSOrder.ResolveOrderPosition(v, GSOrder.ORDER_CURRENT); since[v] <- 0; worst[v] <- 0; trace[v] <- ""; }
			for (local t = 0; t < 40000; t += 10) {
				foreach (v in fleet) {
					if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) continue;
					local o = GSOrder.ResolveOrderPosition(v, GSOrder.ORDER_CURRENT);
					if (o != last[v]) { done[v]++; last[v] = o; since[v] = t; }
					if (t - since[v] > worst[v]) worst[v] = t - since[v];
					if (t % 200 == 0 && t <= 6000) {
						local l = GSVehicle.GetLocation(v);
						trace[v] += " " + t + ":" + GSMap.GetTileX(l) + "," + GSMap.GetTileY(l) + "v" + GSVehicle.GetCurrentSpeed(v) + "o" + o;
					}
				}
				GSController.Sleep(10);
			}
			foreach (v in fleet) {
				local alive = GSVehicle.IsValidVehicle(v) && GSVehicle.GetState(v) != GSVehicle.VS_CRASHED;
				report.check_true("train_" + v + "_intact", alive, "Train should not crash");
				report.metric("orders_" + v, done[v] + " worst_wait " + worst[v]);
				local ok = done[v] >= 12 && worst[v] < 8000;
				report.check_true("train_" + v + "_runs", ok, "Train should keep calling at its stations");
				if (!alive || !ok) report.metric("trace_" + v, trace[v]);
			}
			foreach (k, n in this.fails) report.metric("failcount_" + k, n);
			cm = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.check_true("final_save_written", GSTest.SaveGame("metro_portal_after_scissors_final.sav"), "Save should be written.");
		report.finish();
	}
}
