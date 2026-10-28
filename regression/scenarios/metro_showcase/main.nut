/**
 * metro_showcase
 *
 * Demo network for screenshots and a downloadable savegame, not a narrow
 * regression: two metro lines under a grown city agglomeration.
 *
 * Red line (ordinary rail, level D1): an east-west line that is a loop at
 * the west end only. Rows yA (eastbound) and yB = yA + 2 (westbound) run
 * underground from the west portals to the terminus S3, whose platforms end
 * in buffer stops behind a scissors crossover: trains reverse there and
 * cross to the westbound row. The west end is a short surface hook with the
 * depot. Stations S1, S2, S3 have a 3-tile platform on each row.
 *
 * Blue line (electrified rail, levels D2 and D3): a north-south loop on
 * columns xc1 (southbound) and xc2 = xc1 + 2 (northbound) that passes under
 * the red line. Its interchange platforms lie directly below the red S2
 * platforms and join the same station; ramps take it down to D3 for the deep
 * station Q and the U-turn. The north end is a surface hook with the depot.
 *
 *            hook+depot (blue)
 *                 ||
 *               R (D2)
 *   hook ==S1======S2/P===X=S3|      red, D1 (X = scissors, | = buffer stops)
 *   depot  ==S1======S2/P===X=S3|
 *                 ||  ramps D2 -> D3
 *               Q (D3)
 *                 U
 *
 * Exits: pavilions are reserved before the towns grow, underpasses are
 * placed on straight town roads afterwards.
 *
 * Signal lab (ordinary rail, south of the city, see BuildSignalLab): five
 * stations on a double-track line that is no loop, part on the surface and
 * part below, with portals down and up at both ends, splits into a bypass, a
 * pre-signalled hub and a single-track branch, and every signal type (light
 * and semaphore underground, vanilla ones on the surface). Ten trains on
 * three kinds of routes must keep running without a collision.
 */
class MetroShowcase extends GSController {
	report = null;
	build = null;
	metro = null;
	world = null;
	trains = null;

	function T(x, y) { return GSMap.GetTileIndex(x, y); }

	function ok(id, cond, message)
	{
		this.report.check_true(id, cond, message + " (err=" + GSError.GetLastErrorString() + ")");
		return cond;
	}

	/* Platform of `len` tiles along x starting at (x, y), all one station. */
	function station_x(x, y, len, depth, sid)
	{
		for (local i = 0; i < len; i++) {
			if (!GSMetro.BuildUndergroundStation(T(x + i, y), GSRail.RAILTRACK_NE_SW, depth, sid)) return null;
			if (sid == GSStation.STATION_NEW) sid = GSMetro.GetUndergroundStationID(T(x, y), depth);
		}
		return sid;
	}

	function station_y(x, y, len, depth, sid)
	{
		for (local i = 0; i < len; i++) {
			if (!GSMetro.BuildUndergroundStation(T(x, y + i), GSRail.RAILTRACK_NW_SE, depth, sid)) return null;
			if (sid == GSStation.STATION_NEW) sid = GSMetro.GetUndergroundStationID(T(x, y), depth);
		}
		return sid;
	}

	function pavilion(x, y, sid)
	{
		local tile = T(x, y);
		GSTile.DemolishTile(tile);
		return GSMetro.BuildExitSurface(tile, sid, GSMetro.EXIT_PAVILION);
	}

	/* Underpasses on straight town roads within `dist` of the given platform tiles. */
	function underpasses(sid, platforms, dist, limit)
	{
		local built = 0;
		foreach (p in platforms) {
			local px = GSMap.GetTileX(p);
			local py = GSMap.GetTileY(p);
			for (local dy = -dist; dy <= dist && built < limit; dy++) {
				for (local dx = -dist; dx <= dist && built < limit; dx++) {
					local tile = T(px + dx, py + dy);
					if (!GSRoad.IsRoadTile(tile) || GSTile.IsStationTile(tile)) continue;
					if (GSMetro.GetExitType(tile) != GSMetro.EXIT_INVALID) continue;
					if (GSMetro.BuildExitSurface(tile, sid, GSMetro.EXIT_UNDERPASS)) built++;
				}
			}
		}
		return built;
	}

	function best_engine(railtype, electric_only, other_railtype)
	{
		local best = null;
		local best_speed = -1;
		local engines = GSEngineList(GSVehicle.VT_RAIL);
		for (local e = engines.Begin(); !engines.IsEnd(); e = engines.Next()) {
			if (!GSEngine.IsBuildable(e) || GSEngine.IsWagon(e)) continue;
			if (GSEngine.GetRailType(e) != railtype) continue;
			
			local speed = GSEngine.GetMaxSpeed(e);
			if (speed > best_speed) { best = e; best_speed = speed; }
		}
		return best;
	}

	function make_train(depot, engine, cargo, wagons)
	{
		local v = this.trains.build_train(depot, engine);
		if (v == GSVehicle.VEHICLE_INVALID) return v;
		for (local i = 0; i < wagons; i++) this.trains.attach_cargo_wagon(v, depot, cargo);
		return v;
	}

