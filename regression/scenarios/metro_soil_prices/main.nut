class MetroSoilPrices extends GSController {
	company = null;
	report = null;

	/* Raise a tile (in company mode) until it is flat at height h. */
	function RaiseTo(tile, h)
	{
		local cm = GSCompanyMode(this.company);
		for (local i = 0; i < 20 && GSTile.GetMinHeight(tile) < h; i++) {
			if (!GSTile.RaiseTile(tile, GSTile.SLOPE_ELEVATED)) break;
		}
		return GSTile.GetMinHeight(tile) == h && GSTile.GetSlope(tile) == GSTile.SLOPE_FLAT;
	}

	/* Run one underground command for the company, for real or as an estimate (test mode). */
	function Do(op, tile, level, test = false)
	{
		local cm = GSCompanyMode(this.company);
		local tm = test ? GSTestMode() : null;
		local acc = GSAccounting();
		local ok = false;
		switch (op) {
			case "track": ok = GSMetro.BuildUndergroundRail(tile, GSRail.RAILTRACK_NE_SW, level); break;
			case "untrack": ok = GSMetro.RemoveUndergroundRail(tile, GSRail.RAILTRACK_NE_SW, level); break;
			case "station": ok = GSMetro.BuildUndergroundStation(tile, GSRail.RAILTRACK_NE_SW, level, GSStation.STATION_NEW); break;
			case "unstation": ok = GSMetro.RemoveUndergroundStation(tile, level); break;
			case "signal": ok = GSMetro.BuildUndergroundSignal(tile, GSRail.RAILTRACK_NE_SW, level, GSRail.SIGNALTYPE_NORMAL, GSMetro.SIGNAL_VARIANT_ELECTRIC); break;
			case "ramp": ok = GSMetro.BuildRamp(tile, GSMetro.DIAGDIR_NE, level + 1, level); break;
			case "portal": ok = GSMetro.BuildPortal(tile, GSMetro.DIAGDIR_NE, level); break;
			case "unportal": ok = GSMetro.RemovePortal(tile); break;
			case "convert": ok = GSMetro.ConvertUndergroundRail(tile, tile, level); break;
		}
		local err = GSError.GetLastError();
		local cost = acc.GetCosts();
		acc = null;
		tm = null;
		return { ok = ok, cost = cost, err = err };
	}

	/* Change a game setting (as deity) and check it took the value. */
	function Set(name, value)
	{
		GSGameSettings.SetValue(name, value);
		return GSGameSettings.GetValue(name);
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		this.report = MetroTest("metro_soil_prices", null, 510120);
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();

		try {
			local area = build.find_buildable_rectangle(80, 50);
			report.check_true("build_area_found", area != null, "Expected a clear area.");
			this.company = companies.ensure_company();
			if (area == null || this.company == GSCompany.COMPANY_INVALID) throw "no area or company";
			local x = build.tile_x(area), y = build.tile_y(area);

			local rail = null;
			foreach (rt, _ in GSRailTypeList()) {
				if (GSRail.GetName(rt) == "Railway construction") rail = rt;
			}
			report.check_true("railtype", rail != null, "Plain railway available.");
			{
				local cm = GSCompanyMode(this.company);
				GSRail.SetCurrentRailType(rail);
			}
			local surface_track = GSRail.GetBuildCost(rail, GSRail.BT_TRACK);
			local surface_signal = GSRail.GetBuildCost(rail, GSRail.BT_SIGNAL);
			report.metric("surface_track", surface_track);

			/* Defaults of the underground settings. */
			report.check_eq("default_base", 10, GSGameSettings.GetValue("underground.base_cost_multiplier"), "Base multiplier x10.");
			report.check_eq("default_tunnel_level", -3, GSGameSettings.GetValue("underground.deepest_tunnel_level"), "Tunnels down to -3.");
			report.check_eq("default_station_level", -3, GSGameSettings.GetValue("underground.deepest_station_level"), "Stations down to -3.");
			report.check_eq("default_refund", 25, GSGameSettings.GetValue("underground.refund_percent"), "Refund 25 %.");
			report.check_eq("default_clay", 20, GSGameSettings.GetValue("underground.tunnel_cost_clay"), "Tunnel in clay x2.0.");
			report.check_eq("default_obsidian_station", 200, GSGameSettings.GetValue("underground.station_cost_obsidian"), "Station in obsidian x20.");
			report.check_eq("default_sand_tunnel", 40, GSGameSettings.GetValue("underground.tunnel_cost_sand"), "Tunnel in sand below water x4.");
			report.check_eq("default_sand_station", 50, GSGameSettings.GetValue("underground.station_cost_sand"), "Station in sand below water x5.");

			/* Sites: two columns at height 5, a flat tile at 2, a hill of 8, a mountain of 14. */
			local tA = build.make_tile(x + 5, y + 5);
			local tB = build.make_tile(x + 10, y + 5);
			local tF = build.make_tile(x + 5, y + 30);
			local tR = build.make_tile(x + 20, y + 5);
			local tH = build.make_tile(x + 30, y + 25);
			local tM = build.make_tile(x + 60, y + 25);
			report.check_true("raise_a", this.RaiseTo(tA, 5), "Column A at height 5.");
			report.check_true("raise_b", this.RaiseTo(tB, 5), "Column B at height 5.");
			report.check_true("raise_f", this.RaiseTo(tF, 2), "Flat tile at height 2.");
			report.check_true("raise_r", this.RaiseTo(tR, 2), "Tile for raising land over a tunnel at height 2.");
			report.check_true("raise_h", this.RaiseTo(tH, 8), "Hill of height 8.");
			report.check_true("raise_m", this.RaiseTo(tM, 14), "Mountain of height 14.");

			/* Soil by the depth below the surface. */
			report.check_eq("depth_hill", 7, GSMetro.GetDepthBelowSurface(tH, 1), "Level 1 under a hill of 8 is 7 below ground.");
			report.check_eq("soil_hill", GSMetro.SOIL_GRANITE_PINK, GSMetro.GetSoil(tH, 1), "Level 1 under a hill of 8: pink granite.");
			report.check_eq("depth_flat", 1, GSMetro.GetDepthBelowSurface(tF, 1), "Level 1 under flat land of 2 is 1 below ground.");
			report.check_eq("soil_flat", GSMetro.SOIL_EARTH, GSMetro.GetSoil(tF, 1), "Level 1 under flat land of 2: earth.");
			report.check_eq("soil_surface", GSMetro.SOIL_INVALID, GSMetro.GetSoil(tF, 2), "The surface level has no soil.");
			report.check_eq("soil_mountain_12", GSMetro.SOIL_OBSIDIAN, GSMetro.GetSoil(tM, 2), "Level 2 under a mountain of 14 (12 below): obsidian.");
			report.check_eq("soil_mountain_11", GSMetro.SOIL_OBSIDIAN, GSMetro.GetSoil(tM, 3), "11 below ground: obsidian.");
			report.check_eq("soil_mountain_10", GSMetro.SOIL_GRANITE_BLACK, GSMetro.GetSoil(tM, 4), "10 below ground: black granite.");
			report.check_eq("soil_a_3", GSMetro.SOIL_CLAY, GSMetro.GetSoil(tA, 2), "3 below ground: clay.");
			report.check_eq("soil_a_5", GSMetro.SOIL_MARBLE_LIGHT, GSMetro.GetSoil(tA, 0), "5 below ground: light marble.");

			/* Track at depth 1, 3 and 5: x1, x2 and x3 of the base price; estimate equals execution. */
			local e1 = this.Do("track", tA, 4, true);
			local t1 = this.Do("track", tA, 4);
			local t3 = this.Do("track", tA, 2);
			local e5 = this.Do("track", tA, 0, true);
			local t5 = this.Do("track", tA, 0);
			local base = t1.cost;
			report.metric("track_base", base);
			report.check_true("track_built", t1.ok && t3.ok && t5.ok, "Track at depth 1, 3 and 5 is built.");
			report.check_eq("track_base_price", surface_track * 10, base, "Track at depth 1 costs the surface track x10.");
			report.check_eq("track_depth_3", base * 2, t3.cost, "Track at depth 3 (clay) costs x2.");
			report.check_eq("track_depth_5", base * 3, t5.cost, "Track at depth 5 (light marble) costs x3.");
			report.check_eq("estimate_depth_1", t1.cost, e1.cost, "Estimate equals execution at depth 1.");
			report.check_eq("estimate_depth_5", t5.cost, e5.cost, "Estimate equals execution at depth 5.");

			/* Obsidian: x15. */
			local tm = this.Do("track", tM, 2);
			report.check_true("track_obsidian_built", tm.ok, "Track 12 below ground is built.");
			report.check_eq("track_obsidian", base * 15, tm.cost, "Track in obsidian costs x15.");

			/* Signal: the surface signal price x10, no soil. */
			local sg = this.Do("signal", tA, 0);
			report.check_true("signal_built", sg.ok, "Signal on the deep track is built.");
			report.check_eq("signal_price", surface_signal * 10, sg.cost, "An underground signal costs the surface signal x10, whatever the soil.");

			/* Stations: x1 at depth 1, x3.5 at depth 5 (light marble); estimate equals execution. */
			local s1 = this.Do("station", tB, 4);
			local se5 = this.Do("station", tB, 0, true);
			local s5 = this.Do("station", tB, 0);
			report.check_true("station_built", s1.ok && s5.ok, "Stations at depth 1 and 5 are built.");
			report.check_true("station_dearer_than_track", s1.cost > base, "A platform costs more than a track.");
			report.check_eq("station_depth_5", s1.cost * 7, s5.cost * 2, "A platform at depth 5 costs x3.5 of one at depth 1.");
			report.check_eq("station_estimate", s5.cost, se5.cost, "Estimate equals execution for a platform.");

			/* Ramp between levels 2 and 1 on column B (lower level 1 = depth 4, dark clay x2.5), x3. */
			local rp = this.Do("ramp", tB, 1);
			report.check_true("ramp_built", rp.ok, "Ramp is built.");
			report.check_eq("ramp_price", base * 15, rp.cost * 2, "A ramp costs track x3 x2.5 of its lower level.");

			/* Portal on flat land: track x2 in earth. */
			local pt = this.Do("portal", tF, 1);
			report.check_true("portal_built", pt.ok, "Portal is built.");
			report.check_eq("portal_price", base * 2 + surface_track, pt.cost, "A portal costs track x2 in earth plus the surface rail of its mouth.");

			/* A changed multiplier changes the price. */
			report.check_eq("set_base_20", 20, this.Set("underground.base_cost_multiplier", 20), "Base multiplier set to x20.");
			local e20 = this.Do("track", tA, 1, true);
			report.check_eq("base_20_price", base * 5, e20.cost, "At x20 a track at depth 4 (dark clay x2.5) costs x5.");
			this.Set("underground.base_cost_multiplier", 10);
			report.check_eq("set_dark_clay", 50, this.Set("underground.tunnel_cost_clay_dark", 50), "Dark clay set to x5.");
			local ecd = this.Do("track", tA, 1, true);
			report.check_eq("dark_clay_price", base * 5, ecd.cost, "Track in dark clay at x5 costs x5.");
			this.Set("underground.tunnel_cost_clay_dark", 25);

			/* Sand below water: a single canal tile at height 5 (water depth 1). Light sand,
			 * sand and dark sand at depths 1..3, then dark clay and marble as on land. */
			local tS = build.make_tile(x + 40, y + 5);
			local tSL = build.make_tile(x + 45, y + 5); /* not next to tS: raising it would tilt the canal site */
			report.check_true("raise_s", this.RaiseTo(tS, 5) && this.RaiseTo(tSL, 5), "Canal site at height 5.");
			{
				local cm = GSCompanyMode(this.company);
				report.check_true("canal_s", GSMarine.BuildCanal(tS), "Canal built " + GSError.GetLastErrorString());
			}
			report.check_eq("sand_water_depth", 1, GSMetro.GetWaterDepth(tS), "A single canal tile is 1 deep.");
			report.check_eq("sand_g1", GSMetro.SOIL_SAND_LIGHT, GSMetro.GetSoil(tS, 4), "1 below the canal: light sand.");
			report.check_eq("sand_g2", GSMetro.SOIL_SAND, GSMetro.GetSoil(tS, 3), "2 below the canal: sand.");
			report.check_eq("sand_g3", GSMetro.SOIL_SAND_DARK, GSMetro.GetSoil(tS, 2), "3 below the canal: dark sand.");
			report.check_eq("sand_g4", GSMetro.SOIL_CLAY_DARK, GSMetro.GetSoil(tS, 1), "4 below the canal: dark clay again.");
			report.check_eq("sand_g5", GSMetro.SOIL_MARBLE_LIGHT, GSMetro.GetSoil(tS, 0), "5 below the canal: light marble.");
			report.check_eq("sand_land_g2", GSMetro.SOIL_CLAY_LIGHT, GSMetro.GetSoil(tSL, 3), "On land at the same height: light clay.");
			report.check_eq("sand_land_g1", GSMetro.SOIL_EARTH, GSMetro.GetSoil(tSL, 4), "On land at the same height: earth.");
			local ts2 = this.Do("track", tS, 3);
			report.check_true("sand_track_built", ts2.ok, "Track in sand below the canal is built.");
			report.check_eq("sand_track_price", base * 4, ts2.cost, "Track in sand costs x4.");
			local ets3 = this.Do("track", tS, 2, true);
			report.check_eq("sand_dark_track_price", base * 4, ets3.cost, "Track in dark sand costs the same x4.");
			local ss3 = this.Do("station", tS, 2);
			report.check_true("sand_station_built", ss3.ok, "Platform in dark sand is built " + GSError.GetLastErrorString());
			report.check_eq("sand_station_price", s1.cost * 5, ss3.cost, "A platform in sand costs x5 of one in earth.");
			report.check_eq("set_sand", 60, this.Set("underground.tunnel_cost_sand", 60), "Sand set to x6.");
			local ets6 = this.Do("track", tS, 1, true);
			report.check_eq("sand_g4_unaffected", base * 5 / 2, ets6.cost, "The sand setting leaves the dark clay below it alone.");
			local rsand = this.Do("untrack", tS, 3);
			report.check_eq("sand_refund", -(ts2.cost * 25 / 100), rsand.cost, "The refund is a quarter of the price paid in sand.");
			local ets7 = this.Do("track", tS, 3, true);
			report.check_eq("sand_set_price", base * 6, ets7.cost, "Track in sand at x6 costs x6.");
			this.Set("underground.tunnel_cost_sand", 40);

			/* Refunds. The default: 25 % of the price with the soil. */
			local r25 = this.Do("untrack", tA, 2);
			report.check_true("remove_25", r25.ok, "Track removed.");
			report.check_eq("refund_25", -(base * 2 * 25 / 100), r25.cost, "25 % of a track in clay comes back.");
			this.Do("track", tA, 2);
			this.Set("underground.refund_percent", 0);
			local r0 = this.Do("untrack", tA, 2);
			report.check_true("remove_0", r0.ok, "Track removed.");
			report.check_eq("refund_0", 0, r0.cost, "At 0 % removal returns nothing.");
			local rebuilt = this.Do("track", tA, 2);
			this.Set("underground.refund_percent", 100);
			local r100 = this.Do("untrack", tA, 2);
			report.check_eq("refund_100", -rebuilt.cost, r100.cost, "At 100 % removal returns exactly the build price.");

			/* Raising the land over a tunnel makes its soil deeper, but removal returns no more than was paid. */
			local paid = this.Do("track", tR, 1);
			report.check_true("raise_over_tunnel", this.RaiseTo(tR, 6), "Land raised by 4 over the tunnel.");
			report.check_eq("soil_after_raise", GSMetro.SOIL_MARBLE_LIGHT, GSMetro.GetSoil(tR, 1), "The tunnel now lies 5 below ground.");
			local rraised = this.Do("untrack", tR, 1);
			report.check_true("remove_raised", rraised.ok, "Track under the raised land removed.");
			report.check_eq("refund_raised", -paid.cost, rraised.cost, "The refund is the price paid in earth, not the price in marble.");

			/* A portal pays for the surface rail of its mouth that its removal refunds. */
			local rportal = this.Do("unportal", tF, 1);
			report.check_true("portal_removed", rportal.ok, "Portal removed.");
			report.check_true("portal_refund_not_more", -rportal.cost <= pt.cost, "Removing a portal returns no more than it cost (" + (-rportal.cost) + " of " + pt.cost + ").");

			/* Conversion to a dearer rail type pays the underground difference, so removal never pays more than building. */
			local mono = null;
			foreach (rt, _ in GSRailTypeList()) {
				if (GSRail.GetName(rt) == "Monorail construction") mono = rt;
			}
			report.check_true("monorail", mono != null, "Monorail available.");
			if (mono != null) {
				local track_paid = this.Do("track", tA, 2);
				{
					local cm = GSCompanyMode(this.company);
					GSRail.SetCurrentRailType(mono);
				}
				local conv = this.Do("convert", tA, 2);
				{
					local cm = GSCompanyMode(this.company);
					GSRail.SetCurrentRailType(rail);
				}
				local new_price = 2 * 10 * GSRail.GetBuildCost(mono, GSRail.BT_TRACK);
				local old_price = track_paid.cost;
				local diff = new_price > old_price ? new_price - old_price : 0;
				report.check_true("converted", conv.ok, "Track in clay converted to monorail.");
				report.check_eq("convert_price", diff + new_price / 8, conv.cost, "Conversion costs the underground difference plus an eighth of the new track.");
				local rconv = this.Do("untrack", tA, 2);
				report.check_true("remove_converted", rconv.ok, "Converted track removed.");
				report.check_eq("refund_converted", -new_price, rconv.cost, "At 100 % the converted track returns its monorail price.");
				report.check_true("convert_no_profit", -rconv.cost <= old_price + conv.cost, "Build, convert and remove never gains money.");
			}
			/* Without the soil the refund is reckoned in earth, but never more than the build price. */
			this.Set("underground.refund_soil", 0);
			this.Set("underground.tunnel_cost_clay", 5);
			local cheap = this.Do("track", tA, 2);
			report.check_eq("cheap_clay_price", base / 2, cheap.cost, "Track in clay at x0.5 costs half.");
			local rcheap = this.Do("untrack", tA, 2);
			report.check_eq("refund_capped", -cheap.cost, rcheap.cost, "The refund is never more than the build price.");
			this.Set("underground.tunnel_cost_clay", 20);
			local deep = this.Do("track", tA, 2);
			local rdeep = this.Do("untrack", tA, 2);
			report.check_eq("refund_without_soil", -base, rdeep.cost, "Without the soil, 100 % of a track in clay returns the price in earth.");
			local rst = this.Do("unstation", tB, 0);
			report.check_true("station_removed", rst.ok, "Platform removed.");
			report.check_eq("station_refund_without_soil", -s1.cost, rst.cost, "Without the soil a platform at depth 5 returns the price at depth 1.");
			this.Set("underground.refund_soil", 1);
			this.Set("underground.refund_percent", 25);

			/* Depth limits: stations not deeper than their limit, tunnels by their own. */
			local deep_track = this.Do("track", tB, -4);
			report.check_true("tunnel_limit_default", !deep_track.ok && deep_track.err == GSMetro.ERR_UNDERGROUND_TOO_DEEP, "Level -4 is too deep for tunnels by default.");
			report.check_eq("set_tunnel_5", -5, this.Set("underground.deepest_tunnel_level", -5), "Tunnels down to -5.");
			report.check_eq("station_clamped", -3, this.Set("underground.deepest_station_level", -3), "Stations stay at -3.");
			local deep_station = this.Do("station", tB, -4);
			report.check_true("station_too_deep", !deep_station.ok, "A platform at -4 is refused.");
			report.check_eq("station_too_deep_error", GSMetro.ERR_UNDERGROUND_STATION_TOO_DEEP, deep_station.err, "The station depth error is reported.");
			deep_track = this.Do("track", tB, -4);
			report.check_true("tunnel_at_minus_4", deep_track.ok, "Track at -4 is built with tunnels down to -5.");
			local ok_station = this.Do("station", tB, -3);
			report.check_true("station_at_minus_3", ok_station.ok, "A platform at -3 is built.");
			report.check_eq("station_not_below_tunnels", -5, this.Set("underground.deepest_station_level", -7), "Stations are never allowed below tunnels.");
			report.check_eq("set_tunnel_2", -2, this.Set("underground.deepest_tunnel_level", -2), "Tunnels down to -2.");
			report.check_eq("station_follows_tunnel", -2, GSGameSettings.GetValue("underground.deepest_station_level"), "Raising the tunnel limit raises the station limit.");
			report.check_eq("tunnel_level_not_zero", -1, this.Set("underground.deepest_tunnel_level", 0), "Tunnels always go at least to -1.");
			/* What was built below the raised limit stays reachable and removable. */
			report.check_true("deep_level_reachable", GSMetro.IsUndergroundTile(tB, -4), "Track at -4 is still seen below the limit -1.");
			local old_deep = this.Do("untrack", tB, -4);
			report.check_true("remove_below_limit", old_deep.ok, "Track below a raised limit can still be removed.");
			local new_deep = this.Do("track", tB, -4);
			report.check_true("no_new_below_limit", !new_deep.ok, "Nothing new is built below the limit.");
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
