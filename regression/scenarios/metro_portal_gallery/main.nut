/*
 * Every way into the underground in all four directions, kept in the saved
 * game for looking at them (and built here, so each must succeed):
 *
 *  - a 3x3 plateau one level up with a hillside mouth into each of its sides,
 *    each with a short surface line;
 *  - four portals diving into flat ground two levels down, each with a short
 *    surface line on its open side and a tunnel on its other side;
 *  - four ramps from two levels down to three levels down, with track on both
 *    of their levels.
 */
class MetroPortalGallery extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_portal_gallery", null, 510092);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();

		try {
			local area = build.find_buildable_rectangle(26, 26);
			report.check_true("build_area_found", area != null, "Expected a clear area for the gallery.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);

			if (area != null && company_mode != null) {
				report.check_true("area_levelled", build.level_area_min_height(area, 26, 26, 3), "The gallery area should be flat at height >= 3.");
				local L = build.tile_x(area);
				local T = build.tile_y(area);
				local h = GSTile.GetMinHeight(build.make_tile(L + 1, T + 1));
				report.metric("h", h);

				/* direction into the tunnel, step towards it, track along it */
				local dirs = [
					[GSMetro.DIAGDIR_SW, 1, 0, GSRail.RAILTRACK_NE_SW],
					[GSMetro.DIAGDIR_NE, -1, 0, GSRail.RAILTRACK_NE_SW],
					[GSMetro.DIAGDIR_SE, 0, 1, GSRail.RAILTRACK_NW_SE],
					[GSMetro.DIAGDIR_NW, 0, -1, GSRail.RAILTRACK_NW_SE],
				];
				local at = function(tile, d, k) : (build) {
					return build.make_tile(GSMap.GetTileX(tile) + d[1] * k, GSMap.GetTileY(tile) + d[2] * k);
				};

				/* 1. The plateau: mouths on its four sides, the line coming from outside. */
				local px = L + 5;
				local py = T + 5;
				local raised = true;
				/* every corner of the plateau once: the north corner of the tile at that point */
				for (local i = 0; i <= 3; i++) for (local j = 0; j <= 3; j++) raised = GSTile.RaiseTile(build.make_tile(px + i, py + j), GSTile.SLOPE_N) && raised;
				report.check_true("plateau_raised", raised && GSTile.GetSlope(build.make_tile(px + 1, py + 1)) == GSTile.SLOPE_FLAT && GSTile.GetMinHeight(build.make_tile(px + 1, py + 1)) == h + 1, "The plateau should be raised one level.");
				local mouths = [build.make_tile(px - 1, py + 1), build.make_tile(px + 3, py + 1), build.make_tile(px + 1, py - 1), build.make_tile(px + 1, py + 3)];
				foreach (idx, d in dirs) {
					local mouth = mouths[idx];
					report.check_true("hillside_" + idx, GSMetro.BuildPortal(mouth, d[0], GSTile.GetMinHeight(mouth)), "Hillside portal " + idx + " should be built (err=" + GSError.GetLastErrorString() + ").");
					for (local k = 1; k <= 2; k++) GSRail.BuildRailTrack(at(mouth, d, -k), d[3]);
				}
				report.metric("plateau", build.make_tile(px + 1, py + 1));

				/* 2. Portals diving into flat ground two levels down. */
				local depth = h - 2;
				foreach (idx, d in dirs) {
					local tile = build.make_tile(L + 3 + idx * 6, T + 16);
					report.check_true("dive_" + idx, GSMetro.BuildPortal(tile, d[0], depth), "Dive portal " + idx + " should be built (err=" + GSError.GetLastErrorString() + ").");
					for (local k = 1; k <= 2; k++) {
						GSRail.BuildRailTrack(at(tile, d, -k), d[3]);
						GSMetro.BuildUndergroundRail(at(tile, d, k), d[3], depth);
					}
					if (idx == 0) report.metric("dive_0", tile);
				}

				/* 3. Ramps from two levels down to three levels down. */
				foreach (idx, d in dirs) {
					local tile = build.make_tile(L + 3 + idx * 6, T + 22);
					report.check_true("ramp_" + idx, GSMetro.BuildRamp(tile, d[0], depth, depth - 1), "Ramp " + idx + " should be built (err=" + GSError.GetLastErrorString() + ").");
					GSMetro.BuildUndergroundRail(at(tile, d, -1), d[3], depth);
					GSMetro.BuildUndergroundRail(at(tile, d, 1), d[3], depth - 1);
					if (idx == 0) report.metric("ramp_0", tile);
				}

				report.check_true("final_save_written", GSTest.SaveGame("metro_portal_gallery_final.sav"), "Scenario should save final state for looking at it.");
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
