/**
 * metro_city_network
 *
 * Demo network on a 512x512 map in 1975 with the xUSSR sets: five towns
 * (a big central city and four towns near the map edges) linked underground.
 *
 * Level 0  - high-speed ring (DC 3kV, 249 km/h track, ChS200 + coaches) round
 *            the map through the four edge towns, and four radial suburban
 *            lines (DC 3kV, 140 km/h track, ER22 EMUs) from the central city
 *            to the edge towns.
 * Level -1 - a metro line in every edge town and the metro ring of the
 *            central city (third rail, 81-717 sets).
 * Level -2 - the east-west metro diameter of the central city.
 * Level -3 - the north-south metro diameter, crossing under the other one.
 *
 * Double track everywhere; the two tracks of a station lie side by side so
 * the platforms form one block. Lines end in a terminus with a scissors
 * crossover in front of dead-end platforms; trains reverse at the buffer stops.
 * Every line comes up to the surface once for its depots.
 *
 * Frames: a "line" runs along an axis; "along" is the coordinate on that axis
 * and its two tracks ("lanes") lie on cross coordinates lane0 and lane0 + 1.
 * Lane 0 carries traffic towards higher along, lane 1 towards lower along.
 */
class MetroCityNetwork extends GSController {
	report = null;
	build = null;
	metro = null;
	world = null;
	trains = null;
	fails = null;

	function T(x, y) { return GSMap.GetTileIndex(x, y); }

	function ok(id, cond, message)
	{
		this.report.check_true(id, cond, message + " (err=" + GSError.GetLastErrorString() + ")");
		return cond;
	}

	/** Count a failed build step under \a key instead of failing the scenario. */
	function fail(key)
	{
		if (!(key in this.fails)) this.fails[key] <- 0;
		this.fails[key]++;
		if (this.fails[key] <= 2) this.report.metric("fail_" + key + "_" + this.fails[key], GSError.GetLastErrorString());
	}

	/* ---------------- frames ---------------- */

	function LT(line, along, lane)
	{
		return line.axis == "x" ? T(along, line.lane0 + lane) : T(line.lane0 + lane, along);
	}

	function AxisTrack(axis) { return axis == "x" ? GSRail.RAILTRACK_NE_SW : GSRail.RAILTRACK_NW_SE; }

	/** One-way signal on \a tile facing traffic in \a dir along \a axis (+1 / -1). */
	function Signal(tile, axis, dir, depth, key)
	{
		local track = AxisTrack(axis);
		if (!GSMetro.BuildUndergroundSignal(tile, track, depth)) { fail(key); return; }
		/* The default faces westbound (X) / southbound (Y) traffic; building again turns it. */
		local flip = axis == "x" ? dir > 0 : dir < 0;
		if (flip) GSMetro.BuildUndergroundSignal(tile, track, depth);
	}

	function Rail(tile, track, depth, key)
	{
		if (!GSMetro.BuildUndergroundRail(tile, track, depth)) fail(key);
	}

	/** Platform tile, joined to \a sid (null = new station). Returns the station id. */
	function Platform(tile, axis, depth, sid, key)
	{
		local st = sid == null ? GSStation.STATION_NEW : sid;
		if (!GSMetro.BuildUndergroundStation(tile, AxisTrack(axis), depth, st)) {
			fail(key + (sid == null ? "" : "_join"));
			/* A join that fails (too far, say) still gets its own platform. */
			if (sid == null || !GSMetro.BuildUndergroundStation(tile, AxisTrack(axis), depth, GSStation.STATION_NEW)) return sid;
		}
		return GSMetro.GetUndergroundStationID(tile, depth);
	}

	/** Level a rectangle to height 1 in passes (one LevelTiles runs out of terraform burst). */
	function Flatten(x0, y0, x1, y1)
	{
		for (local y = y0; y <= y1; y++) for (local x = x0; x <= x1; x++) {
			if (GSTile.HasTreeOnTile(T(x, y))) GSTile.DemolishTile(T(x, y));
		}
		for (local pass = 0; pass < 6; pass++) {
			local flat = true;
			for (local y = y0; y <= y1 && flat; y++) for (local x = x0; x <= x1 && flat; x++) {
				if (GSTile.GetSlope(T(x, y)) != GSTile.SLOPE_FLAT || GSTile.GetMinHeight(T(x, y)) != 1) flat = false;
			}
			if (flat) return true;
			for (local i = 0; i < 4 && GSTile.GetCornerHeight(T(x0, y0), GSTile.CORNER_N) > 1; i++) GSTile.LowerTile(T(x0, y0), GSTile.SLOPE_N);
			for (local i = 0; i < 4 && GSTile.GetCornerHeight(T(x0, y0), GSTile.CORNER_N) < 1; i++) GSTile.RaiseTile(T(x0, y0), GSTile.SLOPE_N);
			GSTile.LevelTiles(T(x0, y0), T(x1 + 1, y1 + 1));
			GSController.Sleep(5);
		}
		return false;
	}

