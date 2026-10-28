/**
 * metro_single_order_through_station
 *
 *        depot
 *   +-----+------------------------------+
 *   |    one-way loop (clockwise)        |
 *   |                                    |
 *   +--[portal]==UG==[UG station]==UG==[portal]--+
 *
 * Two such loops: one for standard trains (an engine with three carriages),
 * one for the cars of the metro set NewGRF (four motor cars of half a tile).
 * The bottom edge of each loop runs underground through a station of six
 * tiles; the ramps at both ends climb one level and become portals. Five
 * trains per loop, each with that station as its only order.
 *
 * With its only order a train is sent to the station it has just left. On
 * leaving the platform the front, still at the stop at the end of the last
 * platform tile, "arrived" again on every step: it was held back from the
 * next tile while the parts behind it moved on, until they passed it and the
 * game stopped with an assert in TrainController (a wagon following a vehicle
 * that is not on the next tile).
 *
 * Pass: the game does not stop, and every train stops at the station and
 * leaves it again several times (so it went round the loop in one piece).
 */
class MetroSingleOrderThroughStation extends GSController {
	report = null;

	function T(x, y) { return GSMap.GetTileIndex(x, y); }

	/* Rectangle with staircase (45 degree) corners, clockwise in tile coordinates. */
	function Path(x0, y0, x1, y1, c)
	{
		local p = [];
		for (local x = x0 + c; x < x1 - c; x++) p.push([x, y0]);
		for (local i = 0; i < c; i++) { p.push([x1 - c + i, y0 + i]); p.push([x1 - c + i, y0 + i + 1]); }
		for (local y = y0 + c; y < y1 - c; y++) p.push([x1, y]);
		for (local i = 0; i < c; i++) { p.push([x1 - i, y1 - c + i]); p.push([x1 - i - 1, y1 - c + i]); }
		for (local x = x1 - c; x > x0 + c; x--) p.push([x, y1]);
		for (local i = 0; i < c; i++) { p.push([x0 + c - i, y1 - i]); p.push([x0 + c - i, y1 - i - 1]); }
		for (local y = y1 - c; y > y0 + c; y--) p.push([x0, y]);
		for (local i = 0; i < c; i++) { p.push([x0 + i, y0 + c - i]); p.push([x0 + i + 1, y0 + c - i]); }
		return p;
	}

	/**
	 * Build one loop with its top-left corner at (ox, oy) for rail type rt.
	 * @return Table with the depot tile and the underground station, or null.
	 */
	/** Clear the area of a loop at (ox, oy); whether all of it is free now. */
	function ClearArea(ox, oy)
	{
		local clear = true;
		for (local y = oy - 4; y <= oy + 24; y++) for (local x = ox - 4; x <= ox + 38; x++) {
			if (!GSTile.IsBuildable(T(x, y))) GSTile.DemolishTile(T(x, y));
			if (!GSTile.IsBuildable(T(x, y))) clear = false;
		}
		return clear;
	}