	/* Level a rectangle to height base_h in 8x8 passes (one LevelTiles over a large area runs out of terraform burst). */
	function level_rect(x1, y1, x2, y2, base_h)
	{
		for (local y = y1; y <= y2; y++) {
			for (local x = x1; x <= x2; x++) {
				if (GSTile.HasTreeOnTile(T(x, y))) GSTile.DemolishTile(T(x, y));
			}
		}
		local flat = false;
		for (local pass = 0; pass < 12 && !flat; pass++) {
			flat = true;
			for (local cy = y1; cy <= y2; cy += 8) {
				for (local cx = x1; cx <= x2; cx += 8) {
					local need = false;
					for (local y = cy; y < cy + 8 && !need; y++) {
						for (local x = cx; x < cx + 8 && !need; x++) {
							if (GSTile.GetSlope(T(x, y)) != GSTile.SLOPE_FLAT || GSTile.GetMinHeight(T(x, y)) != base_h) need = true;
						}
					}
					if (!need) continue;
					flat = false;
					for (local i = 0; i < 4 && GSTile.GetCornerHeight(T(cx, cy), GSTile.CORNER_N) < base_h; i++) GSTile.RaiseTile(T(cx, cy), GSTile.SLOPE_N);
					for (local i = 0; i < 4 && GSTile.GetCornerHeight(T(cx, cy), GSTile.CORNER_N) > base_h; i++) GSTile.LowerTile(T(cx, cy), GSTile.SLOPE_N);
					GSTile.LevelTiles(T(cx, cy), T(cx + 8, cy + 8));
				}
			}
			GSController.Sleep(20);
		}
		return flat;
	}

	/**
	 * Underground signal of a type and variant facing east (+x) or west on an
	 * X track, or north/south on a Y track. New block-type signals face both
	 * ways and path signals west (north on Y); building again turns them.
	 */
	function lab_signal(x, y, depth, track, type, variant, forward)
	{
		local pbs = type == GSRail.SIGNALTYPE_PBS || type == GSRail.SIGNALTYPE_PBS_ONEWAY;
		/* forward = east on X, south on Y. */
		local builds = pbs ? (forward ? 2 : 1) : (forward ? 3 : 2);
		if (track == GSRail.RAILTRACK_NW_SE) builds = pbs ? (forward ? 1 : 2) : (forward ? 2 : 3);
		for (local i = 0; i < builds; i++) {
			if (!GSMetro.BuildUndergroundSignal(T(x, y), track, depth, type, variant)) return false;
		}
		return GSMetro.GetUndergroundSignalType(T(x, y), track, depth) == type &&
			GSMetro.GetUndergroundSignalVariant(T(x, y), track, depth) == variant &&
			!GSMetro.IsUndergroundSignalTwoWay(T(x, y), track, depth);
	}