	/**
	 * Surface section of a double-track line between portals at along h0 and h1,
	 * with a depot beside each track. Returns [depot lane 0, depot lane 1].
	 */
	function Hook(line, h0, h1, key)
	{
		local ax = line.axis;
		local c0 = line.lane0;
		local flat = ax == "x" ? Flatten(h0 - 1, c0 - 3, h1 + 1, c0 + 4) : Flatten(c0 - 3, h0 - 1, c0 + 4, h1 + 1);
		if (!flat) fail(key + "_flatten");

		local into_low = ax == "x" ? GSMetro.DIAGDIR_NE : GSMetro.DIAGDIR_NW;
		local into_high = ax == "x" ? GSMetro.DIAGDIR_SW : GSMetro.DIAGDIR_SE;
		for (local lane = 0; lane < 2; lane++) {
			if (!GSMetro.BuildPortal(LT(line, h0, lane), into_low, line.depth)) fail(key + "_portal");
			if (!GSMetro.BuildPortal(LT(line, h1, lane), into_high, line.depth)) fail(key + "_portal");
			for (local a = h0 + 1; a < h1; a++) {
				if (!GSRail.BuildRailTrack(LT(line, a, lane), AxisTrack(ax))) fail(key + "_surface");
			}
		}
		/* One-way signals on the surface too, so trains leaving a depot take their track's direction. */
		foreach (a in [h0 + 2, h1 - 2]) {
			if (!GSRail.BuildSignal(LT(line, a, 0), LT(line, a - 1, 0), GSRail.SIGNALTYPE_PBS_ONEWAY)) fail(key + "_ssignal");
			if (!GSRail.BuildSignal(LT(line, a, 1), LT(line, a + 1, 1), GSRail.SIGNALTYPE_PBS_ONEWAY)) fail(key + "_ssignal");
		}
		local mid = (h0 + h1) / 2;
		local depots = [];
		foreach (lane in [0, 1]) {
			local side = lane == 0 ? -1 : 2;   /* outside the pair of tracks */
			local depot = ax == "x" ? T(mid, c0 + side) : T(c0 + side, mid);
			local here = LT(line, mid, lane);
			local before = LT(line, mid - 1, lane);
			local after = LT(line, mid + 1, lane);
			if (!(GSRail.BuildRail(before, here, depot) && GSRail.BuildRail(depot, here, after) && GSRail.BuildRailDepot(depot, here))) fail(key + "_depot");
			depots.append(depot);
		}
		return depots;
	}

	/** Scissors crossover at along a: both diagonals between the two tracks, in front of a terminus. */
	function Scissors(line, a, key)
	{
		local d = line.depth;
		if (line.axis == "x") {
			foreach (t in [GSRail.RAILTRACK_NE_SW, GSRail.RAILTRACK_NE_SE, GSRail.RAILTRACK_SW_SE]) Rail(LT(line, a, 0), t, d, key);
			foreach (t in [GSRail.RAILTRACK_NE_SW, GSRail.RAILTRACK_NW_SW, GSRail.RAILTRACK_NW_NE]) Rail(LT(line, a, 1), t, d, key);
		} else {
			foreach (t in [GSRail.RAILTRACK_NW_SE, GSRail.RAILTRACK_NW_SW, GSRail.RAILTRACK_SW_SE]) Rail(LT(line, a, 0), t, d, key);
			foreach (t in [GSRail.RAILTRACK_NW_SE, GSRail.RAILTRACK_NE_SE, GSRail.RAILTRACK_NW_NE]) Rail(LT(line, a, 1), t, d, key);
		}
	}

	/**
	 * Turning loop beyond a terminus, so trains never have to reverse. In the
	 * frame of an X line ending at along e (loop towards higher along, rows
	 * c0 = lane 0, c0 + 1 = lane 1, c0 + 2 = loop row):
	 *   lane 0 runs on to e + 3 and turns across to the loop row (R, Y, U);
	 *   the loop row runs back to e + 1 where lane 1 joins it with a short
	 *   diagonal (L on the loop row, R on lane 1).
	 * \a at_end false mirrors it for the start terminus (loop towards lower along).
	 */
	function Loop(line, at_end, key)
	{
		local e = at_end ? line.e : line.s;
		local sg = at_end ? 1 : -1;
		local c0 = line.lane0;
		/* Local pieces: X along, Y across, R/L/U/D = NE_SE / NW_SW / NW_NE / SW_SE in the X end frame. */
		local pieces = [[1, 0, "X"], [2, 0, "X"], [3, 0, "R"], [3, 1, "Y"], [3, 2, "U"], [2, 2, "X"], [1, 2, "L"], [1, 1, "R"]];
		local mirror = { X = "X", Y = "Y", R = "D", L = "U", U = "L", D = "R" };
		local swap = { X = "Y", Y = "X", R = "L", L = "R", U = "U", D = "D" };
		local track = { X = GSRail.RAILTRACK_NE_SW, Y = GSRail.RAILTRACK_NW_SE, R = GSRail.RAILTRACK_NE_SE, L = GSRail.RAILTRACK_NW_SW, U = GSRail.RAILTRACK_NW_NE, D = GSRail.RAILTRACK_SW_SE };
		foreach (pc in pieces) {
			local name = pc[2];
			if (!at_end) name = mirror[name];
			if (line.axis == "y") name = swap[name];
			local a = e + sg * pc[0];
			local c = c0 + pc[1];
			Rail(line.axis == "x" ? T(a, c) : T(c, a), track[name], line.depth, key);
		}
	}

