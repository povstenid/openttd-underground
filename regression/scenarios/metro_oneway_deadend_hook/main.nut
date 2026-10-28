/**
 * metro_oneway_deadend_hook
 *
 * Two double-track lines at level -1 between two dead-end termini with a
 * scissors crossover in front of the platforms (lane 0 carries traffic east,
 * lane 1 back west). Behind terminus A both tracks run on west into a hook:
 *
 *   depot = portal ---- S [A A A A] X --S-------S------S-- X [B B B B]
 *   (lane 1: ----------- S'[A A A A] X ...)
 *
 * S' on lane 1 right behind the platform is a one-way signal that only lets
 * trains go west, into the hook. In line "dead end" the lane 1 hook is a
 * plain underground dead end; in line "portal" it comes up through a portal
 * to a depot on the surface, behind a surface one-way signal that also lets
 * trains go west only. Either way there is no way back out of the hook.
 *
 * Four trains per line run A - B - A. A train arriving at a lane 1 platform
 * of A turns round there for B. While the way back through the crossover was
 * held by another train it ran on instead, past S', into the hook, and stood
 * there for good.
 */
class MetroOnewayDeadendHook extends GSController {
	report = null;
	fails = null;

	function T(x, y) { return GSMap.GetTileIndex(x, y); }

	function fail(key)
	{
		if (!(key in this.fails)) this.fails[key] <- 0;
		this.fails[key]++;
		if (this.fails[key] <= 2) this.report.metric("fail_" + key + "_" + this.fails[key], GSError.GetLastErrorString());
	}

	function Rail(x0, x1, y, d) { for (local x = x0; x <= x1; x++) if (!GSMetro.BuildUndergroundRail(T(x, y), GSRail.RAILTRACK_NE_SW, d)) fail("track"); }

	/** An underground signal on the X axis; dir > 0 lets trains go east only, otherwise west only. */
	function Signal(x, y, dir, d)
	{
		if (!GSMetro.BuildUndergroundSignal(T(x, y), GSRail.RAILTRACK_NE_SW, d)) { fail("signal"); return; }
		/* The default faces westbound traffic; building again turns it. */
		if (dir > 0) GSMetro.BuildUndergroundSignal(T(x, y), GSRail.RAILTRACK_NE_SW, d);
	}

	function Scissors(x, Y, d)
	{
		foreach (t in [GSRail.RAILTRACK_NE_SW, GSRail.RAILTRACK_NE_SE, GSRail.RAILTRACK_SW_SE]) if (!GSMetro.BuildUndergroundRail(T(x, Y), t, d)) fail("scissors");
		foreach (t in [GSRail.RAILTRACK_NE_SW, GSRail.RAILTRACK_NW_SW, GSRail.RAILTRACK_NW_NE]) if (!GSMetro.BuildUndergroundRail(T(x, Y + 1), t, d)) fail("scissors");
	}

	function Station(x0, Y, d)
	{
		local sid = GSStation.STATION_NEW;
		for (local x = x0; x < x0 + 4; x++) foreach (y in [Y, Y + 1]) {
			if (!GSMetro.BuildUndergroundStation(T(x, y), GSRail.RAILTRACK_NE_SW, d, sid)) fail("platform");
			sid = GSMetro.GetUndergroundStationID(T(x, y), d);
		}
		return sid;
	}

