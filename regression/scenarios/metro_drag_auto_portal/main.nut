/*
 * Dragging underground rail (GSMetro.BuildUndergroundRailTrack) at level h out of
 * a one-level hill: the tiles under the hill get underground track, the incline
 * at the foot of the hill (the tunnel mouth) gets a portal facing into the hill
 * and the open ground beyond gets ordinary surface rail.
 *
 * Layout (X axis runs from x0 - 3 to x0 + 7; the hill is x0 + 1 .. x0 + 3):
 *
 *   rows yA, yB : open open open [SLOPE_SW] hill hill hill [SLOPE_NE] open open open
 *   row  yW     : same one-row hill, but the mouth gets a third raised corner
 *   rows yZ..+1 : same hill, crossed by a zig-zag (non-straight) drag
 */
class MetroDragAutoPortal extends GSController {
	report = null;
	build = null;
	h = 0;

	function expect_error(id, ok, expected)
	{
		local err = GSError.GetLastError();
		this.report.check_true(id + "_refused", !ok, "Command should be refused.");
		this.report.check_eq(id + "_error", expected, err, "Unexpected error code (got " + GSError.GetLastErrorString() + ").");
	}

	function t(x, y)
	{
		return this.build.make_tile(x, y);
	}

	/** Underground X track at level h on x1 .. x2 of row y. */
	function check_inside(id, x1, x2, y)
	{
		local ok = true;
		for (local x = x1; x <= x2; x++) {
			local tile = this.t(x, y);
			ok = ok && GSMetro.HasUndergroundRail(tile, this.h) &&
					GSMetro.GetUndergroundTileKind(tile, this.h) == GSMetro.TK_TRACK &&
					GSMetro.GetUndergroundTrackBits(tile, this.h) == GSRail.RAILTRACK_NE_SW &&
					!GSRail.IsRailTile(tile);
		}
		this.report.check_true(id, ok, "Tiles under the hill should carry underground X track at level h and no surface rail.");
	}

	/** Portal at level h with surface rail along the incline. */
	function check_mouth(id, tile)
	{
		this.report.check_true(id + "_portal", GSMetro.IsPortalTile(tile), "The incline at the tunnel mouth should get a portal.");
		this.report.check_eq(id + "_kind", GSMetro.TK_PORTAL, GSMetro.GetUndergroundTileKind(tile, this.h), "The portal slice should sit at level h.");
		this.report.check_true(id + "_surface_rail", GSRail.IsRailTile(tile) && GSRail.GetRailTracks(tile) == GSRail.RAILTRACK_NE_SW, "The portal should carry surface X rail.");
	}

	/** Ordinary surface X rail and nothing underground on x1 .. x2 of row y. */
	function check_open(id, x1, x2, y)
	{
		local ok = true;
		for (local x = x1; x <= x2; x++) {
			local tile = this.t(x, y);
			ok = ok && GSRail.IsRailTile(tile) && GSRail.GetRailTracks(tile) == GSRail.RAILTRACK_NE_SW &&
					!GSMetro.HasUnderground(tile) && GSMetro.GetUndergroundTileKind(tile, this.h) == GSMetro.TK_NONE;
		}
		this.report.check_true(id, ok, "Open ground should get ordinary surface rail and nothing underground.");
	}

	/** Nothing at all was built on the listed tiles. */
	function check_untouched(id, tiles)
	{
		local ok = true;
		foreach (tile in tiles) {
			ok = ok && !GSMetro.HasUnderground(tile) && !GSMetro.IsPortalTile(tile) && !GSRail.IsRailTile(tile);
		}
		this.report.check_true(id, ok, "A refused drag must not build anything.");
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		this.report = MetroTest("metro_drag_auto_portal", null, 510072);
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);
		this.build = MetroTest.MetroBuildLib();
		local build = this.build;