	/**
	 * Double-track line from along s to e with termini at both ends, stations
	 * starting at the alongs in line.stations, and a surface section line.hook.
	 * line.joins maps a station index (0 = start terminus, last = end terminus)
	 * to a station id to join. Returns the station ids in order.
	 */
	function BuildLine(line)
	{
		local key = line.name;
		local P = line.P;
		local s = line.s, e = line.e;
		GSRail.SetCurrentRailType(line.rt);

		/* Station starts: start terminus, intermediate stations, end terminus. */
		local starts = [s];
		foreach (m in line.stations) starts.append(m);
		starts.append(e - P + 1);

		local busy = {};
		local sids = [];
		foreach (i, m in starts) {
			local sid = (i in line.joins) ? line.joins[i] : null;
			for (local a = m; a < m + P; a++) {
				for (local lane = 0; lane < 2; lane++) sid = Platform(LT(line, a, lane), line.axis, line.depth, sid, key + "_platform");
				busy[a] <- true;
			}
			sids.append(sid);
		}
		local sc0 = s + P, sc1 = e - P;

		/* Move the surface section along the line until its area is dry land clear of the stations. */
		local h0 = line.hook[0], h1 = line.hook[1];
		foreach (shift in [0, 5, -5, 10, -10, 15, -15, 20, -20]) {
			local a0 = h0 + shift, a1 = h1 + shift;
			local clear = a0 - 3 > sc0 && a1 + 3 < sc1;
			foreach (m in line.stations) if (a1 + 3 >= m && a0 - 3 <= m + P - 1) clear = false;
			for (local a = a0 - 1; a <= a1 + 1 && clear; a++) for (local c = line.lane0 - 3; c <= line.lane0 + 4 && clear; c++) {
				local t = line.axis == "x" ? T(a, c) : T(c, a);
				if (GSTile.IsWaterTile(t) || GSTile.IsCoastTile(t)) clear = false;
			}
			if (clear) { line.hook = [a0, a1]; break; }
		}
		if (("terminus" in line) && line.terminus == "loop") {
			Loop(line, false, key + "_loop");
			Loop(line, true, key + "_loop");
		} else {
			/* The usual terminus: a scissors crossover in front of dead-end platforms. */
			Scissors(line, sc0, key + "_scissors");
			Scissors(line, sc1, key + "_scissors");
			busy[sc0] <- true;
			busy[sc1] <- true;
		}
		for (local a = line.hook[0]; a <= line.hook[1]; a++) busy[a] <- true;

		local track = AxisTrack(line.axis);
		for (local a = s; a <= e; a++) {
			if (a in busy) continue;
			for (local lane = 0; lane < 2; lane++) Rail(LT(line, a, lane), track, line.depth, key + "_track");
		}

		/* One-way signals: at the termini, round the stations and the surface section, and every 12 tiles. */
		local sig = {};
		sig[sc0 + 1] <- true;
		sig[sc1 - 1] <- true;
		foreach (m in line.stations) { sig[m - 1] <- true; sig[m + P] <- true; }
		sig[line.hook[0] - 2] <- true;
		sig[line.hook[1] + 2] <- true;
		for (local a = sc0 + 4; a < sc1 - 3; a += 12) sig[a] <- true;
		foreach (a, _ in sig) {
			if (a in busy || a <= sc0 || a >= sc1) continue;
			Signal(LT(line, a, 0), line.axis, 1, line.depth, key + "_signal");
			Signal(LT(line, a, 1), line.axis, -1, line.depth, key + "_signal");
		}

		line.depots <- Hook(line, line.hook[0], line.hook[1], key + "_hook");
		line.sids <- sids;
		return sids;
	}

