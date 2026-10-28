/*
 * metro_water_depth
 *
 * Water takes the ground below it down to its depth w: a river or canal tile
 * with land on one of its four sides 1, else 2 (land only on a diagonal does
 * not count); nothing underground is built at a depth g <= w below its
 * surface. Below the sea nothing at all is built (sea tunnels "forbidden").
 * Water does not come over a tunnel it would reach.
 *
 * Land rows (height >= 2):
 *   y    : C1 single canal (x+1)   L1 tunnel g1 (x+5)   L2 tunnel g2 (x+8)
 *   y+3..: 3x3 canal (x+1..x+3)   canal cross (x+7..x+9)   cross over a tunnel (x+13..x+15)
 * At the west border of the map, next to its sea: block A (8 x 8) lowered to
 * sea level and flooded for the depth of the sea, block B (4 x 4) lowered
 * with a tunnel under one tile, which must stay land.
 */
class MetroWaterDepth extends GSController {
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
		this.report = MetroTest("metro_water_depth", null, 510202);
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		try {
			local company = companies.ensure_company();
			local cm = GSCompanyMode(company);
			local T = function(ax, ay) : (build) { return build.make_tile(ax, ay); };
			local rail = function(tile, g) {
				return GSMetro.BuildUndergroundRail(tile, GSRail.RAILTRACK_NE_SW, GSTile.GetMinHeight(tile) - g);
			};

			/* --- Canals (rivers follow the same table). --- */
			local area = build.find_buildable_rectangle(20, 10);
			report.check_true("build_area_found", area != null, "area");
			report.check_true("levelled", build.level_area_min_height(area, 20, 10, 2), "canal rows flat at height 2 or more");
			local x = build.tile_x(area), y = build.tile_y(area) + 1;

			/* A single canal tile has land all round: w = 1. */
			local c1 = T(x + 1, y);
			report.check_true("canal_single", GSMarine.BuildCanal(c1), "canal " + GSError.GetLastErrorString());
			report.check_eq("canal_single_depth", 1, GSMetro.GetWaterDepth(c1), "edge depth");
			this.expect_error("canal_edge_g1", rail(c1, 1), GSMetro.ERR_UNDERGROUND_WATER_TOO_SHALLOW);
			report.check_true("canal_edge_g2", rail(c1, 2), "track at g = 2 under the canal edge " + GSError.GetLastErrorString());
			/* Below water the earth and clay are sand, darker with depth, then dark clay. */
			local h1 = GSTile.GetMinHeight(c1);
			report.check_eq("canal_soil_g1", GSMetro.SOIL_SAND_LIGHT, GSMetro.GetSoil(c1, h1 - 1), "light sand at g = 1 below a canal");
			report.check_eq("canal_soil_g2", GSMetro.SOIL_SAND, GSMetro.GetSoil(c1, h1 - 2), "sand at g = 2 below a canal");
			report.check_eq("canal_soil_g3", GSMetro.SOIL_SAND_DARK, GSMetro.GetSoil(c1, h1 - 3), "dark sand at g = 3 below a canal");
			report.check_eq("canal_soil_g4", GSMetro.SOIL_CLAY_DARK, GSMetro.GetSoil(c1, h1 - 4), "dark clay at g = 4 below a canal");

			/* No canal over a tunnel at g = 1; one over a tunnel at g = 2 has w = 1 and is built. */
			local l1 = T(x + 5, y), l2 = T(x + 8, y);
			report.check_true("tunnels_for_canals", rail(l1, 1) && rail(l2, 2), "tunnels " + GSError.GetLastErrorString());
			this.expect_error("canal_over_g1", GSMarine.BuildCanal(l1), GSMetro.ERR_UNDERGROUND_IN_THE_WAY);
			report.check_true("canal_over_g1_land", !GSTile.IsWaterTile(l1), "the tile stays land");
			report.check_true("canal_over_g2", GSMarine.BuildCanal(l2), "canal over the tunnel at g = 2 " + GSError.GetLastErrorString());
			/* A dragged canal across the tunnel at g = 1 is refused as a whole. */
			this.expect_error("canal_drag_over_g1", GSMetro.BuildCanalArea(T(x + 4, y), T(x + 6, y)), GSMetro.ERR_UNDERGROUND_IN_THE_WAY);
			report.check_true("canal_drag_nothing_built", !GSTile.IsWaterTile(T(x + 4, y)) && !GSTile.IsWaterTile(T(x + 6, y)), "no part of the drag is built");
			/* A canal drag over the whole map is checked quickly (it used to take quadratic time),
			 * and a hill in it is reported before the tunnels under it. */
			local drag_ok = GSMetro.BuildCanalArea(T(1, 1), T(GSMap.GetMapSizeX() - 2, GSMap.GetMapSizeY() - 2));
			report.check_true("canal_drag_map_refused", !drag_ok, "the whole map is no canal");
			report.check_true("canal_drag_map_other_error", GSError.GetLastError() != GSMetro.ERR_UNDERGROUND_IN_THE_WAY, "the first error of the tiles comes first (got " + GSError.GetLastErrorString() + ")");

			/* The middle of a 3 x 3 canal: w = 2. */
			for (local dy = 3; dy <= 5; dy++) for (local dx = 1; dx <= 3; dx++) GSMarine.BuildCanal(T(x + dx, y + dy));
			local c2 = T(x + 2, y + 4);
			report.check_eq("canal_centre_depth", 2, GSMetro.GetWaterDepth(c2), "centre depth");
			report.check_eq("canal_side_depth", 1, GSMetro.GetWaterDepth(T(x + 1, y + 4)), "side of the block");
			this.expect_error("canal_centre_g2", rail(c2, 2), GSMetro.ERR_UNDERGROUND_WATER_TOO_SHALLOW);
			report.check_true("canal_centre_g3", rail(c2, 3), "track at g = 3 under the canal centre " + GSError.GetLastErrorString());

			/* The middle of a cross: land only on its diagonals, still w = 2. */
			local c3 = T(x + 8, y + 4);
			foreach (d in [[0, 0], [-1, 0], [1, 0], [0, -1], [0, 1]]) GSMarine.BuildCanal(T(x + 8 + d[0], y + 4 + d[1]));
			report.check_eq("canal_cross_depth", 2, GSMetro.GetWaterDepth(c3), "diagonal land does not count");
			this.expect_error("canal_cross_g2", rail(c3, 2), GSMetro.ERR_UNDERGROUND_WATER_TOO_SHALLOW);

			/* Water next to new water gets deeper: the fourth arm of a cross over a tunnel at g = 2 is refused. */
			local c4 = T(x + 14, y + 4);
			report.check_true("cross_centre", GSMarine.BuildCanal(c4) && rail(c4, 2), "canal with a tunnel at g = 2 " + GSError.GetLastErrorString());
			report.check_true("cross_three_arms", GSMarine.BuildCanal(T(x + 13, y + 4)) && GSMarine.BuildCanal(T(x + 14, y + 3)) && GSMarine.BuildCanal(T(x + 14, y + 5)), "three arms " + GSError.GetLastErrorString());
			this.expect_error("cross_fourth_arm", GSMarine.BuildCanal(T(x + 15, y + 4)), GSMetro.ERR_UNDERGROUND_IN_THE_WAY);
			report.check_eq("cross_centre_depth", 1, GSMetro.GetWaterDepth(c4), "the centre keeps its land side");

			/* A generated river, when the map has one: its edge has w = 1. */
			local river = null;
			for (local i = 0; i < GSMap.GetMapSize() && river == null; i++) {
				if (GSTile.IsRiverTile(i) && GSMetro.GetWaterDepth(i) == 1 && !GSMetro.HasUnderground(i)) river = i;
			}
			report.metric("river_found", river != null ? 1 : 0);
			if (river != null) {
				this.expect_error("river_edge_g1", rail(river, 1), GSMetro.ERR_UNDERGROUND_WATER_TOO_SHALLOW);
				report.check_true("river_edge_g2", rail(river, 2), "track at g = 2 under the river edge " + GSError.GetLastErrorString());
			}

			/* --- The sea at the west border. --- */
			/* The first land tile of each row, west to east. */
			local first_land = [];
			for (local ty = 0; ty < GSMap.GetMapSizeY(); ty++) {
				local f = -1;
				for (local tx = 1; tx < 48 && f < 0; tx++) if (!GSTile.IsSeaTile(T(tx, ty))) f = tx;
				first_land.append(f);
			}
			report.check_true("border_sea", GSTile.IsSeaTile(T(1, GSMap.GetMapSizeY() / 2)), "sea along the west border");
			/* Land that can be lowered: buildable or a shore, no other water. */
			local lowerable = function(t) { return GSTile.IsBuildable(t) && !GSTile.IsRiverTile(t) && !GSTile.IsSeaTile(t); };
			/* 23 rows y0 - 1 .. y0 + 21 with land from the sea up to 10 tiles past the furthest shore. */
			local y0 = -1, xe = -1;
			for (local yy = 20; yy < GSMap.GetMapSizeY() - 30 && y0 < 0; yy++) {
				local fmax = -1, ok = true;
				for (local ty = yy - 1; ty <= yy + 21 && ok; ty++) {
					if (first_land[ty] < 2 || first_land[ty] > 36) ok = false;
					else fmax = max(fmax, first_land[ty]);
				}
				for (local ty = yy - 1; ty <= yy + 21 && ok; ty++) for (local tx = first_land[ty]; tx <= fmax + 10; tx++) if (!lowerable(T(tx, ty))) { ok = false; break; }
				if (ok) { y0 = yy; xe = fmax + 8; }
			}
			report.metric("sea_area_y", y0);
			report.metric("sea_area_east", xe);
			report.check_true("sea_area_found", y0 >= 0, "land next to the border sea");

			/* Lower a tile to sea level, its highest corners first. */
			local corners = [[GSTile.CORNER_N, GSTile.SLOPE_N], [GSTile.CORNER_W, GSTile.SLOPE_W], [GSTile.CORNER_S, GSTile.SLOPE_S], [GSTile.CORNER_E, GSTile.SLOPE_E]];
			local lower = function(t) : (corners) {
				for (local i = 0; i < 12 && GSTile.GetMaxHeight(t) > 0; i++) {
					local top = GSTile.GetMaxHeight(t), mask = 0;
					foreach (c in corners) if (GSTile.GetCornerHeight(t, c[0]) == top) mask = mask | c[1];
					if (!GSTile.LowerTile(t, mask)) return false;
				}
				return GSTile.GetMaxHeight(t) == 0;
			};

			/* Block A: rows y0 .. y0 + 7 lowered from the shore to x = xe. */
			local lowered = true;
			for (local ty = y0; ty < y0 + 8; ty++) for (local tx = first_land[ty]; tx <= xe; tx++) lowered = lower(T(tx, ty)) && lowered;
			report.check_true("lower_a", lowered, "block A to sea level " + GSError.GetLastErrorString());

			/* Block B: rows y1 .. y1 + 3, its tile keep lowered last and tunnelled at once, before the sea gets there. */
			local y1 = y0 + 12, xb = -1;
			for (local ty = y1; ty < y1 + 4; ty++) xb = max(xb, first_land[ty] + 3);
			local keep = T(xb - 1, y1 + 1);
			lowered = true;
			for (local ty = y1; ty < y1 + 4; ty++) for (local tx = first_land[ty]; tx <= xb; tx++) if (T(tx, ty) != keep) lowered = lower(T(tx, ty)) && lowered;
			lowered = lower(keep) && lowered;
			report.check_true("tunnel_before_flood", GSMetro.BuildUndergroundRail(keep, GSRail.RAILTRACK_NE_SW, -1), "tunnel under land at sea level " + GSError.GetLastErrorString());
			report.check_true("lower_b", lowered, "block B to sea level " + GSError.GetLastErrorString());

			local flooded = false;
			for (local t = 0; t < 16000 && !flooded; t += 100) {
				GSController.Sleep(100);
				flooded = true;
				for (local ty = y0; ty < y0 + 8 && flooded; ty++) for (local tx = first_land[ty]; tx <= xe; tx++) if (!GSTile.IsSeaTile(T(tx, ty))) { flooded = false; break; }
			}
			report.check_true("block_a_flooded", flooded, "block A became sea");
			/* Wait a little more so block B floods all round its tunnel. */
			GSController.Sleep(3000);

			local ym = y0 + 4;
			report.check_eq("sea_depth_1", 1, GSMetro.GetWaterDepth(T(xe, ym)), "first tile from the shore");
			report.check_eq("sea_depth_2", 2, GSMetro.GetWaterDepth(T(xe - 1, ym)), "second tile from the shore");
			report.check_eq("sea_depth_3", 3, GSMetro.GetWaterDepth(T(xe - 2, ym)), "third tile from the shore");
			report.check_eq("sea_depth_4", 3, GSMetro.GetWaterDepth(T(xe - 4, ym)), "further out");
			report.check_eq("shore_is_land", 0, GSMetro.GetWaterDepth(T(xe + 1, ym)), "the shore is land");
			/* Sand below the sea too, from level -1 (the sea is at level 0); not below the shore. */
			report.check_eq("sea_soil_g1", GSMetro.SOIL_SAND_LIGHT, GSMetro.GetSoil(T(xe, ym), -1), "light sand at level -1 below the sea");
			report.check_eq("sea_soil_g2", GSMetro.SOIL_SAND, GSMetro.GetSoil(T(xe - 2, ym), -2), "sand at level -2 below the open sea");
			report.check_eq("sea_soil_g3", GSMetro.SOIL_SAND_DARK, GSMetro.GetSoil(T(xe - 1, ym), -3), "dark sand at level -3 below the sea");
			report.check_eq("sea_soil_g4", GSMetro.SOIL_CLAY_DARK, GSMetro.GetSoil(T(xe - 2, ym), -4), "dark clay at level -4 below the open sea");
			report.check_true("shore_soil", GSMetro.GetSoil(T(xe + 1, ym), -1) == GSMetro.SOIL_EARTH || GSMetro.GetSoil(T(xe + 1, ym), -1) == GSMetro.SOIL_CLAY_LIGHT, "no sand below the shore (got " + GSMetro.GetSoil(T(xe + 1, ym), -1) + ")");
			/* The normal rule (the default): below the sea, as below rivers, only the water itself is closed. */
			report.check_eq("sea_by_depth_default", 1, GSGameSettings.GetValue("underground.sea_tunnels"), "sea tunnels below the water by default");
			this.expect_error("sea_depth1_g1", rail(T(xe, ym), 1), GSMetro.ERR_UNDERGROUND_WATER_TOO_SHALLOW);
			report.check_true("sea_depth1_g2", rail(T(xe, ym), 2), "track at g = 2 under the first sea tile " + GSError.GetLastErrorString());
			this.expect_error("sea_depth2_g2", rail(T(xe - 1, ym), 2), GSMetro.ERR_UNDERGROUND_WATER_TOO_SHALLOW);
			report.check_true("sea_depth2_g3", rail(T(xe - 1, ym), 3), "track at g = 3 under the second sea tile " + GSError.GetLastErrorString());
			this.expect_error("sea_depth3_g3", rail(T(xe - 2, ym), 3), GSMetro.ERR_UNDERGROUND_WATER_TOO_SHALLOW);
			/* The open sea is 3 deep: at g = 4 (level -4, tunnels allowed that deep here) it is open. */
			cm = null; /* settings are changed as deity */
			GSGameSettings.SetValue("underground.deepest_tunnel_level", -5);
			cm = GSCompanyMode(company);
			report.check_eq("deeper_tunnels", -5, GSGameSettings.GetValue("underground.deepest_tunnel_level"), "tunnels down to -5");
			report.check_true("sea_depth3_g4", rail(T(xe - 2, ym), 4), "track at g = 4 under the open sea " + GSError.GetLastErrorString());
			report.check_true("sea_far_g4", rail(T(xe - 4, ym), 4), "track at g = 4 further out " + GSError.GetLastErrorString());

			/* The stricter setting keeps everything out from under the sea, at any depth. */
			cm = null;
			GSGameSettings.SetValue("underground.sea_tunnels", 0);
			cm = GSCompanyMode(company);
			report.check_eq("sea_forbidden_setting", 0, GSGameSettings.GetValue("underground.sea_tunnels"), "setting changed");
			this.expect_error("sea_shore_g1", rail(T(xe, ym), 1), GSMetro.ERR_UNDERGROUND_UNDER_SEA);
			this.expect_error("sea_shore_g3", rail(T(xe - 1, ym + 1), 3), GSMetro.ERR_UNDERGROUND_UNDER_SEA);
			this.expect_error("sea_open_g5", rail(T(xe - 3, ym), 5), GSMetro.ERR_UNDERGROUND_UNDER_SEA);
			this.expect_error("sea_station", GSMetro.BuildUndergroundStation(T(xe - 1, ym + 2), GSRail.RAILTRACK_NE_SW, -2, GSStation.STATION_NEW), GSMetro.ERR_UNDERGROUND_UNDER_SEA);
			cm = null;
			GSGameSettings.SetValue("underground.sea_tunnels", 1);
			GSGameSettings.SetValue("underground.deepest_tunnel_level", -3);
			cm = GSCompanyMode(company);

			/* The sea did not come over the tunnel; the tiles around it are sea. */
			report.check_true("flood_kept_off_tunnel", !GSTile.IsSeaTile(keep) && !GSTile.IsWaterTile(keep), "the land over the tunnel stays land");
			report.check_true("flood_around_tunnel", GSTile.IsSeaTile(T(xb - 2, y1 + 1)) && GSTile.IsSeaTile(T(xb - 1, y1 + 2)) && GSTile.IsSeaTile(T(xb, y1 + 1)), "the sea around it");
			report.check_true("tunnel_kept", GSMetro.IsUndergroundTile(keep, -1), "the tunnel is still there");
			cm = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
