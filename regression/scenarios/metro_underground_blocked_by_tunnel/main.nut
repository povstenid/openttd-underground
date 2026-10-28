class MetroUndergroundBlockedByTunnel extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_underground_blocked_by_tunnel", null, 510014);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();

		try {
			local area = build.find_buildable_rectangle(12, 8);
			report.check_true("build_area_found", area != null, "Expected a clear area for the tunnel regression.");

			local company = companies.ensure_company();
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local company_mode = GSCompanyMode(company);

				report.check_true("area_levelled", build.level_area(area, 12, 8), "Test area should be flat.");

				local x0 = build.tile_x(area) + 2;
				local y = build.tile_y(area) + 3;
				local ridge = build.build_ridge_x(x0, y, 3);
				report.check_true("ridge_built", ridge != null, "A one-level ridge should be raised for the tunnel.");

				if (ridge != null) {
					local z = ridge.base_z;
					local mid = build.make_tile(x0 + 2, y);
					local side = build.make_tile(x0 + 3, y);

					report.check_true("tunnel_built", GSTunnel.BuildTunnel(GSVehicle.VT_RAIL, ridge.start), "The vanilla tunnel should be built.");
					report.check_true("tunnel_entrance_present", GSTunnel.IsTunnelTile(ridge.start) && GSTunnel.IsTunnelTile(ridge.end), "Both tunnel entrances should exist.");

					/* Same level as the tunnel: refused. */
					report.check_true("rail_at_tunnel_level_blocked", !GSMetro.BuildUndergroundRail(mid, GSRail.RAILTRACK_NW_SE, z), "Underground rail must not be built inside a vanilla tunnel.");
					report.check_eq("rail_blocked_error", GSTunnel.ERR_TUNNEL_ANOTHER_TUNNEL_IN_THE_WAY, GSError.GetLastError(), "Refusal should report another tunnel in the way.");
					report.check_true("no_rail_after_refusal", !GSMetro.HasUndergroundRail(mid, z), "No underground rail should exist at the tunnel level.");
					report.check_true("station_at_tunnel_level_blocked", !GSMetro.BuildUndergroundStation(side, GSRail.RAILTRACK_NE_SW, z, GSStation.STATION_NEW), "Underground station must not be built inside a vanilla tunnel.");

					/* A different depth is fine. */
					report.check_true("rail_below_tunnel_built", GSMetro.BuildUndergroundRail(mid, GSRail.RAILTRACK_NW_SE, z - 1), "Underground rail below the tunnel level should be built.");
					report.check_true("rail_below_tunnel_present", GSMetro.HasUndergroundRail(mid, z - 1), "Underground rail below the tunnel should exist.");

					/* Removing the tunnel frees the level again. */
					report.check_true("tunnel_removed", GSTunnel.RemoveTunnel(ridge.start), "The vanilla tunnel should be removable.");
					report.check_true("rail_at_level_after_removal", GSMetro.BuildUndergroundRail(mid, GSRail.RAILTRACK_NW_SE, z), "Underground rail at the former tunnel level should be built after removal.");

					report.metric("tunnel_start", ridge.start);
					report.metric("tunnel_z", z);
				}

				company_mode = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