	/**
	 * Double-track ring: outer rectangle (x0,y0)-(x1,y1) running clockwise, the
	 * inner one inset by a tile running anticlockwise. ring.stations lists
	 * {side, at} with side "top"/"right"/"bottom"/"left"; ring.hook is an x range
	 * on the top side. Returns the station ids in the order of ring.stations.
	 */
	function BuildRing(ring)
	{
		local key = ring.name;
		local d = ring.depth;
		local P = ring.P;
		GSRail.SetCurrentRailType(ring.rt);

		local busy = {};
		local sids = [];
		foreach (i, st in ring.stations) {
			local sid = (i in ring.joins) ? ring.joins[i] : null;
			for (local a = st.at; a < st.at + P; a++) {
				for (local k = 0; k < 2; k++) {
					local tile, axis;
					if (st.side == "top")    { tile = T(a, ring.y0 + k); axis = "x"; }
					if (st.side == "bottom") { tile = T(a, ring.y1 - k); axis = "x"; }
					if (st.side == "left")   { tile = T(ring.x0 + k, a); axis = "y"; }
					if (st.side == "right")  { tile = T(ring.x1 - k, a); axis = "y"; }
					sid = Platform(tile, axis, d, sid, key + "_platform");
					busy[tile] <- true;
				}
			}
			sids.append(sid);
		}
		for (local a = ring.hook[0]; a <= ring.hook[1]; a++) { busy[T(a, ring.y0)] <- true; busy[T(a, ring.y0 + 1)] <- true; }

		for (local k = 0; k < 2; k++) {
			local x0 = ring.x0 + k, y0 = ring.y0 + k, x1 = ring.x1 - k, y1 = ring.y1 - k;
			/* sides */
			for (local x = x0 + 1; x < x1; x++) {
				foreach (y in [y0, y1]) if (!(T(x, y) in busy)) Rail(T(x, y), GSRail.RAILTRACK_NE_SW, d, key + "_track");
			}
			for (local y = y0 + 1; y < y1; y++) {
				foreach (x in [x0, x1]) if (!(T(x, y) in busy)) Rail(T(x, y), GSRail.RAILTRACK_NW_SE, d, key + "_track");
			}
			/* corners */
			Rail(T(x0, y0), GSRail.RAILTRACK_SW_SE, d, key + "_corner");
			Rail(T(x1, y0), GSRail.RAILTRACK_NE_SE, d, key + "_corner");
			Rail(T(x1, y1), GSRail.RAILTRACK_NW_NE, d, key + "_corner");
			Rail(T(x0, y1), GSRail.RAILTRACK_NW_SW, d, key + "_corner");

			/* signals: outer clockwise, inner anticlockwise */
			local cw = k == 0 ? 1 : -1;
			for (local x = x0 + 3; x < x1 - 2; x += 12) {
				if (!(T(x, y0) in busy) && !(T(x - 1, y0) in busy) && !(T(x + 1, y0) in busy)) Signal(T(x, y0), "x", cw, d, key + "_signal");
				if (!(T(x, y1) in busy) && !(T(x - 1, y1) in busy) && !(T(x + 1, y1) in busy)) Signal(T(x, y1), "x", -cw, d, key + "_signal");
			}
			for (local y = y0 + 3; y < y1 - 2; y += 12) {
				if (!(T(x1, y) in busy) && !(T(x1, y - 1) in busy) && !(T(x1, y + 1) in busy)) Signal(T(x1, y), "y", cw, d, key + "_signal");
				if (!(T(x0, y) in busy) && !(T(x0, y - 1) in busy) && !(T(x0, y + 1) in busy)) Signal(T(x0, y), "y", -cw, d, key + "_signal");
			}
		}

		local hook = { axis = "x", lane0 = ring.y0, depth = d };
		ring.depots <- Hook(hook, ring.hook[0], ring.hook[1], key + "_hook");
		ring.sids <- sids;
		return sids;
	}

	/* ---------------- exits ---------------- */

	/** Pavilions beside both ends of a two-track platform block. */
	function Pavilions(tiles_axis, a0, a1, lane0, sid)
	{
		local n = 0;
		if (sid == null) return 0;
		foreach (spot in [[a0, lane0 - 2], [a1, lane0 + 3]]) {
			local tile = tiles_axis == "x" ? T(spot[0], spot[1]) : T(spot[1], spot[0]);
			if (GSTile.HasTreeOnTile(tile)) GSTile.DemolishTile(tile);
			if (GSMetro.BuildExitSurface(tile, sid, GSMetro.EXIT_PAVILION)) n++;
		}
		return n;
	}

	function Underpasses(sid, centre, limit)
	{
		if (sid == null) return 0;
		local cx = GSMap.GetTileX(centre), cy = GSMap.GetTileY(centre);
		local n = 0;
		for (local r = 1; r <= 6 && n < limit; r++) {
			for (local dy = -r; dy <= r && n < limit; dy++) for (local dx = -r; dx <= r && n < limit; dx++) {
				if (max(abs(dx), abs(dy)) != r) continue;
				local tile = T(cx + dx, cy + dy);
				if (!GSRoad.IsRoadTile(tile) || GSTile.IsStationTile(tile)) continue;
				if (GSMetro.GetExitType(tile) != GSMetro.EXIT_INVALID) continue;
				if (GSMetro.BuildExitSurface(tile, sid, GSMetro.EXIT_UNDERPASS)) n++;
			}
		}
		return n;
	}

