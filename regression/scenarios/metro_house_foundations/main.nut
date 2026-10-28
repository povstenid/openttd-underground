/*
 * metro_house_foundations
 *
 * A house takes the ground below it down to its foundation depth f (0 to 2,
 * by its population per tile against the largest one of the game's houses):
 * nothing underground may be built at a depth g <= f below the surface there,
 * and no house is built whose foundation would reach underground construction.
 *
 * Row y (flat, height >= 2), houses three tiles apart:
 *   x+1 tall (track)  x+4 tall (platform)  x+7 tall (ramp)  x+10 medium  x+13 low
 * Row y+4: tunnels under empty tiles, then a player places houses over them.
 * Row y+8: a blueprint of a short line, pasted under the tall house of row y.
 * Then a town grows over rows of tunnels at g = 2: none of its houses with a
 * foundation of 2 may stand over them.
 */
class MetroHouseFoundations extends GSController {
	report = null;

	function expect_error(id, ok, expected)
	{
		local err = GSError.GetLastError();
		this.report.check_true(id + "_refused", !ok, "Command should be refused.");
		this.report.check_eq(id + "_error", expected, err, "Unexpected error code (got " + GSError.GetLastErrorString() + ").");
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		this.report = MetroTest("metro_house_foundations", null, 510201);
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local world = MetroTest.MetroWorldLib(build);
		try {
			local area = build.find_buildable_rectangle(64, 24);
			report.check_true("build_area_found", area != null, "area");
			local company = companies.ensure_company();
			local x = build.tile_x(area), y = build.tile_y(area) + 2;
			local T = function(ax, ay) : (build) { return build.make_tile(ax, ay); };
			report.check_true("levelled", build.level_area_min_height(area, 20, 14, 2), "test rows flat at height 2 or more");

			/* Underground construction as the company; houses are placed as deity. */
			local rail = function(tile, g) : (company) {
				local cm = GSCompanyMode(company);
				return GSMetro.BuildUndergroundRail(tile, GSRail.RAILTRACK_NE_SW, GSTile.GetMinHeight(tile) - g);
			};

			/* Players may place houses. */
			GSGameSettings.SetValue("economy.place_houses", 1);
			report.metric("place_houses", GSGameSettings.GetValue("economy.place_houses"));

			/* House types by foundation depth. */
			local by_f = [[], [], []];
			for (local h = 0; h < GSMetro.GetHouseTypeCount(); h++) {
				local f = GSMetro.GetHouseTypeFoundationDepth(h);
				if (f >= 0) by_f[f].append(h);
			}
			report.metric("house_types_f0", by_f[0].len());
			report.metric("house_types_f1", by_f[1].len());
			report.metric("house_types_f2", by_f[2].len());
			report.check_true("house_types", by_f[0].len() > 0 && by_f[1].len() > 0 && by_f[2].len() > 0, "houses of every foundation depth");

			/* A town to own the houses, away from the test rows. */
			local town = world.create_town(T(x + 48, y + 10), "Fundament", 0);
			report.check_true("town", town != null, "town founded");

			local place = function(tile, types) {
				foreach (h in types) {
					if (GSMetro.PlaceHouse(tile, h)) return h;
				}
				return null;
			};

			/* 1. Under the tallest houses nothing at g = 1 and 2, all at g = 3. */
			local h1 = T(x + 1, y), h2 = T(x + 4, y), h3 = T(x + 7, y);
			report.check_true("tall_placed", place(h1, by_f[2]) != null && place(h2, by_f[2]) != null && place(h3, by_f[2]) != null, "three tall houses placed " + GSError.GetLastErrorString());
			report.check_eq("tall_depth", 2, GSMetro.GetFoundationDepth(h1), "the tall house has a foundation of 2");
			local z = GSTile.GetMinHeight(h1);
			report.metric("surface_height", z);
			this.expect_error("tall_track_g1", rail(h1, 1), GSMetro.ERR_UNDERGROUND_HOUSE_FOUNDATION);
			this.expect_error("tall_track_g2", rail(h1, 2), GSMetro.ERR_UNDERGROUND_HOUSE_FOUNDATION);
			report.check_true("tall_track_g3", rail(h1, 3), "track at g = 3 under the tall house " + GSError.GetLastErrorString());
			{
				local cm = GSCompanyMode(company);
				this.expect_error("tall_portal_g1", GSMetro.BuildPortal(h1, GSMetro.DIAGDIR_SW, z - 1), GSMetro.ERR_UNDERGROUND_HOUSE_FOUNDATION);
				this.expect_error("tall_station_g2", GSMetro.BuildUndergroundStation(h2, GSRail.RAILTRACK_NE_SW, z - 2, GSStation.STATION_NEW), GSMetro.ERR_UNDERGROUND_HOUSE_FOUNDATION);
				report.check_true("tall_station_g3", GSMetro.BuildUndergroundStation(h2, GSRail.RAILTRACK_NE_SW, z - 3, GSStation.STATION_NEW), "platform at g = 3 " + GSError.GetLastErrorString());
				this.expect_error("tall_ramp_g2_g3", GSMetro.BuildRamp(h3, GSMetro.DIAGDIR_SW, z - 2, z - 3), GSMetro.ERR_UNDERGROUND_HOUSE_FOUNDATION);
				report.check_true("tall_ramp_g3_g4", GSMetro.BuildRamp(h3, GSMetro.DIAGDIR_SW, z - 3, z - 4), "ramp from g = 3 " + GSError.GetLastErrorString());
				cm = null;
			}

			/* 2. A medium house takes one level, a low one none. */
			local hm = T(x + 10, y), hl = T(x + 13, y);
			report.check_true("medium_placed", place(hm, by_f[1]) != null, "medium house " + GSError.GetLastErrorString());
			report.check_eq("medium_depth", 1, GSMetro.GetFoundationDepth(hm), "medium foundation");
			this.expect_error("medium_track_g1", rail(hm, 1), GSMetro.ERR_UNDERGROUND_HOUSE_FOUNDATION);
			report.check_true("medium_track_g2", rail(hm, 2), "track at g = 2 under the medium house " + GSError.GetLastErrorString());
			report.check_true("low_placed", place(hl, by_f[0]) != null, "low house " + GSError.GetLastErrorString());
			report.check_eq("low_depth", 0, GSMetro.GetFoundationDepth(hl), "low foundation");
			report.check_true("low_track_g1", rail(hl, 1), "track at g = 1 under the low house " + GSError.GetLastErrorString());

			/* 3. A player does not place a house whose foundation reaches a tunnel. */
			local p1 = T(x + 1, y + 4), p2 = T(x + 5, y + 4);
			report.check_true("tunnels_for_houses", rail(p1, 2) && rail(p2, 1), "tunnels at g = 2 and g = 1 " + GSError.GetLastErrorString());
			local refused_tall = true;
			foreach (h in by_f[2]) {
				if (GSMetro.PlaceHouse(p1, h)) refused_tall = false;
				else if (GSError.GetLastError() != GSMetro.ERR_UNDERGROUND_IN_THE_WAY) report.metric("tall_over_tunnel_error_" + h, GSError.GetLastErrorString());
			}
			report.check_true("player_tall_over_g2_refused", refused_tall && GSMetro.GetFoundationDepth(p1) == 0, "no tall house over the tunnel at g = 2");
			report.check_true("player_medium_over_g2", place(p1, by_f[1]) != null, "a medium house over the tunnel at g = 2 " + GSError.GetLastErrorString());
			local refused_medium = true;
			foreach (h in by_f[1]) {
				if (GSMetro.PlaceHouse(p2, h)) refused_medium = false;
			}
			report.check_true("player_medium_over_g1_refused", refused_medium, "no medium house over the tunnel at g = 1");
			report.check_true("player_low_over_g1", place(p2, by_f[0]) != null, "a low house over the tunnel at g = 1 " + GSError.GetLastErrorString());

			/* 4. A blueprint is refused under the tall house like the commands are. */
			{
				local cm = GSCompanyMode(company);
				local by = y + 8;
				local d = GSTile.GetMinHeight(T(x + 1, by)) - 1;
				report.check_true("bp_line", GSMetro.BuildUndergroundRailTrack(T(x + 1, by), T(x + 3, by), GSRail.RAILTRACK_NE_SW, d), "line to copy " + GSError.GetLastErrorString());
				local bp = GSMetro.CopyBlueprint(T(x + 1, by), T(x + 3, by), d, d, false);
				report.check_true("bp_copied", bp != null, "blueprint copied");
				if (bp != null) {
					local n = GSMetro.GetBlueprintPasteChunkCount(bp, 0, false, 0);
					report.check_eq("bp_chunks", 1, n, "one part");
					/* Over the tall house h1 (x+1, y) at g = 1: refused. On the empty row y+2: built. */
					report.check_true("bp_under_house_refused", !GSMetro.PasteBlueprintChunk(bp, T(x, y), z - 1, 0, false, 0, 0), "paste under the tall house refused");
					report.check_true("bp_no_track_under_house", !GSMetro.IsUndergroundTile(h1, z - 1), "nothing built under the house");
					report.check_true("bp_elsewhere", GSMetro.PasteBlueprintChunk(bp, T(x + 1, y + 2), GSTile.GetMinHeight(T(x + 1, y + 2)) - 1, 0, false, 0, 0), "paste on empty land " + GSError.GetLastErrorString());
				}
				cm = null;
			}

			/* 5. A town grows over rows of tunnels at g = 2 (every other row). */
			local c = GSTown.GetLocation(town);
			local cx = build.tile_x(c), cy = build.tile_y(c);
			local tunnel_tiles = 0;
			for (local dy = -10; dy <= 10; dy += 2) {
				for (local dx = -10; dx <= 10; dx++) {
					if (rail(T(cx + dx, cy + dy), 2)) tunnel_tiles++;
				}
			}
			report.metric("town_tunnel_tiles", tunnel_tiles);
			report.check_true("town_tunnels", tunnel_tiles > 100, "tunnel rows under the town");
			GSTown.ExpandTown(town, 600);
			GSController.Sleep(10);
			report.metric("town_population", GSTown.GetPopulation(town));

			local violations = 0, tall_on_tunnel_rows = 0, tall_elsewhere = 0, houses_over_tunnels = 0;
			for (local dy = -12; dy <= 12; dy++) {
				for (local dx = -12; dx <= 12; dx++) {
					local t = T(cx + dx, cy + dy);
					local f = GSMetro.GetFoundationDepth(t);
					local tunnel = GSMetro.IsUndergroundTile(t, GSTile.GetMinHeight(t) - 2);
					if (GSTile.IsHouseTile(t) && tunnel) houses_over_tunnels++;
					if (f == 2) {
						if (tunnel) tall_on_tunnel_rows++; else tall_elsewhere++;
					}
					for (local g = 1; g <= f; g++) {
						if (GSMetro.IsUndergroundTile(t, GSTile.GetMinHeight(t) - g)) violations++;
					}
				}
			}
			report.metric("houses_over_tunnels", houses_over_tunnels);
			report.metric("tall_houses_elsewhere", tall_elsewhere);
			report.check_eq("town_no_foundation_on_tunnel", 0, violations, "no house foundation reaches a tunnel");
			report.check_eq("town_no_tall_house_on_tunnel", 0, tall_on_tunnel_rows, "no tall house over the tunnels at g = 2");
			report.check_true("town_builds_over_tunnels", houses_over_tunnels > 0, "lower houses are still built over the tunnels");
			report.check_true("town_builds_tall_houses", tall_elsewhere > 0, "the town does build tall houses off the tunnels");
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
