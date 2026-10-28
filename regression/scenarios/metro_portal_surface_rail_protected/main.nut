/*
 * Layout (single row y):
 *
 *   [portal A]--UG(-1) x3--[portal B]   [ramp -1/-2]   [plain rail]
 *
 * Vanilla removal of a portal's surface rail (single track removal, tile
 * demolition) must be refused while the portal exists. GSMetro.RemovePortal
 * removes the portal slice and its surface rail together. GSMetro.RemoveRamp
 * removes a ramp. Plain rail elsewhere is still demolished normally.
 */
class MetroPortalSurfaceRailProtected extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_portal_surface_rail_protected", null, 510042);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);

		try {
			local area = build.find_buildable_rectangle(20, 6);
			report.check_true("build_area_found", area != null, "Expected a clear area for the test layout.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);

			if (area != null && company_mode != null) {
				local x = build.tile_x(area) + 2;
				local y = build.tile_y(area) + 2;
				local depth = -1;

				local portal_a = build.make_tile(x, y);
				local portal_b = build.make_tile(x + 4, y);
				local ramp_tile = build.make_tile(x + 8, y);
				local plain_tile = build.make_tile(x + 11, y);

				report.check_true("portal_a_built", metro.build_portal(portal_a, GSMetro.DIAGDIR_SW, depth), "Portal A should be built.");
				report.check_true("portal_b_built", metro.build_portal(portal_b, GSMetro.DIAGDIR_NE, depth), "Portal B should be built.");
				report.check_true("underground_built", metro.build_underground_x(x + 1, x + 3, y, depth), "Underground track should be built.");
				report.check_true("portal_link", GSMetro.HasPortalConnection(portal_a, portal_b, depth), "Portals should be connected underground.");
				report.check_true("portal_a_surface_rail", GSRail.IsRailTile(portal_a), "Portal A should have surface rail.");

				/* Single track removal of the portal rail is refused. */
				local removed_track = GSRail.RemoveRailTrack(portal_a, GSRail.RAILTRACK_NE_SW);
				report.check_true("remove_rail_track_refused", !removed_track, "GSRail.RemoveRailTrack on a portal tile must fail.");
				report.check_true("rail_after_remove_track", GSRail.IsRailTile(portal_a) && GSRail.GetRailTracks(portal_a) == GSRail.RAILTRACK_NE_SW, "Portal surface rail must survive track removal.");

				/* Dragged removal across the portal tile is refused for that tile. */
				local removed_drag = GSRail.RemoveRail(build.make_tile(x - 1, y), portal_a, build.make_tile(x + 1, y));
				report.check_true("remove_rail_drag_refused", !removed_drag, "GSRail.RemoveRail on a portal tile must fail.");
				report.check_true("rail_after_remove_drag", GSRail.IsRailTile(portal_a), "Portal surface rail must survive dragged removal.");

				/* Demolition is refused. */
				local demolished = GSTile.DemolishTile(portal_a);
				report.check_true("demolish_refused", !demolished, "GSTile.DemolishTile on a portal tile must fail.");
				report.check_true("rail_after_demolish", GSRail.IsRailTile(portal_a), "Portal surface rail must survive demolition.");
				report.check_true("portal_after_demolish", GSMetro.IsPortalTile(portal_a), "Portal must survive demolition.");
				report.check_true("link_after_attempts", GSMetro.HasPortalConnection(portal_a, portal_b, depth), "Portal link must survive refused removals.");

				/* Proper removal through the metro API takes both away. */
				local portal_removed = GSMetro.RemovePortal(portal_a);
				report.check_true("remove_portal_ok", portal_removed, "GSMetro.RemovePortal should succeed (err=" + GSError.GetLastErrorString() + ").");
				report.check_true("no_portal_slice", !GSMetro.IsPortalTile(portal_a), "Portal slice should be gone.");
				report.check_eq("no_slice_at_depth", GSMetro.TK_NONE, GSMetro.GetUndergroundTileKind(portal_a, depth), "No underground slice should remain on the portal tile.");
				report.check_true("no_surface_rail", !GSRail.IsRailTile(portal_a), "Surface rail of the portal should be gone.");
				report.check_true("link_gone", !GSMetro.HasPortalConnection(portal_a, portal_b, depth) && !GSMetro.HasPortalConnection(portal_b, portal_a, depth), "Portal connection should be gone.");
				report.check_true("portal_b_kept", GSMetro.IsPortalTile(portal_b) && GSRail.IsRailTile(portal_b), "The other portal must be untouched.");
				report.check_true("remove_portal_twice_fails", !GSMetro.RemovePortal(portal_a), "Removing a missing portal must fail.");

				report.check_true("remove_portal_b_ok", GSMetro.RemovePortal(portal_b), "GSMetro.RemovePortal on portal B should succeed.");
				report.check_true("portal_b_gone", !GSMetro.IsPortalTile(portal_b) && !GSRail.IsRailTile(portal_b), "Portal B and its rail should be gone.");

				/* Ramp removal. */
				report.check_true("ramp_built", GSMetro.BuildRamp(ramp_tile, GSMetro.DIAGDIR_SW, -1, -2), "Ramp should be built.");
				report.check_eq("ramp_kind", GSMetro.TK_RAMP, GSMetro.GetUndergroundTileKind(ramp_tile, -1), "Ramp slice should exist.");
				report.check_true("remove_ramp_ok", GSMetro.RemoveRamp(ramp_tile, -2), "GSMetro.RemoveRamp should succeed.");
				report.check_eq("ramp_gone_upper", GSMetro.TK_NONE, GSMetro.GetUndergroundTileKind(ramp_tile, -1), "Ramp should be gone at depth -1.");
				report.check_eq("ramp_gone_lower", GSMetro.TK_NONE, GSMetro.GetUndergroundTileKind(ramp_tile, -2), "Ramp should be gone at depth -2.");
				report.check_true("remove_ramp_twice_fails", !GSMetro.RemoveRamp(ramp_tile, -1), "Removing a missing ramp must fail.");

				/* Vanilla demolition of ordinary rail is unaffected. */
				report.check_true("plain_rail_built", GSRail.BuildRailTrack(plain_tile, GSRail.RAILTRACK_NE_SW), "Plain rail should be built.");
				report.check_true("plain_rail_demolished", GSTile.DemolishTile(plain_tile), "Plain rail should still be demolishable.");
				report.check_true("plain_rail_gone", !GSRail.IsRailTile(plain_tile), "Plain rail should be gone.");
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