	/**
	 * The signal lab: a separate network south of the city that runs trains
	 * past every signal type, underground (light and semaphore) and on the
	 * surface, with portals down and up at both ends and junctions everywhere.
	 * Double track, eastbound row Y0, westbound row Y0 + 2 (Y0 + 1 is the gap
	 * row of crossovers); x offsets from X0:
	 *
	 *  surface W (mixed station)        underground                                      surface E
	 *  hook+depot  [W W W] portal [W W W] -- M bypass -- [C hub, pre-signals] -- S --  portal X [E E E]|
	 *                                         split/merge    split/merge        branch       scissors
	 *
	 * W: surface platforms at -6..-4 joined with underground platforms at
	 *    2..4 (part of the station on the surface, part below).
	 * M: bypass platforms on rows Y0 - 1 / Y0 + 3 between splits at 9 and 19.
	 * C: hub, both directions split into two platforms (28..30); eastbound
	 *    entry/exit pre-signals with block departure signals, westbound
	 *    entry/combo/exit chain.
	 * S: single-track terminus branch (column 42, platforms Y0+6..+8) reached
	 *    from the eastbound row over a crossover at 38 and the westbound row;
	 *    path signals only, a two-way passable one on the branch.
	 * E: surface terminus behind a scissors crossover, past the portals at 46.
	 */
	function BuildSignalLab(X0, Y0, rail)
	{
		local report = this.report;
		local build = this.build;
		local metro = this.metro;
		local lab = { ok = false };

		local flat = this.level_rect(X0 - 17, Y0 - 3, X0 + 60, Y0 + 11, 1);
		report.metric("lab_area_flat", flat ? 1 : 0);
		local D = GSTile.GetMinHeight(T(X0, Y0)) - 1;
		local E = GSMetro.SIGNAL_VARIANT_ELECTRIC, SEM = GSMetro.SIGNAL_VARIANT_SEMAPHORE;
		local X = GSRail.RAILTRACK_NE_SW;
		GSRail.SetCurrentRailType(rail);

		/* ---- West: surface hook, depot and the surface half of W ---- */
		local xh = X0 - 14;
		local sidW = null;
		if (GSRail.BuildRailStation(T(X0 - 6, Y0), X, 3, 3, GSStation.STATION_NEW)) sidW = GSStation.GetStationID(T(X0 - 6, Y0));
		ok("lab_w_surface_station", sidW != null, "Lab: surface platforms of W");
		foreach (y in [Y0, Y0 + 2]) {
			ok("lab_w_rows_" + y, build.build_surface_x(xh + 1, X0 - 7, y) && build.build_surface_x(X0 - 3, X0 - 1, y), "Lab: west surface rows");
		}
		ok("lab_hook",
			GSRail.BuildRail(T(xh + 1, Y0 + 2), T(xh, Y0 + 2), T(xh, Y0 + 1)) &&
			GSRail.BuildRail(T(xh, Y0 + 2), T(xh, Y0 + 1), T(xh, Y0)) &&
			GSRail.BuildRail(T(xh, Y0 + 1), T(xh, Y0), T(xh + 1, Y0)), "Lab: west hook");
		lab.depot <- T(xh + 2, Y0 - 1);
		ok("lab_depot",
			GSRail.BuildRail(T(xh + 1, Y0), T(xh + 2, Y0), lab.depot) &&
			GSRail.BuildRail(lab.depot, T(xh + 2, Y0), T(xh + 3, Y0)) &&
			GSRail.BuildRailDepot(lab.depot, T(xh + 2, Y0)), "Lab: depot");
		/* Surface signals (front = the side trains come from): block, then an
		 * entry/exit pair round the W platform eastbound; westbound a combo
		 * before the platform and an exit behind it. */
		ok("lab_w_surface_signals",
			GSRail.BuildSignal(T(X0 - 10, Y0), T(X0 - 11, Y0), GSRail.SIGNALTYPE_NORMAL) &&
			GSRail.BuildSignal(T(X0 - 8, Y0), T(X0 - 9, Y0), GSRail.SIGNALTYPE_ENTRY) &&
			GSRail.BuildSignal(T(X0 - 3, Y0), T(X0 - 4, Y0), GSRail.SIGNALTYPE_EXIT) &&
			GSRail.BuildSignal(T(X0 - 2, Y0 + 2), T(X0 - 1, Y0 + 2), GSRail.SIGNALTYPE_COMBO) &&
			GSRail.BuildSignal(T(X0 - 8, Y0 + 2), T(X0 - 7, Y0 + 2), GSRail.SIGNALTYPE_EXIT), "Lab: west surface signals");
		ok("lab_portals_w", metro.build_portal(T(X0, Y0), GSMetro.DIAGDIR_SW, D) && metro.build_portal(T(X0, Y0 + 2), GSMetro.DIAGDIR_SW, D), "Lab: west portals (down eastbound, up westbound)");

		/* ---- Underground stations (before the track, which skips over them) ---- */
		local w_ug = sidW != null && this.station_x(X0 + 2, Y0, 3, D, sidW) != null && this.station_x(X0 + 2, Y0 + 2, 3, D, sidW) != null;
		ok("lab_w_underground", w_ug, "Lab: underground platforms of W, joined to its surface platforms");
		local sidM = this.station_x(X0 + 13, Y0 - 1, 3, D, GSStation.STATION_NEW);
		if (sidM != null && this.station_x(X0 + 13, Y0 + 3, 3, D, sidM) == null) sidM = null;
		local sidC = this.station_x(X0 + 28, Y0 - 1, 3, D, GSStation.STATION_NEW);
		foreach (y in [Y0, Y0 + 2, Y0 + 3]) if (sidC != null && this.station_x(X0 + 28, y, 3, D, sidC) == null) sidC = null;
		local sidS = this.station_y(X0 + 42, Y0 + 6, 3, D, GSStation.STATION_NEW);
		ok("lab_ug_stations", sidM != null && sidC != null && sidS != null, "Lab: underground stations M, C, S");

		/* ---- Underground track ---- */
		local track_ok = true;
		local rail_x = function(x1, x2, y) : (D) {
			local okk = true;
			for (local x = x1; x <= x2; x++) if (!GSMetro.BuildUndergroundRail(GSMap.GetTileIndex(x, y), GSRail.RAILTRACK_NE_SW, D)) okk = false;
			return okk;
		};
		track_ok = rail_x(X0 + 1, X0 + 45, Y0) && rail_x(X0 + 1, X0 + 45, Y0 + 2) && track_ok;
		/* Bypass rows and hub rows (on top of the platforms: free no-ops). */
		track_ok = rail_x(X0 + 10, X0 + 18, Y0 - 1) && rail_x(X0 + 10, X0 + 18, Y0 + 3) && track_ok;
		track_ok = rail_x(X0 + 26, X0 + 32, Y0 - 1) && rail_x(X0 + 26, X0 + 32, Y0 + 3) && track_ok;
		local piece = function(dx, y, t) : (X0, D) { return GSMetro.BuildUndergroundRail(GSMap.GetTileIndex(X0 + dx, y), t, D); };
		/* Splits: eastbound Y0 -> Y0-1 at the west end and back at the east end, westbound Y0+2 -> Y0+3 at the east end and back at the west end. */
		foreach (s in [[9, 19], [25, 33]]) {
			track_ok = piece(s[0], Y0, GSRail.RAILTRACK_NW_NE) && piece(s[0], Y0 - 1, GSRail.RAILTRACK_SW_SE) &&
				piece(s[1], Y0 - 1, GSRail.RAILTRACK_NE_SE) && piece(s[1], Y0, GSRail.RAILTRACK_NW_SW) &&
				piece(s[1], Y0 + 2, GSRail.RAILTRACK_SW_SE) && piece(s[1], Y0 + 3, GSRail.RAILTRACK_NW_NE) &&
				piece(s[0], Y0 + 3, GSRail.RAILTRACK_NW_SW) && piece(s[0], Y0 + 2, GSRail.RAILTRACK_NE_SE) && track_ok;
		}
		/* S branch: crossover from the eastbound to the westbound row, then down column 42. */
		track_ok = piece(38, Y0, GSRail.RAILTRACK_NE_SE) && piece(38, Y0 + 1, GSRail.RAILTRACK_NW_SW) &&
			piece(39, Y0 + 1, GSRail.RAILTRACK_NE_SE) && piece(39, Y0 + 2, GSRail.RAILTRACK_NW_SW) &&
			piece(42, Y0 + 2, GSRail.RAILTRACK_NE_SE) && track_ok;
		for (local y = Y0 + 3; y <= Y0 + 5; y++) track_ok = piece(42, y, GSRail.RAILTRACK_NW_SE) && track_ok;
		ok("lab_ug_track", track_ok, "Lab: underground track, splits and branch");

		/* ---- Underground signals: every type, light and semaphore ---- */
		local sigs = [
			/* x, row, type, variant, east */
			[6, Y0, GSRail.SIGNALTYPE_NORMAL, E, true],
			[14, Y0, GSRail.SIGNALTYPE_NORMAL, E, true],
			[21, Y0, GSRail.SIGNALTYPE_COMBO, E, true],
			[12, Y0 - 1, GSRail.SIGNALTYPE_PBS, E, true],
			[17, Y0 - 1, GSRail.SIGNALTYPE_PBS_ONEWAY, SEM, true],
			[24, Y0, GSRail.SIGNALTYPE_ENTRY, E, true],
			[27, Y0, GSRail.SIGNALTYPE_EXIT, E, true],
			[27, Y0 - 1, GSRail.SIGNALTYPE_EXIT, E, true],
			[31, Y0, GSRail.SIGNALTYPE_NORMAL, E, true],
			[31, Y0 - 1, GSRail.SIGNALTYPE_NORMAL, E, true],
			[37, Y0, GSRail.SIGNALTYPE_PBS_ONEWAY, E, true],
			[43, Y0 + 2, GSRail.SIGNALTYPE_PBS_ONEWAY, SEM, false],
			[34, Y0 + 2, GSRail.SIGNALTYPE_ENTRY, SEM, false],
			[31, Y0 + 2, GSRail.SIGNALTYPE_COMBO, SEM, false],
			[31, Y0 + 3, GSRail.SIGNALTYPE_COMBO, SEM, false],
			[27, Y0 + 2, GSRail.SIGNALTYPE_EXIT, SEM, false],
			[27, Y0 + 3, GSRail.SIGNALTYPE_EXIT, SEM, false],
			[21, Y0 + 2, GSRail.SIGNALTYPE_NORMAL, SEM, false],
			[16, Y0 + 3, GSRail.SIGNALTYPE_PBS, SEM, false],
			[11, Y0 + 3, GSRail.SIGNALTYPE_PBS_ONEWAY, E, false],
			[14, Y0 + 2, GSRail.SIGNALTYPE_NORMAL, SEM, false],
			[6, Y0 + 2, GSRail.SIGNALTYPE_NORMAL, SEM, false],
		];
		local sig_ok = 0;
		local kinds = {};
		foreach (s in sigs) {
			if (this.lab_signal(X0 + s[0], s[1], D, X, s[2], s[3], s[4])) {
				sig_ok++;
				kinds[s[2] * 2 + s[3]] <- true;
			} else {
				report.metric("lab_bad_signal_" + s[0] + "_" + (s[1] - Y0), GSError.GetLastErrorString());
			}
		}
		/* The two-way passable path signal on the branch faces trains leaving S (north). */
		local branch_sig = this.lab_signal(X0 + 42, Y0 + 5, D, GSRail.RAILTRACK_NW_SE, GSRail.SIGNALTYPE_PBS, E, false);
		if (branch_sig) sig_ok++;
		report.check_eq("lab_ug_signals", sigs.len() + 1, sig_ok, "Lab: underground signals of the right type, variant and direction");
		report.check_eq("lab_ug_signal_kinds", 12, kinds.len(), "Lab: all six types in both variants underground");

		/* ---- East: portals up, surface scissors and terminus E ---- */
		ok("lab_portals_e", metro.build_portal(T(X0 + 46, Y0), GSMetro.DIAGDIR_NE, D) && metro.build_portal(T(X0 + 46, Y0 + 2), GSMetro.DIAGDIR_NE, D), "Lab: east portals (up eastbound, down westbound)");
		local sidE = null;
		if (GSRail.BuildRailStation(T(X0 + 53, Y0), X, 3, 3, GSStation.STATION_NEW)) sidE = GSStation.GetStationID(T(X0 + 53, Y0));
		ok("lab_e_station", sidE != null, "Lab: surface terminus E");
		foreach (y in [Y0, Y0 + 2]) ok("lab_e_rows_" + y, build.build_surface_x(X0 + 47, X0 + 52, y), "Lab: east surface rows");
		local spiece = function(dx, y, t) : (X0) { return GSRail.BuildRailTrack(GSMap.GetTileIndex(X0 + dx, y), t); };
		ok("lab_e_scissors",
			spiece(49, Y0, GSRail.RAILTRACK_NE_SE) && spiece(49, Y0 + 1, GSRail.RAILTRACK_NW_SW) &&
			spiece(50, Y0 + 1, GSRail.RAILTRACK_NE_SE) && spiece(50, Y0 + 2, GSRail.RAILTRACK_NW_SW) &&
			spiece(52, Y0, GSRail.RAILTRACK_SW_SE) && spiece(52, Y0 + 1, GSRail.RAILTRACK_NW_NE) &&
			spiece(51, Y0 + 1, GSRail.RAILTRACK_SW_SE) && spiece(51, Y0 + 2, GSRail.RAILTRACK_NW_NE), "Lab: surface scissors");
		ok("lab_e_surface_signals",
			GSRail.BuildSignal(T(X0 + 48, Y0), T(X0 + 47, Y0), GSRail.SIGNALTYPE_PBS_ONEWAY) &&
			GSRail.BuildSignal(T(X0 + 48, Y0 + 2), T(X0 + 49, Y0 + 2), GSRail.SIGNALTYPE_PBS), "Lab: east surface signals");

		foreach (s in [[sidW, "Lab Westpoint"], [sidM, "Lab Midway"], [sidC, "Lab Signal Hub"], [sidS, "Lab Southfield"], [sidE, "Lab Eastend"]]) {
			if (s[0] != null) GSBaseStation.SetName(s[0], s[1]);
		}
		lab.W <- sidW; lab.M <- sidM; lab.C <- sidC; lab.S <- sidS; lab.E <- sidE;
		lab.ok = sidW != null && sidM != null && sidC != null && sidS != null && sidE != null;
		report.metric("xy_lab_west", X0 + "," + Y0);
		report.metric("xy_lab_hub", (X0 + 28) + "," + Y0);
		report.metric("lab_depth", D);
		return lab;
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		this.report = MetroTest("metro_showcase", null, 510777);
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);
		this.build = MetroTest.MetroBuildLib();
		this.metro = MetroTest.MetroLib(this.build);
		this.world = MetroTest.MetroWorldLib(this.build);
		this.trains = MetroTest.MetroTrainLib();
		local build = this.build;
		local metro = this.metro;
		local world = this.world;