	function BuildLoop(name, ox, oy, rt)
	{
		for (local pass = 0; pass < 8; pass++) {
			local corner = T(ox - 4, oy - 4);
			for (local i = 0; i < 4 && GSTile.GetCornerHeight(corner, GSTile.CORNER_N) > 2; i++) GSTile.LowerTile(corner, GSTile.SLOPE_N);
			for (local i = 0; i < 4 && GSTile.GetCornerHeight(corner, GSTile.CORNER_N) < 2; i++) GSTile.RaiseTile(corner, GSTile.SLOPE_N);
			GSTile.LevelTiles(corner, T(ox + 39, oy + 25));
		}

		GSRail.SetCurrentRailType(rt);
		local x0 = ox, y0 = oy, x1 = ox + 34, y1 = oy + 20;
		local p = this.Path(x0, y0, x1, y1, 5);
		local n = p.len(), bad = 0;
		local ug_x1 = ox + 7, ug_x2 = ox + 23; /* portals at both ends */
		for (local i = 0; i < n; i++) {
			local a = p[(i + n - 1) % n], b = p[i], c = p[(i + 1) % n];
			if (b[1] == y1 && b[0] >= ug_x1 && b[0] <= ug_x2) continue;
			if (!GSRail.BuildRail(T(a[0], a[1]), T(b[0], b[1]), T(c[0], c[1]))) { bad++; this.report.metric(name + "_bad_" + b[0] + "_" + b[1], GSError.GetLastErrorString()); }
		}
		this.report.check_eq(name + "_loop_built", 0, bad, "Every piece of the surface loop should be built.");

		/* One-way block signals, clockwise, so that several trains share the loop. */
		local signals = 0;
		for (local i = 3; i < n; i += 6) {
			local a = p[(i + n - 1) % n], b = p[i], c = p[(i + 1) % n];
			if (b[1] == y1 && b[0] >= ug_x1 - 2 && b[0] <= ug_x2 + 2) continue;
			if (b[1] == y0 && b[0] >= ox + 9 && b[0] <= ox + 23) continue;
			if (!((a[0] == c[0] && a[0] == b[0]) || (a[1] == c[1] && a[1] == b[1]))) continue;
			if (GSRail.BuildSignal(T(b[0], b[1]), T(c[0], c[1]), GSRail.SIGNALTYPE_NORMAL)) signals++;
		}
		this.report.metric(name + "_signals", signals);

		/* Underground: track, a through station of six tiles, track; the ramps
		 * at the ends climb to the surface and become portals. */
		local d = GSTile.GetMinHeight(T(ox + 15, y1)) - 1;
		local ok = true;
		local sid = GSStation.STATION_NEW;
		for (local x = ug_x1 + 1; x <= ug_x2 - 1; x++) {
			if (x >= ox + 12 && x <= ox + 17) {
				ok = GSMetro.BuildUndergroundStation(T(x, y1), GSRail.RAILTRACK_NE_SW, d, sid) && ok;
				if (sid == GSStation.STATION_NEW) sid = GSMetro.GetUndergroundStationID(T(x, y1), d);
			} else {
				ok = GSMetro.BuildUndergroundRail(T(x, y1), GSRail.RAILTRACK_NE_SW, d) && ok;
			}
		}
		ok = GSMetro.BuildRamp(T(ug_x2, y1), GSMetro.DIAGDIR_SW, d, d + 1) && ok;
		ok = GSMetro.BuildRamp(T(ug_x1, y1), GSMetro.DIAGDIR_NE, d, d + 1) && ok;
		this.report.check_true(name + "_underground_built", ok && GSStation.IsValidStation(sid), "Underground line, station and ramps should be built: " + GSError.GetLastErrorString());
		GSMetro.BuildExitSurface(T(ox + 14, y1 + 2), sid, GSMetro.EXIT_PAVILION);

		/* A depot north of the top edge. */
		local depot = T(ox + 12, oy - 1), here = T(ox + 12, oy);
		local depot_ok = GSRail.BuildRail(depot, here, T(ox + 13, oy)) && GSRail.BuildRailDepot(depot, here);
		this.report.check_true(name + "_depot_built", depot_ok, "Depot should be built.");

		return { depot = depot, sid = sid, ok = ok && depot_ok && bad == 0 };
	}

