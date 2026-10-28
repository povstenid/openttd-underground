/*
 * metro_industry_foundations
 *
 * An industry takes the ground below it down to its foundation depth
 * (setting underground.industry_foundation_depth, 2 by default): nothing
 * underground may be built at a depth g <= 2 below its tiles, and no industry
 * is set up above underground construction this shallow.
 *
 * Area A: an industry; track under one of its tiles at g = 1 and 2 is refused,
 *         at g = 3 built; a platform at g = 2 is refused.
 * Area B: a block of tunnels at g = 2; an industry over it is refused.
 * Area C: the same block at g = 3; the industry is built over it.
 * Then the setting changes in the running game: what is built stays, the
 * rule follows the new depth for new construction only.
 */
class MetroIndustryFoundations extends GSController {
	report = null;

	function expect_error(id, ok, expected)
	{
		local err = GSError.GetLastError();
		this.report.check_true(id + "_refused", !ok, "Command should be refused.");
		this.report.check_eq(id + "_error", expected, err, "Unexpected error code (got " + GSError.GetLastErrorString() + ").");
	}

	/** Tunnel rows NE-SW at depth g below the surface, under the w x h block at (x, y). */
	function tunnel_block(build, company, x, y, w, h, g)
	{
		local cm = GSCompanyMode(company);
		local ok = true;
		for (local dy = 0; dy < h; dy++) {
			for (local dx = 0; dx < w; dx++) {
				local t = build.make_tile(x + dx, y + dy);
				if (!GSMetro.BuildUndergroundRail(t, GSRail.RAILTRACK_NE_SW, GSTile.GetMinHeight(t) - g)) ok = false;
			}
		}
		return ok;
	}