	/**
	 * Build a line with its west end at X0 and lane 0 at Y.
	 * @param portal_hook Whether the lane 1 hook comes up through a portal to a depot.
	 * @return The two stations and the lane 0 depot.
	 */
	function BuildLine(X0, Y, portal_hook)
	{
		local d = -1;
		/* Surface: lane 0 (and with portal_hook lane 1) depot at X0, track X0+1 .. X0+6, portal at X0+7. */
		local lanes = portal_hook ? [Y, Y + 1] : [Y];
		foreach (y in lanes) {
			for (local x = X0 + 1; x <= X0 + 6; x++) if (!GSRail.BuildRailTrack(T(x, y), GSRail.RAILTRACK_NE_SW)) fail("surface");
			if (!GSRail.BuildRailDepot(T(X0, y), T(X0 + 1, y))) fail("depot");
			if (!GSMetro.BuildPortal(T(X0 + 7, y), GSMetro.DIAGDIR_SW, d)) fail("portal");
		}
		/* Surface one-way path signals: lane 0 lets trains go east, lane 1 west. */
		if (!GSRail.BuildSignal(T(X0 + 4, Y), T(X0 + 3, Y), GSRail.SIGNALTYPE_PBS_ONEWAY)) fail("ssignal");
		if (portal_hook && !GSRail.BuildSignal(T(X0 + 4, Y + 1), T(X0 + 5, Y + 1), GSRail.SIGNALTYPE_PBS_ONEWAY)) fail("ssignal");
		/* Without the portal the lane 1 hook is an underground dead end. */
		if (!portal_hook) Rail(X0 + 3, X0 + 7, Y + 1, d);
		Rail(X0 + 8, X0 + 9, Y, d);
		Rail(X0 + 8, X0 + 9, Y + 1, d);
		/* Right behind the platforms: lane 0 east only, lane 1 west only (into the hook). */
		Signal(X0 + 9, Y, 1, d);
		Signal(X0 + 9, Y + 1, -1, d);
		local A = Station(X0 + 10, Y, d);
		Scissors(X0 + 14, Y, d);
		Rail(X0 + 15, X0 + 34, Y, d);
		Rail(X0 + 15, X0 + 34, Y + 1, d);
		foreach (x in [X0 + 16, X0 + 25, X0 + 33]) {
			Signal(x, Y, 1, d);
			Signal(x, Y + 1, -1, d);
		}
		Scissors(X0 + 35, Y, d);
		local B = Station(X0 + 36, Y, d);
		return { A = A, B = B, depot = T(X0, Y) };
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		this.report = MetroTest("metro_oneway_deadend_hook", null, 510942);
		this.fails = {};
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local trains = MetroTest.MetroTrainLib();

		try {
			local company = companies.ensure_company();
			local cm = GSCompanyMode(company);
			local engine = trains.find_rail_engine();
			GSRail.SetCurrentRailType(GSEngine.GetRailType(engine));

			local X0 = 40;
			local lines = [{ name = "dead_end", Y = 64, portal = false }, { name = "portal", Y = 96, portal = true }];
			foreach (line in lines) {
				local Y = line.Y;
				for (local y = Y - 4; y < Y + 6; y++) for (local x = X0 - 4; x < X0 + 46; x++) if (GSTile.IsWaterTile(T(x, y))) GSTile.DemolishTile(T(x, y));
				build.level_area_min_height(T(X0 - 4, Y - 4), 50, 10, 2);
				local l = BuildLine(X0, Y, line.portal);
				line.fleet <- [];
				for (local k = 0; k < 4; k++) {
					/* Two cabs, about half a platform long. */
					local v = trains.build_train(l.depot, engine);
					local tail = trains.build_train(l.depot, engine);
					if (v == GSVehicle.VEHICLE_INVALID || tail == GSVehicle.VEHICLE_INVALID || !GSVehicle.MoveWagon(tail, 0, v, 0)) { fail("train"); continue; }
					trains.append_underground_order(v, l.A, GSOrder.OF_NON_STOP_INTERMEDIATE);
					trains.append_underground_order(v, l.B, GSOrder.OF_NON_STOP_INTERMEDIATE);
					line.fleet.append(v);
				}
				report.check_eq(line.name + "_fleet", 4, line.fleet.len(), "Four trains");
			}
			/* Start the trains one after another, both lines alike. */
			for (local k = 0; k < 4; k++) {
				foreach (line in lines) if (k < line.fleet.len()) GSVehicle.StartStopVehicle(line.fleet[k]);
				GSController.Sleep(400);
			}

			/* Run: count the orders each train completes and its longest stay on lane 1 behind the platforms. */
			local done = {}, last = {}, in_hook = {}, worst = {};
			foreach (line in lines) foreach (v in line.fleet) { done[v] <- 0; last[v] <- GSOrder.ResolveOrderPosition(v, GSOrder.ORDER_CURRENT); in_hook[v] <- 0; worst[v] <- 0; }
			for (local t = 0; t < 30000; t += 10) {
				foreach (line in lines) foreach (v in line.fleet) {
					if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) continue;
					local o = GSOrder.ResolveOrderPosition(v, GSOrder.ORDER_CURRENT);
					if (o != last[v]) { done[v]++; last[v] = o; }
					local l = GSVehicle.GetLocation(v);
					local x = GSMap.GetTileX(l), y = GSMap.GetTileY(l);
					if (y == line.Y + 1 && x <= X0 + 9) {
						in_hook[v] += 10;
						if (in_hook[v] > worst[v]) worst[v] = in_hook[v];
					} else {
						in_hook[v] = 0;
					}
				}
				GSController.Sleep(10);
			}
			foreach (line in lines) foreach (v in line.fleet) {
				local alive = GSVehicle.IsValidVehicle(v) && GSVehicle.GetState(v) != GSVehicle.VS_CRASHED;
				report.check_true(line.name + "_train_" + v + "_intact", alive, "Train should not crash");
				report.metric(line.name + "_orders_" + v, done[v] + " hook_ticks " + worst[v]);
				report.check_true(line.name + "_train_" + v + "_not_in_hook", worst[v] < 1000, "Train should not run into the hook behind the lane 1 platform");
				report.check_true(line.name + "_train_" + v + "_runs", done[v] >= 6, "Train should keep calling at its stations");
			}
			foreach (k, n in this.fails) report.metric("failcount_" + k, n);
			cm = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.check_true("final_save_written", GSTest.SaveGame("metro_oneway_deadend_hook_final.sav"), "Save should be written.");
		report.finish();
	}
}