	function Name(sid, name)
	{
		if (sid != null && GSStation.IsValidStation(sid)) GSBaseStation.SetName(sid, name);
	}

	/* ---------------- vehicles ---------------- */

	function FindRailType(name_part, max_speed)
	{
		local rts = GSRailTypeList();
		for (local rt = rts.Begin(); !rts.IsEnd(); rt = rts.Next()) {
			if (GSRail.GetName(rt).find(name_part) != null && GSRail.GetMaxSpeed(rt) == max_speed) return rt;
		}
		return null;
	}

	/** Best passenger engine on \a rt: fastest, then most powerful. */
	function FindEngine(rt, need_capacity)
	{
		local best = null, key = -1;
		local engines = GSEngineList(GSVehicle.VT_RAIL);
		for (local e = engines.Begin(); !engines.IsEnd(); e = engines.Next()) {
			if (!GSEngine.IsBuildable(e) || GSEngine.IsWagon(e) || GSEngine.GetRailType(e) != rt) continue;
			if (GSEngine.GetPower(e) <= 0) continue;
			if (need_capacity && GSEngine.GetCapacity(e) <= 0) continue;
			local k = GSEngine.GetMaxSpeed(e) * 100000 + GSEngine.GetPower(e);
			if (k > key) { best = e; key = k; }
		}
		return best;
	}

	/** Passenger wagon that runs on \a rt: fastest, then largest. */
	function FindWagon(rt, cargo)
	{
		local best = null, key = -1;
		local engines = GSEngineList(GSVehicle.VT_RAIL);
		for (local e = engines.Begin(); !engines.IsEnd(); e = engines.Next()) {
			if (!GSEngine.IsBuildable(e) || !GSEngine.IsWagon(e)) continue;
			if (!GSEngine.CanRunOnRail(e, rt) || !GSEngine.CanRefitCargo(e, cargo) && GSEngine.GetCargoType(e) != cargo) continue;
			/* Wagons of the line's own track type first (metro cars for the metro, EMU cars for the EMUs). */
			local own = GSEngine.GetRailType(e) == rt ? 1 : 0;
			local k = own * 10000000 + min(GSEngine.GetMaxSpeed(e), 250) * 1000 + GSEngine.GetCapacity(e);
			if (k > key) { best = e; key = k; }
		}
		return best;
	}

	/**
	 * Train of \a engine and \a n wagons. Multiple units (EMUs, metro sets)
	 * need a cab at both ends, so with \a tail a second head car closes the set.
	 */
	function MakeTrain(depot, engine, wagon, n, cargo, tail = false)
	{
		local v = GSVehicle.BuildVehicle(depot, engine);
		if (v == GSVehicle.VEHICLE_INVALID) { fail("train"); return v; }
		if (wagon != null) {
			for (local i = 0; i < n; i++) {
				local w = GSVehicle.BuildVehicleWithRefit(depot, wagon, cargo);
				if (w == GSVehicle.VEHICLE_INVALID) w = GSVehicle.BuildVehicle(depot, wagon);
				if (w == GSVehicle.VEHICLE_INVALID || !GSVehicle.MoveWagon(w, 0, v, GSVehicle.GetNumWagons(v) - 1)) { fail("wagon"); break; }
			}
		}
		if (tail) {
			local h = GSVehicle.BuildVehicle(depot, engine);
			if (h == GSVehicle.VEHICLE_INVALID || !GSVehicle.MoveWagon(h, 0, v, GSVehicle.GetNumWagons(v) - 1)) fail("tail");
		}
		return v;
	}

	function Orders(v, sids)
	{
		foreach (sid in sids) {
			if (sid == null || !this.trains.append_underground_order(v, sid, GSOrder.OF_NON_STOP_INTERMEDIATE)) fail("order");
		}
	}