	/** Tiles of an industry, searched around one of them. */
	function industry_tiles(build, industry, near)
	{
		local tiles = [];
		local nx = build.tile_x(near), ny = build.tile_y(near);
		for (local dy = -6; dy <= 6; dy++) {
			for (local dx = -6; dx <= 6; dx++) {
				local t = build.make_tile(nx + dx, ny + dy);
				if (GSIndustry.GetIndustryID(t) == industry) tiles.append(t);
			}
		}
		return tiles;
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		this.report = MetroTest("metro_industry_foundations", null, 510211);
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local world = MetroTest.MetroWorldLib(build);
		try {
			local area = build.find_buildable_rectangle(60, 16);
			report.check_true("build_area_found", area != null, "area");
			local company = companies.ensure_company();
			local x = build.tile_x(area), y = build.tile_y(area) + 1;
			local T = function(ax, ay) : (build) { return build.make_tile(ax, ay); };
			{
				local cm = GSCompanyMode(company);
				/* Area A keeps a column off the west edge, where the land next to it may not be clear. */
				foreach (i, ax in [x + 2, x + 20, x + 40]) {
					local ok = false;
					/* An incline at the edge can stay after a raise: level again. */
					for (local attempt = 0; attempt < 3 && !ok; attempt++) ok = build.level_area_min_height(T(ax, y), 14, 12, 2);
					local sloped = 0, hmin = 99, hmax = -1, kinds = "";
					for (local dy = 0; dy < 12; dy++) for (local dx = 0; dx < 14; dx++) {
						local t = T(ax + dx, y + dy);
						if (GSTile.GetSlope(t) != GSTile.SLOPE_FLAT) { sloped++; if (kinds.len() < 60) kinds += dx + "," + dy + ":" + GSTile.GetSlope(t) + (GSTile.IsBuildable(t) ? "" : "!") + " "; }
						hmin = min(hmin, GSTile.GetMinHeight(t)); hmax = max(hmax, GSTile.GetMaxHeight(t));
					}
					report.metric("area_" + i + "_sloped", sloped);
					report.metric("area_" + i + "_heights", hmin + ".." + hmax);
					report.metric("area_" + i + "_slopes", kinds);
					report.check_true("levelled_" + i, ok, "area " + i + " flat at height 2 or more: " + GSError.GetLastErrorString());
				}
			}
			/* Several industries of one type stand near the same town. */
			GSGameSettings.SetValue("economy.multiple_industry_per_town", 1);

			report.check_eq("setting_default", 2, GSGameSettings.GetValue("underground.industry_foundation_depth"), "industry foundations are 2 levels deep by default");

			/* Area A, x .. x+13: an industry of the first type that builds on this land. */
			local built = null, type = null;
			foreach (it, _ in GSIndustryTypeList()) {
				if (!GSIndustryType.CanBuildIndustry(it) || GSIndustryType.IsBuiltOnWater(it)) continue;
				built = world.build_industry_in_area(it, T(x + 4, y + 4), 3, 3);
				if (built != null) { type = it; break; }
			}
			report.check_true("industry_a_built", built != null, "an industry in area A");
			if (built == null) throw "no industry could be built";
			report.metric("industry_type", type);
			report.metric("industry_type_name", GSIndustryType.GetName(type));

			local tiles = this.industry_tiles(build, built.industry, built.tile);
			report.metric("industry_tiles", tiles.len());
			report.check_true("industry_a_tiles", tiles.len() >= 2, "the industry has several tiles");
			local t1 = tiles[0], t2 = tiles[tiles.len() - 1];
			local h1 = GSTile.GetMinHeight(t1), h2 = GSTile.GetMinHeight(t2);
			report.check_eq("foundation_depth", 2, GSMetro.GetFoundationDepth(t1), "GSMetro.GetFoundationDepth of an industry tile");

			local rail = function(tile, g, track = GSRail.RAILTRACK_NE_SW) : (company) {
				local cm = GSCompanyMode(company);
				return GSMetro.BuildUndergroundRail(tile, track, GSTile.GetMinHeight(tile) - g);
			};

			/* 1. Under the industry nothing at g = 1 and 2, track at g = 3. */
			this.expect_error("track_g1", rail(t1, 1), GSMetro.ERR_UNDERGROUND_INDUSTRY_FOUNDATION);
			this.expect_error("track_g2", rail(t1, 2), GSMetro.ERR_UNDERGROUND_INDUSTRY_FOUNDATION);
			report.check_true("track_g3_built", rail(t1, 3), "track at g = 3 under the industry: " + GSError.GetLastErrorString());
			{
				local cm = GSCompanyMode(company);
				this.expect_error("station_g2", GSMetro.BuildUndergroundStation(t2, GSRail.RAILTRACK_NE_SW, h2 - 2, GSStation.STATION_NEW), GSMetro.ERR_UNDERGROUND_INDUSTRY_FOUNDATION);
			}

			/* 2. Area B, x+20 .. x+33: tunnels at g = 2 under a 6 x 6 block; no industry above them. */
			report.check_true("block_b_built", this.tunnel_block(build, company, x + 22, y + 2, 6, 6, 2), "tunnels at g = 2 in area B");
			local before = GSIndustryList().Count();
			local ok_b = GSIndustryType.BuildIndustry(type, T(x + 23, y + 3));
			this.expect_error("industry_over_g2", ok_b, GSMetro.ERR_UNDERGROUND_IN_THE_WAY);
			report.check_eq("industry_over_g2_count", before, GSIndustryList().Count(), "no industry was set up");

			/* 3. Area C, x+40 .. x+53: the same at g = 3; the industry is set up. */
			report.check_true("block_c_built", this.tunnel_block(build, company, x + 42, y + 2, 6, 6, 3), "tunnels at g = 3 in area C");
			local ok_c = GSIndustryType.BuildIndustry(type, T(x + 43, y + 3));
			report.check_true("industry_over_g3_built", ok_c, "an industry over tunnels at g = 3: " + GSError.GetLastErrorString());
			local c_industry = GSIndustry.GetIndustryID(T(x + 43, y + 3));
			local c_tiles = GSIndustry.IsValidIndustry(c_industry) ? this.industry_tiles(build, c_industry, T(x + 43, y + 3)) : [];
			local c_over = 0;
			foreach (t in c_tiles) if (GSMetro.IsUndergroundTile(t, GSTile.GetMinHeight(t) - 3)) c_over++;
			report.metric("industry_c_tiles_over_tunnel", c_over);
			report.check_true("industry_c_over_tunnel", c_over > 0, "the industry stands over the tunnels");

			/* 4. The setting changes in the running game. */
			report.check_true("set_depth_0", GSGameSettings.SetValue("underground.industry_foundation_depth", 0), "setting changed to 0");
			report.check_eq("foundation_depth_0", 0, GSMetro.GetFoundationDepth(t1), "no foundation with the setting 0");
			report.check_true("track_g1_depth_0", rail(t1, 1), "track at g = 1 under the industry with no foundation: " + GSError.GetLastErrorString());

			report.check_true("set_depth_1", GSGameSettings.SetValue("underground.industry_foundation_depth", 1), "setting changed to 1");
			report.check_true("track_g2_depth_1", rail(t1, 2), "track at g = 2 under a foundation of 1: " + GSError.GetLastErrorString());
			this.expect_error("track_g1_depth_1", rail(t2, 1), GSMetro.ERR_UNDERGROUND_INDUSTRY_FOUNDATION);

			report.check_true("set_depth_2", GSGameSettings.SetValue("underground.industry_foundation_depth", 2), "setting changed back to 2");
			report.check_true("kept_g1", GSMetro.IsUndergroundTile(t1, h1 - 1), "the track at g = 1 built meanwhile stays");
			report.check_true("kept_g2", GSMetro.IsUndergroundTile(t1, h1 - 2), "the track at g = 2 built meanwhile stays");
			report.check_true("existing_track_ok", rail(t1, 1), "laying the track that is there already: " + GSError.GetLastErrorString());
			this.expect_error("new_track_g1", rail(t1, 1, GSRail.RAILTRACK_NW_SE), GSMetro.ERR_UNDERGROUND_INDUSTRY_FOUNDATION);

			/* Settings other than the defaults go into the savegame (checked by loading it). */
			GSGameSettings.SetValue("underground.industry_foundation_depth", 1);
			report.check_true("settings_save_written", GSTest.SaveGame("metro_industry_foundations_settings.sav"), "save with changed settings");
			GSGameSettings.SetValue("underground.industry_foundation_depth", 2);

			/* The game runs on with industries over the tunnels. */
			GSController.Sleep(2000);
			report.check_true("industry_a_alive", GSIndustry.IsValidIndustry(built.industry), "industry A is still there");
			report.check_true("industry_c_alive", GSIndustry.IsValidIndustry(c_industry), "industry C is still there");
			report.check_true("tunnel_c_kept", GSMetro.IsUndergroundTile(T(x + 43, y + 3), GSTile.GetMinHeight(T(x + 43, y + 3)) - 3), "the tunnels under industry C stay");
			report.check_true("save_written", GSTest.SaveGame("metro_industry_foundations_final.sav"), "final save");
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
