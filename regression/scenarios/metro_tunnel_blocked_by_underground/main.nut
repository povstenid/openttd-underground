class MetroTunnelBlockedByUnderground extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_tunnel_blocked_by_underground", null, 510013);
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

					report.check_eq("tunnel_other_end", ridge.end, GSTunnel.GetOtherTunnelEnd(ridge.start), "Tunnel through the ridge should end at the far slope.");

					/* Rail one level below the tunnel must not interfere. */
					report.check_true("deep_rail_built", GSMetro.BuildUndergroundRail(mid, GSRail.RAILTRACK_NW_SE, z - 1), "Underground rail below the tunnel level should be built.");

					/* Rail exactly at the tunnel level blocks it. */
					report.check_true("level_rail_built", GSMetro.BuildUndergroundRail(mid, GSRail.RAILTRACK_NW_SE, z), "Underground rail at the tunnel level should be built under the ridge.");
					report.check_true("tunnel_blocked", !GSTunnel.BuildTunnel(GSVehicle.VT_RAIL, ridge.start), "A tunnel must not cut through underground rail at its level.");
					report.check_eq("tunnel_blocked_error", GSTunnel.ERR_TUNNEL_ANOTHER_TUNNEL_IN_THE_WAY, GSError.GetLastError(), "Refusal should report another tunnel in the way.");
					report.check_true("no_tunnel_after_refusal", !GSTunnel.IsTunnelTile(ridge.start), "No tunnel entrance should exist after the refusal.");
					report.check_true("level_rail_intact", GSMetro.HasUndergroundRail(mid, z), "Underground rail must survive the refused tunnel.");

					report.check_true("level_rail_removed", GSMetro.RemoveUndergroundRail(mid, GSRail.RAILTRACK_NW_SE, z), "Removing the blocking underground rail should succeed.");
					report.check_true("tunnel_built", GSTunnel.BuildTunnel(GSVehicle.VT_RAIL, ridge.start), "The tunnel should be built once the level is free.");
					report.check_true("tunnel_entrance_present", GSTunnel.IsTunnelTile(ridge.start) && GSTunnel.IsTunnelTile(ridge.end), "Both tunnel entrances should exist.");
					report.check_true("deep_rail_intact", GSMetro.HasUndergroundRail(mid, z - 1), "Deeper underground rail must remain.");

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