		try {
			local company = companies.ensure_company();
			local cargo = world.find_passenger_cargo();
			/* RAILTYPE_RAIL and RAILTYPE_ELECTRIC of the default rail type table. */
			local rail = 0;
			local elrail = 1;

			/* ---------- Geometry ----------
			 * In the middle of the map, so the camera can frame it from every side.
			 * The town the map generator always places lies elsewhere for this seed. */
			local XW = 100;           /* red west portals */
			local yA = 140;           /* red eastbound row */
			local central = null;
			local yB = yA + 2;        /* red westbound row */
			local xh = XW - 5;        /* red surface hook column */
			local XE = XW + 54;       /* red underground U-turn column */
			local S1 = XW + 8, S2 = XW + 26, S3 = XW + 44;
			local xc1 = S2, xc2 = S2 + 2; /* blue columns, under the red S2 platforms */
			local YN = yA - 20;       /* blue portals */
			local yh = YN - 5;        /* blue surface hook row */
			local yR = yA - 11;       /* blue station R (3 tiles from yR) */
			local yRamp = yA + 6;
			local yQ = yA + 11;       /* blue deep station Q */
			local yU = yA + 17;       /* blue underground U-turn row */

			local company_mode = GSCompanyMode(company);
			ok("two_railtypes", GSRail.IsRailTypeAvailable(rail) && GSRail.IsRailTypeAvailable(elrail), "Expected ordinary and electrified rail");
			report.metric("railtypes", GSRail.GetName(rail) + " / " + GSRail.GetName(elrail));
			/* Clear trees off the network area so surface rail and exits can be placed. */
			for (local y = yh - 3; y < yU + 4; y++) {
				for (local x = xh - 3; x < XE + 4; x++) {
					if (GSTile.HasTreeOnTile(T(x, y))) GSTile.DemolishTile(T(x, y));
				}
			}
			local top_left = T(xh - 3, yh - 3);
			/* Level in passes: one LevelTiles over the whole area runs out of terraform burst. */
			local base_h = 1;
			local flat = false;
			for (local pass = 0; pass < 12 && !flat; pass++) {
				flat = true;
				for (local cy = yh - 3; cy < yU + 4; cy += 8) {
					for (local cx = xh - 3; cx < XE + 4; cx += 8) {
						local need = false;
						for (local y = cy; y < cy + 8 && !need; y++) {
							for (local x = cx; x < cx + 8 && !need; x++) {
								if (GSTile.GetSlope(T(x, y)) != GSTile.SLOPE_FLAT || GSTile.GetMinHeight(T(x, y)) != base_h) need = true;
							}
						}
						if (!need) continue;
						flat = false;
						/* Raise the anchor corner to base_h first: LevelTiles levels to the first tile's north corner. */
						for (local i = 0; i < 4 && GSTile.GetCornerHeight(T(cx, cy), GSTile.CORNER_N) < base_h; i++) GSTile.RaiseTile(T(cx, cy), GSTile.SLOPE_N);
						for (local i = 0; i < 4 && GSTile.GetCornerHeight(T(cx, cy), GSTile.CORNER_N) > base_h; i++) GSTile.LowerTile(T(cx, cy), GSTile.SLOPE_N);
						GSTile.LevelTiles(T(cx, cy), T(cx + 8, cy + 8));
					}
				}
				GSController.Sleep(20);
			}
			report.metric("area_flat", flat ? 1 : 0);
			local h = GSTile.GetMinHeight(T(XW, yA));
			local D1 = h - 1, D2 = h - 2, D3 = h - 3;
			report.metric("surface_height", h);

			/* ---------- Red line (ordinary rail, D1) ---------- */
			GSRail.SetCurrentRailType(rail);
			ok("red_surface_rows", build.build_surface_x(xh + 1, XW - 1, yA) && build.build_surface_x(xh + 1, XW - 1, yB), "Red surface rows");
			ok("red_hook",
				GSRail.BuildRail(T(xh + 1, yB), T(xh, yB), T(xh, yA + 1)) &&
				GSRail.BuildRail(T(xh, yB), T(xh, yA + 1), T(xh, yA)) &&
				GSRail.BuildRail(T(xh, yA + 1), T(xh, yA), T(xh + 1, yA)), "Red west hook");
			local red_depot = T(xh + 2, yA - 1);
			ok("red_depot",
				GSRail.BuildRail(T(xh + 1, yA), T(xh + 2, yA), red_depot) &&
				GSRail.BuildRail(red_depot, T(xh + 2, yA), T(xh + 3, yA)) &&
				GSRail.BuildRailDepot(red_depot, T(xh + 2, yA)), "Red depot");
			ok("red_surface_signals",
				GSRail.BuildSignal(T(xh + 4, yA), T(xh + 3, yA), GSRail.SIGNALTYPE_PBS_ONEWAY) &&
				GSRail.BuildSignal(T(xh + 3, yB), T(xh + 4, yB), GSRail.SIGNALTYPE_PBS_ONEWAY), "Red surface signals");
			ok("red_portal_a", metro.build_portal(T(XW, yA), GSMetro.DIAGDIR_SW, D1), "Red portal A");
			ok("red_portal_b", metro.build_portal(T(XW, yB), GSMetro.DIAGDIR_SW, D1), "Red portal B");
			foreach (y in [yA, yB]) {
				for (local x = XW + 1; x < S3 + 3; x++) {
					local onstation = (x >= S1 && x < S1 + 3) || (x >= S2 && x < S2 + 3) || (x >= S3 && x < S3 + 3);
					if (!onstation) GSMetro.BuildUndergroundRail(T(x, y), GSRail.RAILTRACK_NE_SW, D1);
				}
			}
			/* The east end is a terminus: S3's platforms end in buffer stops, and a
			 * scissors over the gap row in front of them lets an eastbound train
			 * pick either platform and a departing one cross to the westbound row.
			 * Eastbound diagonal at XW+40..41 (yA -> yB), westbound at XW+43..42. */
			ok("red_scissors",
				GSMetro.BuildUndergroundRail(T(XW + 40, yA), GSRail.RAILTRACK_NE_SE, D1) &&
				GSMetro.BuildUndergroundRail(T(XW + 40, yA + 1), GSRail.RAILTRACK_NW_SW, D1) &&
				GSMetro.BuildUndergroundRail(T(XW + 41, yA + 1), GSRail.RAILTRACK_NE_SE, D1) &&
				GSMetro.BuildUndergroundRail(T(XW + 41, yB), GSRail.RAILTRACK_NW_SW, D1) &&
				GSMetro.BuildUndergroundRail(T(XW + 43, yA), GSRail.RAILTRACK_SW_SE, D1) &&
				GSMetro.BuildUndergroundRail(T(XW + 43, yA + 1), GSRail.RAILTRACK_NW_NE, D1) &&
				GSMetro.BuildUndergroundRail(T(XW + 42, yA + 1), GSRail.RAILTRACK_SW_SE, D1) &&
				GSMetro.BuildUndergroundRail(T(XW + 42, yB), GSRail.RAILTRACK_NW_NE, D1), "Red terminus scissors");

			local sid1 = this.station_x(S1, yA, 3, D1, GSStation.STATION_NEW);
			if (sid1 != null) this.station_x(S1, yB, 3, D1, sid1);
			local sid2 = this.station_x(S2, yA, 3, D1, GSStation.STATION_NEW);
			if (sid2 != null) this.station_x(S2, yB, 3, D1, sid2);
			local sid3 = this.station_x(S3, yA, 3, D1, GSStation.STATION_NEW);
			if (sid3 != null) this.station_x(S3, yB, 3, D1, sid3);
			ok("red_stations", sid1 != null && sid2 != null && sid3 != null, "Red stations S1..S3");

			local red_signals = 0;
			foreach (dx in [2, 6, 13, 22, 31, 38]) {
				foreach (y in [yA, yB]) {
					if (GSMetro.BuildUndergroundSignal(T(XW + dx, y), GSRail.RAILTRACK_NE_SW, D1)) red_signals++;
					/* The default faces westbound traffic; building again turns it for the eastbound row. */
					if (y == yA) GSMetro.BuildUndergroundSignal(T(XW + dx, y), GSRail.RAILTRACK_NE_SW, D1);
				}
			}
			report.check_eq("red_signals", 12, red_signals, "Red underground signals");

			/* ---------- Blue line (electrified, D2/D3) ---------- */
			GSRail.SetCurrentRailType(elrail);
			ok("blue_surface_cols", build.build_surface_y(xc1, yh + 1, YN - 1) && build.build_surface_y(xc2, yh + 1, YN - 1), "Blue surface columns");
			ok("blue_hook",
				GSRail.BuildRail(T(xc1, yh + 1), T(xc1, yh), T(xc1 + 1, yh)) &&
				GSRail.BuildRail(T(xc1, yh), T(xc1 + 1, yh), T(xc2, yh)) &&
				GSRail.BuildRail(T(xc1 + 1, yh), T(xc2, yh), T(xc2, yh + 1)), "Blue north hook");
			local blue_depot = T(xc1 - 1, yh + 2);
			ok("blue_depot",
				GSRail.BuildRail(T(xc1, yh + 1), T(xc1, yh + 2), blue_depot) &&
				GSRail.BuildRail(blue_depot, T(xc1, yh + 2), T(xc1, yh + 3)) &&
				GSRail.BuildRailDepot(blue_depot, T(xc1, yh + 2)), "Blue depot");
			ok("blue_surface_signals",
				GSRail.BuildSignal(T(xc1, yh + 4), T(xc1, yh + 3), GSRail.SIGNALTYPE_PBS_ONEWAY) &&
				GSRail.BuildSignal(T(xc2, yh + 3), T(xc2, yh + 4), GSRail.SIGNALTYPE_PBS_ONEWAY), "Blue surface signals");
			ok("blue_portal_1", metro.build_portal(T(xc1, YN), GSMetro.DIAGDIR_SE, D2), "Blue portal southbound");
			ok("blue_portal_2", metro.build_portal(T(xc2, YN), GSMetro.DIAGDIR_SE, D2), "Blue portal northbound");

			local blue_ok = true;
			foreach (x in [xc1, xc2]) {
				for (local y = YN + 1; y < yU; y++) {
					local onstation = (y >= yR && y < yR + 3) || (y >= yA - 1 && y < yA + 2) || (y >= yQ && y < yQ + 3);
					if (onstation || y == yRamp) continue;
					local d = y < yRamp ? D2 : D3;
					if (!GSMetro.BuildUndergroundRail(T(x, y), GSRail.RAILTRACK_NW_SE, d)) blue_ok = false;
				}
				if (!GSMetro.BuildRamp(T(x, yRamp), GSMetro.DIAGDIR_SE, D2, D3)) blue_ok = false;
			}
			ok("blue_track", blue_ok, "Blue underground columns and ramps");
			ok("blue_uturn",
				GSMetro.BuildUndergroundRail(T(xc1, yU), GSRail.RAILTRACK_NW_SW, D3) &&
				GSMetro.BuildUndergroundRail(T(xc1 + 1, yU), GSRail.RAILTRACK_NE_SW, D3) &&
				GSMetro.BuildUndergroundRail(T(xc2, yU), GSRail.RAILTRACK_NW_NE, D3), "Blue underground U-turn");

			local sidR = this.station_y(xc1, yR, 3, D2, GSStation.STATION_NEW);
			if (sidR != null) this.station_y(xc2, yR, 3, D2, sidR);
			/* Interchange: right below the red S2 platforms, joined to S2. */
			local p1 = this.station_y(xc1, yA - 1, 3, D2, sid2);
			local p2 = this.station_y(xc2, yA - 1, 3, D2, sid2);
			local sidQ = this.station_y(xc1, yQ, 3, D3, GSStation.STATION_NEW);
			if (sidQ != null) this.station_y(xc2, yQ, 3, D3, sidQ);
			ok("blue_stations", sidR != null && p1 != null && p2 != null && sidQ != null, "Blue stations R, interchange, Q");

			local blue_signals = 0;
			foreach (y in [YN + 2, yA - 14, yA - 7, yA - 3, yA + 3, yA + 9, yA + 15]) {
				local d = y < yRamp ? D2 : D3;
				foreach (x in [xc1, xc2]) {
					if (GSMetro.BuildUndergroundSignal(T(x, y), GSRail.RAILTRACK_NW_SE, d)) blue_signals++;
					/* The default faces southbound traffic; turn it for the northbound column. */
					if (x == xc2) GSMetro.BuildUndergroundSignal(T(x, y), GSRail.RAILTRACK_NW_SE, d);
				}
			}
			report.check_eq("blue_signals", 14, blue_signals, "Blue underground signals");

			foreach (s in [[sid1, "Westbury"], [sid2, "Central Interchange"], [sid3, "Eastgate"], [sidR, "Northam"], [sidQ, "Southwick Deep"]]) {
				if (s[0] != null) GSBaseStation.SetName(s[0], s[1]);
			}

			/* ---------- Pavilions, before the towns take the land ---------- */
			local pav = 0;
			foreach (s in [[S1, sid1], [S2, sid2], [S3, sid3]]) {
				if (this.pavilion(s[0] + 1, yA - 2, s[1])) pav++;
				if (this.pavilion(s[0] + 1, yB + 2, s[1])) pav++;
			}
			foreach (s in [[yR + 1, sidR], [yA, sid2], [yQ + 1, sidQ]]) {
				if (this.pavilion(xc1 - 2, s[0], s[1])) pav++;
				if (this.pavilion(xc2 + 2, s[0], s[1])) pav++;
			}
			report.metric("pavilions", pav);

			/* ---------- Signal lab, south of the city ---------- */
			local lab = this.BuildSignalLab(110, 195, rail);
			company_mode = null;

			/* ---------- Towns over the network ---------- */
			local towns = [];
			foreach (t in [[S1 + 1, yA + 7, "Westbury"], [S3 + 1, yA + 7, "Eastgate"], [S2 - 8, yA - 7, "Central City"],
					[xc2 + 8, yR, "Northam"], [xc1 - 7, yQ + 2, "Southwick"]]) {
				local id = world.create_town_near(T(t[0], t[1]), 6, t[2], 0);
				if (id != null) towns.append(id);
			}
			report.metric("towns", towns.len());
			foreach (id in towns) GSTown.ExpandTown(id, 260);
			foreach (id in towns) GSTown.ExpandTown(id, 200);

			/* ---------- Underpasses on the town roads ---------- */
			company_mode = GSCompanyMode(company);
			local up = 0;
			up += this.underpasses(sid1, [T(S1 + 1, yA), T(S1 + 1, yB)], 3, 3);
			up += this.underpasses(sid2, [T(S2 + 1, yA), T(S2 + 1, yB)], 3, 3);
			up += this.underpasses(sid3, [T(S3 + 1, yA), T(S3 + 1, yB)], 3, 3);
			up += this.underpasses(sidR, [T(xc1 + 1, yR + 1)], 3, 3);
			up += this.underpasses(sidQ, [T(xc1 + 1, yQ + 1)], 3, 3);
			report.metric("underpasses", up);

			/* ---------- Trains ---------- */
			local red_engine = this.best_engine(rail, false, elrail);
			local blue_engine = this.best_engine(elrail, true, rail);
			if (blue_engine == null) blue_engine = this.best_engine(elrail, false, rail);
			ok("engines", red_engine != null && blue_engine != null, "Red and blue engines");
			report.metric("engines", GSEngine.GetName(red_engine) + " / " + GSEngine.GetName(blue_engine));

			local flags = GSOrder.OF_NON_STOP_INTERMEDIATE;
			local fleet = [];
			for (local i = 0; i < 3; i++) {
				GSRail.SetCurrentRailType(rail);
				local v = this.make_train(red_depot, red_engine, cargo, 3);
				if (v == GSVehicle.VEHICLE_INVALID) continue;
				this.trains.append_underground_order(v, sid1, flags);
				this.trains.append_underground_order(v, sid2, flags);
				this.trains.append_underground_order(v, sid3, flags);
				fleet.append(v);
			}
			for (local i = 0; i < 2; i++) {
				GSRail.SetCurrentRailType(elrail);
				local v = this.make_train(blue_depot, blue_engine, cargo, 3);
				if (v == GSVehicle.VEHICLE_INVALID) continue;
				this.trains.append_underground_order(v, sidR, flags);
				this.trains.append_underground_order(v, sid2, flags);
				this.trains.append_underground_order(v, sidQ, flags);
				fleet.append(v);
			}
			ok("fleet", fleet.len() == 5, "Five trains");

			/* Signal lab: ten trains on three kinds of routes, every one through
			 * the hub and both ends of the network (surface and underground). */
			local lab_fleet = [];
			local lab_route = {};
			if (lab.ok) {
				GSRail.SetCurrentRailType(rail);
				local routes = [
					[lab.W, lab.C, lab.E, lab.C],
					[lab.W, lab.M, lab.C, lab.S, lab.C, lab.M],
					[lab.M, lab.E, lab.W, lab.S],
				];
				foreach (i, n in [4, 3, 3]) {
					for (local k = 0; k < n; k++) {
						local v = this.make_train(lab.depot, red_engine, cargo, 2);
						if (v == GSVehicle.VEHICLE_INVALID) continue;
						foreach (sid in routes[i]) this.trains.append_underground_order(v, sid, flags);
						lab_fleet.append(v);
						lab_route[v] <- routes[i];
					}
				}
			}
			ok("lab_fleet", lab_fleet.len() == 10, "Ten signal lab trains");

			foreach (v in fleet) {
				GSVehicle.StartStopVehicle(v);
				this.trains.wait_for_departure(v, 3000);
				GSController.Sleep(600);
			}
			foreach (v in lab_fleet) {
				GSVehicle.StartStopVehicle(v);
				this.trains.wait_for_departure(v, 3000);
				GSController.Sleep(400);
			}

			/* Let the network run and count underground station calls. Lab trains
			 * count completed orders (their stops are too short to be sampled)
			 * and their calls at the surface terminus E. */
			local calls = {};
			local was = {};
			foreach (v in fleet) { calls[v] <- 0; was[v] <- false; }
			foreach (v in lab_fleet) { calls[v] <- 0; was[v] <- GSOrder.ResolveOrderPosition(v, GSOrder.ORDER_CURRENT); }
			local lab_e_calls = 0;
			for (local t = 0; t < 60000; t += 10) {
				foreach (v in fleet) {
					local at = GSVehicle.GetState(v) == GSVehicle.VS_AT_STATION && GSMetro.IsVehicleUnderground(v);
					if (at && !was[v]) calls[v]++;
					was[v] = at;
				}
				foreach (v in lab_fleet) {
					local o = GSOrder.ResolveOrderPosition(v, GSOrder.ORDER_CURRENT);
					if (o != was[v]) {
						calls[v]++;
						if (was[v] >= 0 && was[v] < lab_route[v].len() && lab_route[v][was[v]] == lab.E) lab_e_calls++;
						was[v] = o;
					}
				}
				GSController.Sleep(10);
			}
			local lab_total = 0;
			foreach (v in lab_fleet) {
				report.check_true("lab_train_" + v + "_intact", GSVehicle.IsValidVehicle(v) && GSVehicle.GetState(v) != GSVehicle.VS_CRASHED, "Lab train should not crash");
				report.check_true("lab_train_" + v + "_serves", calls[v] >= 12, "Lab train should keep serving its stations (calls=" + calls[v] + ")");
				lab_total += calls[v];
			}
			report.metric("lab_total_calls", lab_total);
			report.metric("lab_e_calls", lab_e_calls);
			report.check_true("lab_e_calls", lab_e_calls >= 20, "Lab trains keep reaching the surface terminus E through the portals");
			local total = 0;
			foreach (v in fleet) {
				/* A train that is gone or crashed means two trains met in a block. */
				report.check_true("train_" + v + "_intact", GSVehicle.IsValidVehicle(v) && GSVehicle.GetState(v) != GSVehicle.VS_CRASHED, "Train should not crash");
				report.check_true("train_" + v + "_serves", calls[v] >= 20, "Train should keep serving underground stations (calls=" + calls[v] + ")");
				total += calls[v];
			}
			report.metric("total_calls", total);

			/* Coordinates for the screenshot scripts. */
			report.metric("xy_red_S1", S1 + "," + yA);
			report.metric("xy_interchange", S2 + "," + yA);
			report.metric("xy_red_S3", S3 + "," + yA);
			report.metric("xy_blue_R", xc1 + "," + yR);
			report.metric("xy_blue_ramp", xc1 + "," + yRamp);
			report.metric("xy_blue_Q", xc1 + "," + yQ);
			report.metric("xy_red_portals", XW + "," + yA);
			report.metric("xy_blue_portals", xc1 + "," + YN);
			report.metric("depths", D1 + "," + D2 + "," + D3);
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_showcase_final.sav"), "Showcase save should be written.");
		report.finish();
	}
}
