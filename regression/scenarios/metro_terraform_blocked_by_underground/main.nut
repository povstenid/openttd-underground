class MetroTerraformBlockedByUnderground extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_terraform_blocked_by_underground", null, 510012);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();

		try {
			local area = build.find_buildable_rectangle(8, 8);
			report.check_true("build_area_found", area != null, "Expected a clear area for the terraform regression.");

			local company = companies.ensure_company();
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local company_mode = GSCompanyMode(company);

				report.check_true("area_levelled", build.level_area(area, 8, 8), "Test area should be flat.");

				local tile = build.make_tile(build.tile_x(area) + 3, build.tile_y(area) + 3);
				/* Lowering needs at least height 1. */
				if (GSTile.GetMinHeight(tile) < 1) GSTile.RaiseTile(tile, GSTile.SLOPE_ELEVATED);

				local h = GSTile.GetMinHeight(tile);
				report.check_true("tile_flat", GSTile.GetSlope(tile) == GSTile.SLOPE_FLAT, "Test tile should be flat.");
				report.check_true("tile_above_sea_level", h >= 1, "Test tile should be at height >= 1.");

				/* Slices are absolute: depth z spans [z, z + 1). The slice directly below
				 * a surface at height h is depth h - 1; a slice at h - 2 has one level of
				 * clearance and must not block lowering the surface by one. */
				local shallow = h - 1;
				local deep = h - 2;

				report.check_true("deep_rail_built", GSMetro.BuildUndergroundRail(tile, GSRail.RAILTRACK_NE_SW, deep), "Deep underground rail should be built.");
				report.check_true("shallow_rail_built", GSMetro.BuildUndergroundRail(tile, GSRail.RAILTRACK_NE_SW, shallow), "Shallow underground rail should be built directly under the surface.");

				report.check_true("lower_blocked", !GSTile.LowerTile(tile, GSTile.SLOPE_N), "Lowering the north corner must fail while rail sits directly below.");
				report.metric("lower_blocked_error", GSError.GetLastErrorString());
				report.check_eq("corner_unchanged", h, GSTile.GetCornerHeight(tile, GSTile.CORNER_N), "North corner height must be unchanged after the refused lowering.");
				report.check_true("shallow_rail_intact", GSMetro.HasUndergroundRail(tile, shallow), "Shallow rail must survive the refused lowering.");

				report.check_true("shallow_rail_removed", GSMetro.RemoveUndergroundRail(tile, GSRail.RAILTRACK_NE_SW, shallow), "Removing the shallow underground rail should succeed.");
				report.check_true("lower_allowed", GSTile.LowerTile(tile, GSTile.SLOPE_N), "Lowering must succeed once only deeper rail remains.");
				report.check_eq("corner_lowered", h - 1, GSTile.GetCornerHeight(tile, GSTile.CORNER_N), "North corner should be one level lower.");
				report.check_true("deep_rail_intact", GSMetro.HasUndergroundRail(tile, deep), "Deep rail must survive the lowering.");

				report.check_true("lower_again_blocked", !GSTile.LowerTile(tile, GSTile.SLOPE_N), "Lowering again must fail because the deep rail would be uncovered.");
				report.check_true("raise_allowed", GSTile.RaiseTile(tile, GSTile.SLOPE_N), "Raising never conflicts with the underground.");

				report.metric("tile", tile);
				report.metric("height", h);

				company_mode = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
