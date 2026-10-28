/*
 * An underground level z is valid under a tile iff z lies strictly below the
 * lowest corner of the tile (the vanilla tunnel rule), whatever its other
 * corners do. Terraforming must not break that rule for existing slices.
 *
 * Layout (row y, X axis): A at (x, y), B at (x + 1, y), C at (x + 4, y).
 *   A: flat at h, rail at h - 1.
 *   B: west corner lowered -> lowest corner h - 1.
 *   C: north corner raised -> lowest corner still h, highest h + 1.
 */
class MetroSliceRuleLowestCorner extends GSController {
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

		this.report = MetroTest("metro_slice_rule_lowest_corner", null, 510073);
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();

		try {
			local area = build.find_buildable_rectangle(10, 8);
			report.check_true("build_area_found", area != null, "Expected a clear area for the slice rule regression.");

			local company = companies.ensure_company();
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local company_mode = GSCompanyMode(company);

				/* Height >= 2 keeps every corner involved above sea level. */
				report.check_true("area_levelled", build.level_area_min_height(area, 10, 8, 2), "Test area should be flat at height >= 2.");

				local x = build.tile_x(area) + 2;
				local y = build.tile_y(area) + 3;
				local a = build.make_tile(x, y);
				local b = build.make_tile(x + 1, y);
				local c = build.make_tile(x + 4, y);
				local h = GSTile.GetMinHeight(a);
				report.metric("h", h);
				report.check_true("a_flat", GSTile.GetSlope(a) == GSTile.SLOPE_FLAT && h >= 2, "Tile A should be flat at height >= 2.");

				/* A: the level right below the surface is valid, the surface level is not. */
				report.check_true("a_rail_below_surface", GSMetro.BuildUndergroundRail(a, GSRail.RAILTRACK_NE_SW, h - 1), "Rail at h - 1 under flat A should be built (err=" + GSError.GetLastErrorString() + ").");
				this.expect_error("a_rail_at_surface", GSMetro.BuildUndergroundRail(a, GSRail.RAILTRACK_NE_SW, h), GSMetro.ERR_UNDERGROUND_DEPTH_INVALID);

				/* B: lowering one corner (not shared with A) gives a slope with lowest corner h - 1. */
				report.check_true("b_corner_lowered", GSTile.LowerTile(b, GSTile.SLOPE_W), "Lowering the west corner of B should succeed (err=" + GSError.GetLastErrorString() + ").");
				report.check_true("b_sloped", GSTile.GetSlope(b) != GSTile.SLOPE_FLAT, "B should be sloped.");
				report.check_eq("b_lowest_corner", h - 1, GSTile.GetMinHeight(b), "The lowest corner of B should be h - 1.");
				report.check_eq("b_highest_corner", h, GSTile.GetMaxHeight(b), "The highest corner of B should be h.");
				report.check_true("a_still_flat", GSTile.GetSlope(a) == GSTile.SLOPE_FLAT && GSTile.GetMinHeight(a) == h, "A should be unaffected.");

				this.expect_error("b_rail_at_lowest_corner", GSMetro.BuildUndergroundRail(b, GSRail.RAILTRACK_NE_SW, h - 1), GSMetro.ERR_UNDERGROUND_DEPTH_INVALID);
				report.check_true("b_no_rail_at_lowest_corner", !GSMetro.HasUndergroundRail(b, h - 1), "No rail should exist at h - 1 under B.");
				report.check_true("b_rail_below_lowest_corner", GSMetro.BuildUndergroundRail(b, GSRail.RAILTRACK_NE_SW, h - 2), "Rail at h - 2 under B should be built (err=" + GSError.GetLastErrorString() + ").");
				report.check_true("b_rail_present", GSMetro.HasUndergroundRail(b, h - 2), "Rail at h - 2 under B should exist.");

				/* Lowering another corner of B keeps its lowest corner at h - 1: still above the rail at h - 2. */
				report.check_true("b_lower_keeps_lowest", GSTile.LowerTile(b, GSTile.SLOPE_S), "Lowering the south corner of B to h - 1 should succeed above rail at h - 2 (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("b_lowest_corner_after", h - 1, GSTile.GetMinHeight(b), "The lowest corner of B should still be h - 1.");
				report.check_true("b_rail_intact", GSMetro.HasUndergroundRail(b, h - 2), "Rail at h - 2 under B must survive.");

				/* C: raising one corner does not change the lowest corner, so the rule is unchanged. */
				report.check_true("c_corner_raised", GSTile.RaiseTile(c, GSTile.SLOPE_N), "Raising the north corner of C should succeed (err=" + GSError.GetLastErrorString() + ").");
				report.check_eq("c_lowest_corner", h, GSTile.GetMinHeight(c), "The lowest corner of C should be h.");
				report.check_eq("c_highest_corner", h + 1, GSTile.GetMaxHeight(c), "The highest corner of C should be h + 1.");
				this.expect_error("c_rail_at_lowest_corner", GSMetro.BuildUndergroundRail(c, GSRail.RAILTRACK_NE_SW, h), GSMetro.ERR_UNDERGROUND_DEPTH_INVALID);
				report.check_true("c_rail_below_lowest_corner", GSMetro.BuildUndergroundRail(c, GSRail.RAILTRACK_NE_SW, h - 1), "Rail at h - 1 under C should be built (err=" + GSError.GetLastErrorString() + ").");

				/* A: lowering a corner would bring the lowest corner down to the rail at h - 1. */
				this.expect_error("a_lower_over_rail", GSTile.LowerTile(a, GSTile.SLOPE_N), GSMetro.ERR_UNDERGROUND_IN_THE_WAY);
				report.check_eq("a_corner_unchanged", h, GSTile.GetCornerHeight(a, GSTile.CORNER_N), "The north corner of A must be unchanged.");
				report.check_true("a_rail_intact", GSMetro.HasUndergroundRail(a, h - 1), "Rail at h - 1 under A must survive.");
				/* The corner A shares with B is refused the same way. */
				this.expect_error("a_lower_shared_corner", GSTile.LowerTile(b, GSTile.SLOPE_N), GSMetro.ERR_UNDERGROUND_IN_THE_WAY);
				report.check_eq("a_shared_corner_unchanged", h, GSTile.GetCornerHeight(a, GSTile.CORNER_W), "The corner A shares with B must be unchanged.");

				company_mode = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