	/* ---------------- main ---------------- */

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		this.report = MetroTest("metro_city_network", null, 510901);
		this.build = MetroTest.MetroBuildLib();
		this.metro = MetroTest.MetroLib(this.build);
		this.world = MetroTest.MetroWorldLib(this.build);
		this.trains = MetroTest.MetroTrainLib();
		this.fails = {};
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);

		try {
			local company = companies.ensure_company();
			local cargo = this.world.find_passenger_cargo();
			local cm = GSCompanyMode(company);

			/* Metro v2 builds nothing below the sea and keeps rivers clear down to their
			 * depth: raise the map's water out of it first, so the network has the dry
			 * land it was laid out on. */
			local wet = 0;
			for (local pass = 0; pass < 4; pass++) {
				local left = 0;
				for (local t = 0; t < GSMap.GetMapSize(); t++) {
					if (!GSTile.IsSeaTile(t) && !GSTile.IsRiverTile(t) && !GSTile.IsWaterTile(t)) continue;
					left++;
					/* A river is removed, the sea raised out of the water. */
					if (!GSTile.IsRiverTile(t) || !GSTile.DemolishTile(t)) GSTile.RaiseTile(t, GSTile.SLOPE_ELEVATED);
				}
				if (pass == 0) wet = left;
				if (left == 0) break;
			}
			report.metric("raised_water_tiles", wet);

			local rt_hs = FindRailType("DC 3kV", 249);
			local rt_sub = FindRailType("DC 3kV", 140);
			local rt_metro = FindRailType("Third rail railroad (left)", 100);
			ok("rail_types", rt_hs != null && rt_sub != null && rt_metro != null, "Expected the xUSSR high-speed, suburban and third-rail track");
			report.metric("rail_types", rt_hs + "/" + rt_sub + "/" + rt_metro);

			local C = 256;           /* centre of the map and of the central city */
			local L0 = -1, LM = -1, LEW = -2, LNS = -3, LEDGE = -2;

			/* ---------- high-speed ring (level 0) ---------- */
			local hs = {
				name = "hs", rt = rt_hs, depth = L0, P = 10,
				x0 = 96, y0 = 96, x1 = 416, y1 = 416,
				stations = [{ side = "top", at = 251 }, { side = "right", at = 251 }, { side = "bottom", at = 251 }, { side = "left", at = 251 }],
				joins = {}, hook = [140, 152],
			};
			BuildRing(hs);

			/* ---------- suburban radial lines (level 0) ---------- */
			local sub = [
				{ name = "sub_n", axis = "y", lane0 = 262, s = 102, e = 218, stations = [], hook = [150, 162] },
				{ name = "sub_e", axis = "x", lane0 = 262, s = 294, e = 410, stations = [], hook = [350, 362] },
				{ name = "sub_s", axis = "y", lane0 = 248, s = 294, e = 410, stations = [], hook = [350, 362] },
				{ name = "sub_w", axis = "x", lane0 = 248, s = 102, e = 218, stations = [], hook = [150, 162] },
			];
			foreach (l in sub) { l.rt <- rt_sub; l.depth <- L0; l.P <- 6; l.joins <- {}; }
			/* The edge-town terminus of each line joins the high-speed station there. */
			sub[0].joins[0] <- hs.sids[0];
			sub[1].joins[1] <- hs.sids[1];
			sub[2].joins[1] <- hs.sids[2];
			sub[3].joins[0] <- hs.sids[3];
			foreach (l in sub) BuildLine(l);

			/* ---------- central metro ring (level -1) ---------- */
			local ring = {
				name = "ring", rt = rt_metro, depth = LM, P = 4,
				x0 = 226, y0 = 226, x1 = 286, y1 = 286,
				stations = [{ side = "top", at = 254 }, { side = "right", at = 254 }, { side = "bottom", at = 254 }, { side = "left", at = 254 }],
				joins = { [0] = sub[0].sids[1], [1] = sub[1].sids[0], [2] = sub[2].sids[0], [3] = sub[3].sids[1] },
				hook = [268, 278],
			};
			BuildRing(ring);

			/* ---------- central diameters (levels -2 and -3) ---------- */
			local ew = { name = "ew", axis = "x", lane0 = 255, s = 196, e = 316, stations = [224, 254, 285], hook = [295, 305], depth = LEW };
			local ns = { name = "ns", axis = "y", lane0 = 255, s = 196, e = 316, stations = [224, 254, 285], hook = [295, 305], depth = LNS };
			foreach (l in [ew, ns]) { l.rt <- rt_metro; l.P <- 4; }
			ew.joins <- { [1] = ring.sids[3], [3] = ring.sids[1] };
			BuildLine(ew);
			ns.joins <- { [1] = ring.sids[0], [2] = ew.sids[2], [3] = ring.sids[2] };
			BuildLine(ns);

			/* ---------- edge-town metros (level -1) ---------- */
			local em = [
				{ name = "m_n", axis = "x", lane0 = 92, s = 226, e = 296, stations = [254], hook = [266, 276], hub = 0 },
				{ name = "m_e", axis = "y", lane0 = 420, s = 226, e = 296, stations = [254], hook = [266, 276], hub = 1 },
				{ name = "m_s", axis = "x", lane0 = 420, s = 226, e = 296, stations = [254], hook = [266, 276], hub = 2 },
				{ name = "m_w", axis = "y", lane0 = 92, s = 226, e = 296, stations = [254], hook = [266, 276], hub = 3 },
			];
			foreach (l in em) { l.rt <- rt_metro; l.depth <- LEDGE; l.P <- 4; l.joins <- { [1] = hs.sids[l.hub] }; BuildLine(l); }

			/* Names */
			local edge_names = ["Severograd", "Vostochny", "Yuzhny", "Zapadny"];
			foreach (i, n in edge_names) Name(hs.sids[i], n + " Station");
			foreach (i, n in ["Central North", "Central East", "Central South", "Central West"]) Name(ring.sids[i], n);
			Name(ew.sids[2], "Central Square");
			foreach (i, l in [ew, ns]) {
				Name(l.sids[0], (i == 0 ? "Zavodskaya" : "Parkovaya"));
				Name(l.sids[4], (i == 0 ? "Rechnaya" : "Lesnaya"));
			}
			foreach (i, l in em) {
				Name(l.sids[0], edge_names[i] + " " + (l.axis == "x" ? "West" : "North") + " End");
				Name(l.sids[2], edge_names[i] + " " + (l.axis == "x" ? "East" : "South") + " End");
			}

			/* ---------- pavilions before the towns take the land ---------- */
			local pav = 0;
			foreach (l in sub) foreach (i, sid in l.sids) {
				local m = i == 0 ? l.s : l.e - l.P + 1;
				pav += Pavilions(l.axis, m, m + l.P - 1, l.lane0, sid);
			}
			foreach (l in [ew, ns]) foreach (i, sid in l.sids) {
				local m = i == 0 ? l.s : (i == 4 ? l.e - l.P + 1 : l.stations[i - 1]);
				pav += Pavilions(l.axis, m, m + l.P - 1, l.lane0, sid);
			}
			foreach (l in em) foreach (i, sid in l.sids) {
				local m = i == 0 ? l.s : (i == 2 ? l.e - l.P + 1 : l.stations[0]);
				pav += Pavilions(l.axis, m, m + l.P - 1, l.lane0, sid);
			}
			foreach (r in [hs, ring]) foreach (i, st in r.stations) {
				local sid = r.sids[i];
				if (st.side == "top")    pav += Pavilions("x", st.at, st.at + r.P - 1, r.y0, sid);
				if (st.side == "bottom") pav += Pavilions("x", st.at, st.at + r.P - 1, r.y1 - 1, sid);
				if (st.side == "left")   pav += Pavilions("y", st.at, st.at + r.P - 1, r.x0, sid);
				if (st.side == "right")  pav += Pavilions("y", st.at, st.at + r.P - 1, r.x1 - 1, sid);
			}
			report.metric("pavilions", pav);
			cm = null;

			/* ---------- towns ---------- */
			local towns = [];
			foreach (t in [[C - 12, C - 12, "Centralnogorsk", 1400], [256, 84, "Severograd", 700], [428, 256, "Vostochny", 700],
					[256, 428, "Yuzhny", 700], [84, 256, "Zapadny", 700]]) {
				local id = this.world.create_town_near(T(t[0], t[1]), 10, t[2], 0);
				if (id == null) { fail("town"); continue; }
				towns.append([id, t[3]]);
			}
			foreach (t in towns) GSTown.ExpandTown(t[0], t[1] / 2);
			foreach (t in towns) GSTown.ExpandTown(t[0], t[1] / 2);
			report.metric("towns", towns.len());

			/* ---------- underpasses on the grown roads ---------- */
			cm = GSCompanyMode(company);
			local up = 0;
			foreach (i, sid in hs.sids) up += Underpasses(sid, GSStation.GetLocation(sid), 4);
			foreach (sid in ring.sids) up += Underpasses(sid, GSStation.GetLocation(sid), 3);
			foreach (l in [ew, ns]) foreach (sid in l.sids) up += Underpasses(sid, GSStation.GetLocation(sid), 2);
			foreach (l in em) foreach (sid in l.sids) up += Underpasses(sid, GSStation.GetLocation(sid), 2);
			report.metric("underpasses", up);

			/* ---------- trains ---------- */
			local e_hs = FindEngine(rt_hs, false), w_hs = FindWagon(rt_hs, cargo);
			local e_sub = FindEngine(rt_sub, true), w_sub = FindWagon(rt_sub, cargo);
			local e_m = FindEngine(rt_metro, true), w_m = FindWagon(rt_metro, cargo);
			ok("engines", e_hs != null && e_sub != null && e_m != null, "Expected engines for all three track types");
			report.metric("stock", GSEngine.GetName(e_hs) + " + " + (w_hs == null ? "-" : GSEngine.GetName(w_hs)) + " | " +
					GSEngine.GetName(e_sub) + " + " + (w_sub == null ? "-" : GSEngine.GetName(w_sub)) + " | " +
					GSEngine.GetName(e_m) + " + " + (w_m == null ? "-" : GSEngine.GetName(w_m)));

			local fleet = [];
			/* High-speed ring: two trains each way. */
			local cw = [hs.sids[0], hs.sids[1], hs.sids[2], hs.sids[3]];
			local ccw = [hs.sids[0], hs.sids[3], hs.sids[2], hs.sids[1]];
			for (local i = 0; i < 2; i++) {
				local v = MakeTrain(hs.depots[0], e_hs, w_hs, 7, cargo); if (v != GSVehicle.VEHICLE_INVALID) { Orders(v, cw); fleet.append(v); }
				v = MakeTrain(hs.depots[1], e_hs, w_hs, 7, cargo); if (v != GSVehicle.VEHICLE_INVALID) { Orders(v, ccw); fleet.append(v); }
			}
			/* Suburban: two EMUs per line, one from each depot. */
			foreach (l in sub) foreach (dep in l.depots) {
				local v = MakeTrain(dep, e_sub, w_sub, 2, cargo, true); if (v != GSVehicle.VEHICLE_INVALID) { Orders(v, l.sids); fleet.append(v); }
			}
			/* Metro ring: two trains each way. */
			local rcw = [ring.sids[0], ring.sids[1], ring.sids[2], ring.sids[3]];
			local rccw = [ring.sids[0], ring.sids[3], ring.sids[2], ring.sids[1]];
			for (local i = 0; i < 2; i++) {
				local v = MakeTrain(ring.depots[0], e_m, w_m, 2, cargo, true); if (v != GSVehicle.VEHICLE_INVALID) { Orders(v, rcw); fleet.append(v); }
				v = MakeTrain(ring.depots[1], e_m, w_m, 2, cargo, true); if (v != GSVehicle.VEHICLE_INVALID) { Orders(v, rccw); fleet.append(v); }
			}
			/* Metro lines: two trains each, there and back. */
			foreach (l in [ew, ns, em[0], em[1], em[2], em[3]]) {
				local route = clone l.sids;
				for (local i = l.sids.len() - 2; i > 0; i--) route.append(l.sids[i]);
				foreach (dep in l.depots) {
					local v = MakeTrain(dep, e_m, w_m, 2, cargo, true); if (v != GSVehicle.VEHICLE_INVALID) { Orders(v, route); fleet.append(v); }
				}
			}
			report.metric("trains", fleet.len());
			foreach (v in fleet) {
				local nm = GSEngine.GetName(GSVehicle.GetEngineType(v));
				if (!(("len_" + nm) in report._metrics)) report.metric("len_" + nm, GSVehicle.GetLength(v));
			}

			local nostart = 0;
			foreach (v in fleet) {
				if (!GSVehicle.StartStopVehicle(v)) {
					nostart++;
					if (nostart <= 3) report.metric("nostart_" + v, GSEngine.GetName(GSVehicle.GetEngineType(v)) + " wagons=" + GSVehicle.GetNumWagons(v) + " err=" + GSError.GetLastErrorString());
				}
				GSController.Sleep(40);
			}
			report.metric("nostart", nostart);

			/* ---------- run ---------- */
			local calls = {}, was = {}, last = {};
			foreach (v in fleet) { calls[v] <- 0; was[v] <- false; last[v] <- ""; }
			for (local t = 0; t < 40000; t += 20) {
				foreach (v in fleet) {
					if (GSVehicle.IsValidVehicle(v) && GSVehicle.GetState(v) != GSVehicle.VS_CRASHED) {
						local l = GSVehicle.GetLocation(v);
						last[v] = "t=" + t + " " + GSEngine.GetName(GSVehicle.GetEngineType(v)) + " at " + GSMap.GetTileX(l) + "," + GSMap.GetTileY(l) + " ug=" + GSMetro.IsVehicleUnderground(v) + " d=" + GSMetro.GetVehicleUndergroundDepth(v);
					}
					local at = GSVehicle.IsValidVehicle(v) && GSVehicle.GetState(v) == GSVehicle.VS_AT_STATION;
					if (at && !was[v]) calls[v]++;
					was[v] = at;
				}
				GSController.Sleep(20);
			}
			local crashed = 0, idle = 0, total = 0;
			foreach (v in fleet) {
				if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) {
					crashed++;
					report.metric("lost_" + v, last[v]);
					if (GSVehicle.IsValidVehicle(v)) report.metric("crashed_" + v, GSEngine.GetName(GSVehicle.GetEngineType(v)) + " at " + GSMap.GetTileX(GSVehicle.GetLocation(v)) + "," + GSMap.GetTileY(GSVehicle.GetLocation(v)) + " ug=" + GSMetro.IsVehicleUnderground(v));
				}
				else if (calls[v] < 2) { idle++; report.metric("idle_" + v, GSEngine.GetName(GSVehicle.GetEngineType(v)) + " at " + GSMap.GetTileX(GSVehicle.GetLocation(v)) + "," + GSMap.GetTileY(GSVehicle.GetLocation(v)) + " state " + GSVehicle.GetState(v) + " spd " + GSVehicle.GetCurrentSpeed(v) + " ug " + GSMetro.IsVehicleUnderground(v) + " d " + GSMetro.GetVehicleUndergroundDepth(v) + " order " + GSOrder.ResolveOrderPosition(v, GSOrder.ORDER_CURRENT) + " calls " + calls[v]); }
				total += calls[v];
			}
			report.metric("station_calls", total);
			report.check_eq("no_crashes", 0, crashed, "No train should crash");
			report.metric("idle_trains", idle);
			foreach (k, n in this.fails) report.metric("failcount_" + k, n);
			cm = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_city_network_final.sav"), "Network save should be written.");
		report.finish();
	}
}