		try {
			local area = build.find_buildable_rectangle(20, 17);
			report.check_true("build_area_found", area != null, "Expected a clear area for the drag regression.");

			local company = companies.ensure_company();
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local company_mode = GSCompanyMode(company);

				report.check_true("area_levelled", build.level_area_min_height(area, 20, 17, 1), "Test area should be flat at height >= 1.");

				local ax = build.tile_x(area);
				local ay = build.tile_y(area);
				local x0 = ax + 5;
				local yA = ay + 2;
				local yB = ay + 3;
				local yW = ay + 7;
				local yZ = ay + 11;

				local hA = build.raise_block(x0 + 1, yA, x0 + 3, yB);
				local hW = build.raise_block(x0 + 1, yW, x0 + 3, yW);
				local hZ = build.raise_block(x0 + 1, yZ, x0 + 3, yZ + 1);
				report.check_true("hills_built", hA != null && hW != null && hZ != null && hA == hW && hA == hZ, "Three one-level hills should be raised from the same ground level.");

				if (hA != null && hA == hW && hA == hZ) {
					this.h = hA;
					local h = hA;
					report.metric("h", h);

					/* --- Line A: from inside the hill out across the north-east incline, then out across the south-west one. --- */
					report.check_true("drag_a_out_ne", GSMetro.BuildUndergroundRailTrack(this.t(x0 + 2, yA), this.t(x0 - 3, yA), GSRail.RAILTRACK_NE_SW, h), "Drag from inside the hill to open ground should succeed (err=" + GSError.GetLastErrorString() + ").");
					this.check_inside("a_ne_inside", x0 + 1, x0 + 2, yA);
					this.check_mouth("a_ne_mouth", this.t(x0, yA));
					this.check_open("a_ne_open", x0 - 3, x0 - 1, yA);
					report.check_true("a_ne_stops_at_end", !GSRail.IsRailTile(this.t(x0 - 4, yA)), "The drag must stop at its end tile.");
					report.check_true("a_ne_hill_rest_free", !GSMetro.HasUnderground(this.t(x0 + 3, yA)), "Tiles beyond the start must stay untouched.");

					report.check_true("drag_a_out_sw", GSMetro.BuildUndergroundRailTrack(this.t(x0 + 2, yA), this.t(x0 + 7, yA), GSRail.RAILTRACK_NE_SW, h), "Drag from existing track inside the hill to the other side should succeed (err=" + GSError.GetLastErrorString() + ").");
					this.check_inside("a_sw_inside", x0 + 1, x0 + 3, yA);
					this.check_mouth("a_sw_mouth", this.t(x0 + 4, yA));
					this.check_open("a_sw_open", x0 + 5, x0 + 7, yA);

					report.check_true("a_connection_ne_sw", GSMetro.HasPortalConnection(this.t(x0, yA), this.t(x0 + 4, yA), h), "The two mouths of line A should be connected at level h.");
					report.check_true("a_connection_sw_ne", GSMetro.HasPortalConnection(this.t(x0 + 4, yA), this.t(x0, yA), h), "The connection should exist in both directions.");

					/* --- Line B (parallel): from open ground into the hill, from both sides. --- */
					report.check_true("drag_b_in_from_ne", GSMetro.BuildUndergroundRailTrack(this.t(x0 - 3, yB), this.t(x0 + 2, yB), GSRail.RAILTRACK_NE_SW, h), "Drag from open ground into the hill should succeed (err=" + GSError.GetLastErrorString() + ").");
					this.check_open("b_ne_open", x0 - 3, x0 - 1, yB);
					this.check_mouth("b_ne_mouth", this.t(x0, yB));
					this.check_inside("b_ne_inside", x0 + 1, x0 + 2, yB);
					report.check_true("b_no_connection_yet", !GSMetro.HasPortalConnection(this.t(x0, yB), this.t(x0 + 4, yB), h), "Line B has only one mouth so far.");

					report.check_true("drag_b_in_from_sw", GSMetro.BuildUndergroundRailTrack(this.t(x0 + 7, yB), this.t(x0 + 2, yB), GSRail.RAILTRACK_NE_SW, h), "Drag from the other side into existing track should succeed (err=" + GSError.GetLastErrorString() + ").");
					this.check_open("b_sw_open", x0 + 5, x0 + 7, yB);
					this.check_mouth("b_sw_mouth", this.t(x0 + 4, yB));
					this.check_inside("b_sw_inside", x0 + 1, x0 + 3, yB);

					report.check_true("b_connection_ne_sw", GSMetro.HasPortalConnection(this.t(x0, yB), this.t(x0 + 4, yB), h), "The two mouths of line B should be connected at level h.");
					report.check_true("b_connection_sw_ne", GSMetro.HasPortalConnection(this.t(x0 + 4, yB), this.t(x0, yB), h), "The connection should exist in both directions.");
					report.check_true("a_b_not_connected", !GSMetro.HasPortalConnection(this.t(x0, yA), this.t(x0, yB), h) && !GSMetro.HasPortalConnection(this.t(x0, yA), this.t(x0 + 4, yB), h), "Parallel straight lines must not be connected.");

					/* --- Row W: the mouth tile is not an incline (three corners raised). ---
					 * It cannot be the portal; the last tile under the hill becomes a portal
					 * diving into the ground instead, and the mouth carries surface track. */
					local mouth_w = this.t(x0, yW);
					report.check_true("w_corner_raised", GSTile.RaiseTile(mouth_w, GSTile.SLOPE_N), "Raising the north corner of the mouth tile should succeed.");
					report.check_eq("w_mouth_slope", GSTile.SLOPE_NWS, GSTile.GetSlope(mouth_w), "The mouth tile should have three corners raised.");
					report.check_eq("w_mouth_lowest", h, GSTile.GetMinHeight(mouth_w), "The mouth tile should still be open at level h.");
					report.check_true("w_drag_out", GSMetro.BuildUndergroundRailTrack(this.t(x0 + 2, yW), this.t(x0 - 3, yW), GSRail.RAILTRACK_NE_SW, h), "Drag out past a mouth that is no incline should succeed (err=" + GSError.GetLastErrorString() + ").");
					this.check_inside("w_inside", x0 + 2, x0 + 2, yW);
					report.check_true("w_dive_portal", GSMetro.IsPortalTile(this.t(x0 + 1, yW)) && GSMetro.GetUndergroundTileKind(this.t(x0 + 1, yW), h) == GSMetro.TK_PORTAL, "The last tile under the hill should be a portal diving into the ground.");
					report.check_true("w_mouth_rail", GSRail.IsRailTile(mouth_w) && !GSMetro.IsPortalTile(mouth_w), "The mouth should carry surface track.");
					this.check_open("w_open", x0 - 3, x0 - 1, yW);

					/* --- Rows Z: a zig-zag (non-straight) drag across the mouth. ---
					 * The line runs (x0+2,yZ) (x0+1,yZ) (x0+1,yZ+1) [mouth (x0,yZ+1)] (x0,yZ+2) (x0-1,yZ+2) (x0-1,yZ+3). */
					local z_tiles = [this.t(x0 + 2, yZ), this.t(x0 + 1, yZ), this.t(x0 + 1, yZ + 1), this.t(x0, yZ + 1), this.t(x0, yZ + 2), this.t(x0 - 1, yZ + 2), this.t(x0 - 1, yZ + 3)];
					report.check_eq("z_mouth_slope", GSTile.SLOPE_SW, GSTile.GetSlope(this.t(x0, yZ + 1)), "The zig-zag mouth should be an incline into the hill.");
					this.expect_error("z_drag_out", GSMetro.BuildUndergroundRailTrack(this.t(x0 + 2, yZ), this.t(x0 - 1, yZ + 3), GSRail.RAILTRACK_NW_NE, h), GSMetro.ERR_UNDERGROUND_PORTAL_NEEDS_STRAIGHT_TRACK);
					this.check_untouched("z_drag_out_nothing_built", z_tiles);
					/* The same line the other way round crosses the sloped tile (x0, yZ+2) before the
					 * mouth, where vanilla refuses the half-tile surface track; the mouth error must
					 * still win (the drag checks mouths before building anything). */
					this.expect_error("z_drag_in", GSMetro.BuildUndergroundRailTrack(this.t(x0 - 1, yZ + 3), this.t(x0 + 2, yZ), GSRail.RAILTRACK_NW_NE, h), GSMetro.ERR_UNDERGROUND_PORTAL_NEEDS_STRAIGHT_TRACK);
					this.check_untouched("z_drag_in_nothing_built", z_tiles);

					/* Into the hill from open ground with right half-tile tracks: the line runs
					 * (x0-2,yZ-2) (x0-2,yZ-1) (x0-1,yZ-1) (x0-1,yZ) (x0,yZ) [mouth (x0,yZ+1)] (x0+1,yZ+1). */
					local zr_tiles = [this.t(x0 - 2, yZ - 2), this.t(x0 - 2, yZ - 1), this.t(x0 - 1, yZ - 1), this.t(x0 - 1, yZ), this.t(x0, yZ), this.t(x0, yZ + 1), this.t(x0 + 1, yZ + 1)];
					this.expect_error("z_drag_in_right", GSMetro.BuildUndergroundRailTrack(this.t(x0 - 2, yZ - 2), this.t(x0 + 1, yZ + 1), GSRail.RAILTRACK_NE_SE, h), GSMetro.ERR_UNDERGROUND_PORTAL_NEEDS_STRAIGHT_TRACK);
					this.check_untouched("z_drag_in_right_nothing_built", zr_tiles);

					/* A straight drag on the same hill still works. */
					report.check_true("z_straight_drag", GSMetro.BuildUndergroundRailTrack(this.t(x0 + 2, yZ), this.t(x0 - 3, yZ), GSRail.RAILTRACK_NE_SW, h), "A straight drag out of the zig-zag hill should succeed (err=" + GSError.GetLastErrorString() + ").");
					this.check_mouth("z_straight_mouth", this.t(x0, yZ));
				}

				company_mode = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
