/**
 * metro_drag_exit_flat
 *
 * On flat ground an underground line leaves for the surface where it is
 * dragged onto surface rail of its own course: the last tile below ground
 * becomes a portal diving into the ground, the surface rail stays. In both
 * drag directions, and when an existing tunnel is extended to the surface
 * (its last track piece is replaced by the portal).
 */
class MetroDragExitFlat extends GSController {
	report = null;
	build = null;

	function t(x, y) { return this.build.make_tile(x, y); }

	function check_exit(id, x, y, d)
	{
		local ok = true;
		for (local i = 0; i <= 4; i++) ok = ok && GSMetro.GetUndergroundTileKind(this.t(x + i, y), d) == GSMetro.TK_TRACK;
		this.report.check_true(id + "_tunnel", ok, "Tiles before the exit should carry underground track.");
		local p = this.t(x + 5, y);
		this.report.check_true(id + "_portal", GSMetro.IsPortalTile(p) && GSMetro.GetUndergroundTileKind(p, d) == GSMetro.TK_PORTAL, "The last tile below ground should be a portal.");
		this.report.check_true(id + "_portal_rail", GSRail.IsRailTile(p) && GSRail.GetRailTracks(p) == GSRail.RAILTRACK_NE_SW, "The portal should carry surface X rail.");
		local s = this.t(x + 6, y);
		this.report.check_true(id + "_surface", GSRail.IsRailTile(s) && !GSMetro.HasUnderground(s), "The surface rail should stay, with nothing below it.");
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		this.report = MetroTest("metro_drag_exit_flat", null, 510036);
		local report = this.report;
		local companies = MetroTest.MetroCompanyLib(report);
		this.build = MetroTest.MetroBuildLib();
		local build = this.build;
		local metro = MetroTest.MetroLib(build);

		try {
			local area = build.find_buildable_rectangle(20, 12);
			report.check_true("build_area_found", area != null, "Expected a clear area.");
			local company = companies.ensure_company();
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local cm = GSCompanyMode(company);
				local x = build.tile_x(area) + 2;
				local y = build.tile_y(area) + 2;
				report.check_true("levelled", build.level_area_min_height(this.t(x - 1, y - 1), 12, 9, 2), "Flat ground at height 2 or more.");
				local z = GSTile.GetMinHeight(this.t(x, y));
				report.metric("surface", z);

				foreach (dy in [0, 2, 4]) {
					report.check_true("surface_rail_" + dy, GSRail.BuildRailTrack(this.t(x + 6, y + dy), GSRail.RAILTRACK_NE_SW) && GSRail.BuildRailTrack(this.t(x + 7, y + dy), GSRail.RAILTRACK_NE_SW), "Surface rail.");
				}

				/* Out: from the tunnel onto surface rail. */
				report.check_true("drag_out", GSMetro.BuildUndergroundRailTrack(this.t(x, y), this.t(x + 6, y), GSRail.RAILTRACK_NE_SW, z - 1), "Drag out onto surface rail: " + GSError.GetLastErrorString());
				this.check_exit("out", x, y, z - 1);

				/* In: from surface rail into the ground. */
				report.check_true("drag_in", GSMetro.BuildUndergroundRailTrack(this.t(x + 6, y + 2), this.t(x, y + 2), GSRail.RAILTRACK_NE_SW, z - 1), "Drag in from surface rail: " + GSError.GetLastErrorString());
				this.check_exit("in", x, y + 2, z - 1);

				/* An existing tunnel two levels down extended to the surface. */
				report.check_true("tunnel_built", metro.build_underground_x(x, x + 5, y + 4, z - 2), "Tunnel.");
				report.check_true("drag_extend", GSMetro.BuildUndergroundRailTrack(this.t(x + 3, y + 4), this.t(x + 6, y + 4), GSRail.RAILTRACK_NE_SW, z - 2), "Extend the tunnel onto surface rail: " + GSError.GetLastErrorString());
				this.check_exit("extend", x, y + 4, z - 2);

				/* Without surface rail at the end the line stays underground. */
				report.check_true("drag_plain", GSMetro.BuildUndergroundRailTrack(this.t(x, y + 6), this.t(x + 6, y + 6), GSRail.RAILTRACK_NE_SW, z - 1), "Plain drag.");
				report.check_true("plain_no_portal", !GSMetro.IsPortalTile(this.t(x + 5, y + 6)) && GSMetro.GetUndergroundTileKind(this.t(x + 6, y + 6), z - 1) == GSMetro.TK_TRACK, "No portal without surface rail.");
				cm = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