	function StartTrains(name, trains, sid)
	{
		local ordered = 0, started = 0;
		foreach (t in trains) {
			if (GSMetro.AppendOrderByStationID(t, sid, GSOrder.OF_NONE)) ordered++;
			if (GSVehicle.StartStopVehicle(t)) started++;
			GSController.Sleep(150);
		}
		this.report.check_eq(name + "_trains_ordered", trains.len(), ordered, "Every train should get its order.");
		this.report.check_eq(name + "_trains_started", trains.len(), started, "Every train should start: " + GSError.GetLastErrorString());
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		this.report = MetroTest("metro_single_order_through_station", null, 510137);
		local companies = MetroTest.MetroCompanyLib(this.report);

		try {
			local company = companies.ensure_company();
			local cm = GSCompanyMode(company);

			local rail_rt = -1, metro_rt = -1;
			foreach (rt, _ in GSRailTypeList()) {
				if (GSRail.GetName(rt) == "Metro track") metro_rt = rt;
				else if (rail_rt == -1) rail_rt = rt;
			}
			this.report.check_true("metro_set_loaded", metro_rt != -1, "The metro set NewGRF (metro_set.grf) should be loaded.");

			/* Standard trains: the slowest engine for ordinary rail and three carriages. */
			local engine = null, carriage = null;
			foreach (e, _ in GSEngineList(GSVehicle.VT_RAIL)) {
				if (!GSEngine.IsBuildable(e) || GSEngine.GetRailType(e) != rail_rt) continue;
				if (GSEngine.IsWagon(e)) {
					if (carriage == null && GSEngine.GetCapacity(e) > 0) carriage = e;
				} else if (GSEngine.GetPower(e) > 0 && GSEngine.GetCapacity(e) <= 0) {
					if (engine == null || GSEngine.GetMaxSpeed(e) < GSEngine.GetMaxSpeed(engine)) engine = e;
				}
			}
			/* Metro cars: four motor cars of one type. */
			local car = null;
			if (metro_rt != -1) {
				foreach (e, _ in GSEngineList(GSVehicle.VT_RAIL)) {
					if (GSEngine.IsBuildable(e) && !GSEngine.IsWagon(e) && GSEngine.GetRailType(e) == metro_rt) { car = e; break; }
				}
			}
			this.report.check_true("engines_found", engine != null && carriage != null && (metro_rt == -1 || car != null), "Expected buildable engines and carriages.");

			local lines = [];
			if (engine != null && carriage != null) lines.push({ name = "rail", ox = 12, oy = 10, rt = rail_rt, parts = [engine, carriage, carriage, carriage] });
			if (car != null) lines.push({ name = "metro", ox = 12, oy = 60, rt = metro_rt, parts = [car, car, car, car] });

			local all = [];
			local spots = [[12, 10], [12, 60], [70, 10], [70, 60], [12, 98], [70, 98]];
			foreach (line in lines) {
				while (spots.len() > 0 && !this.ClearArea(spots[0][0], spots[0][1])) spots.remove(0);
				if (spots.len() == 0) break;
				line.ox = spots[0][0];
				line.oy = spots[0][1];
				spots.remove(0);
				this.report.metric(line.name + "_origin", line.ox + "," + line.oy);
				local built = this.BuildLoop(line.name, line.ox, line.oy, line.rt);
				if (!built.ok) continue;
				line.sid <- built.sid;
				line.trains <- [];
				for (local k = 0; k < 5; k++) {
					local head = null;
					foreach (part in line.parts) {
						local v = GSVehicle.BuildVehicle(built.depot, part);
						if (!GSVehicle.IsValidVehicle(v)) continue;
						if (head == null) head = v;
						else GSVehicle.MoveWagon(v, 0, head, GSVehicle.GetNumWagons(head) - 1);
					}
					if (head != null && GSVehicle.GetNumWagons(head) == 4) line.trains.push(head);
				}
				this.report.check_eq(line.name + "_trains_built", 5, line.trains.len(), "Five trains of four parts should be built.");
				if (line.trains.len() > 0) this.report.metric(line.name + "_train_length", GSVehicle.GetLength(line.trains[0]));
				all.push(line);
			}
			foreach (line in all) this.StartTrains(line.name, line.trains, line.sid);

			/* Count the stops at the station: a stop begins when a train stands at
			 * the station below ground, and ends when it is no longer there. */
			local state = {};
			foreach (line in all) foreach (t in line.trains) state[t] <- { at = false, stops = 0 };
			local ticks = 0;
			local enough = false;
			while (ticks < 34000 && !enough) {
				GSController.Sleep(1);
				ticks++;
				enough = true;
				foreach (line in all) foreach (t in line.trains) {
					local s = state[t];
					local at = GSVehicle.IsValidVehicle(t) && GSVehicle.GetState(t) == GSVehicle.VS_AT_STATION && GSMetro.IsVehicleUnderground(t) &&
							GSMetro.GetUndergroundStationID(GSMetro.GetVehicleUndergroundTile(t), GSMetro.GetVehicleUndergroundDepth(t)) == line.sid;
					if (at && !s.at) s.stops++;
					s.at = at;
					if (s.stops < 3) enough = false;
				}
			}
			this.report.metric("ticks", ticks);
			foreach (line in all) {
				local least = 1000;
				foreach (t in line.trains) {
					local alive = GSVehicle.IsValidVehicle(t) && GSVehicle.GetState(t) != GSVehicle.VS_CRASHED;
					this.report.check_true(line.name + "_train_" + t + "_kept", alive, "The train must not crash or vanish.");
					if (state[t].stops < least) least = state[t].stops;
					if (alive) this.report.metric(line.name + "_train_" + t, "stops " + state[t].stops + " state " + GSVehicle.GetState(t) + " at " + GSMap.GetTileX(GSVehicle.GetLocation(t)) + "," + GSMap.GetTileY(GSVehicle.GetLocation(t)) + (GSMetro.IsVehicleUnderground(t) ? " below" : ""));
				}
				this.report.metric(line.name + "_least_stops", least);
				this.report.check_true(line.name + "_every_train_stops_repeatedly", least >= 3, "Every train should stop at the station and leave it at least three times, the fewest stops were " + least + ".");
			}
			cm = null;
		} catch (e) {
			this.report.check_true("unexpected_exception", false, e);
		}

		this.report.finish();
	}
}
