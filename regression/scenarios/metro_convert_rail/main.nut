/**
 * metro_convert_rail
 *
 * Underground construction of one rail type is converted to another in an
 * area at one level, like the surface rail conversion: track (with a crossing
 * piece), a platform, a ramp and a portal with its surface rail. The company
 * infrastructure counts move from the old rail type to the new one.
 */
class MetroConvertRail extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_convert_rail", null, 510034);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);

		try {
			local area = build.find_buildable_rectangle(32, 8);
			report.check_true("build_area_found", area != null, "Expected a clear area.");
			local company = companies.ensure_company();
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 3;
				local d = -1;
				local cm = GSCompanyMode(company);

				local from = GSRail.GetCurrentRailType();
				local to = -1;
				foreach (rt, _ in GSRailTypeList()) {
					if (rt != from && GSRail.IsRailTypeAvailable(rt) && GSRail.GetMaxSpeed(rt) >= 0 && GSRail.TrainCanRunOnRail(from, rt)) { to = rt; break; }
				}
				report.check_true("second_railtype", to >= 0, "A second rail type the first one's trains can run on.");
				report.metric("from", GSRail.GetName(from));
				report.metric("to", to >= 0 ? GSRail.GetName(to) : "none");

				local t8 = build.make_tile(x + 8, y), t9 = build.make_tile(x + 9, y), t10 = build.make_tile(x + 10, y);
				local t12 = build.make_tile(x + 12, y);
				local ramp_tile = build.make_tile(x + 16, y);
				local portal_tile = build.make_tile(x + 22, y);
				local beyond = build.make_tile(x + 26, y);

				report.check_true("chain_built", metro.build_underground_x(x + 8, x + 10, y, d), "Underground chain.");
				report.check_true("crossing_built", GSMetro.BuildUndergroundRail(t9, GSRail.RAILTRACK_NW_SE, d), "Crossing piece.");
				report.check_true("station_built", metro.build_station_x(t12, d), "Platform.");
				report.check_true("ramp_built", GSMetro.BuildRamp(ramp_tile, GSMetro.DIAGDIR_SW, d, d - 1), "Ramp.");
				report.check_true("portal_built", metro.build_portal(portal_tile, GSMetro.DIAGDIR_SW, d), "Portal.");
				report.check_true("beyond_built", GSMetro.BuildUndergroundRail(beyond, GSRail.RAILTRACK_NE_SW, d), "Track outside the area.");

				if (to >= 0) {
					local rail_from = GSInfrastructure.GetRailPieceCount(company, from);
					local rail_to = GSInfrastructure.GetRailPieceCount(company, to);

					GSRail.SetCurrentRailType(to);
					local money = GSCompany.GetBankBalance(company);
					report.check_true("converted", GSMetro.ConvertUndergroundRail(t8, portal_tile, d), "Conversion of the area should succeed: " + GSError.GetLastErrorString());
					report.check_true("cost_paid", GSCompany.GetBankBalance(company) < money, "Conversion should cost money.");

					foreach (name, tile in {track = t8, crossing = t9, track_end = t10, platform = t12, ramp = ramp_tile, portal = portal_tile}) {
						report.check_eq("type_" + name, to, GSMetro.GetUndergroundRailType(tile, d), name + " should have the new rail type.");
					}
					report.check_eq("crossing_kept", GSRail.RAILTRACK_NE_SW | GSRail.RAILTRACK_NW_SE, GSMetro.GetUndergroundTrackBits(t9, d), "Conversion should keep the track pieces.");
					report.check_eq("portal_surface_rail", to, GSRail.GetRailType(portal_tile), "The portal's surface rail should be converted with it.");
					report.check_eq("type_outside", from, GSMetro.GetUndergroundRailType(beyond, d), "Track outside the area should keep its rail type.");

					/* Track (4 pieces), platform, ramp, portal slice and portal surface rail. */
					report.check_eq("count_from", rail_from - 8, GSInfrastructure.GetRailPieceCount(company, from), "Pieces should leave the old rail type.");
					report.check_eq("count_to", rail_to + 8, GSInfrastructure.GetRailPieceCount(company, to), "Pieces should be counted for the new rail type.");

					report.check_true("nothing_left", !GSMetro.ConvertUndergroundRail(t8, portal_tile, d), "Converting again should find nothing to convert.");

					GSRail.SetCurrentRailType(from);
					report.check_true("converted_back", GSMetro.ConvertUndergroundRail(t8, t12, d), "Converting back part of the area should succeed.");
					report.check_eq("back_track", from, GSMetro.GetUndergroundRailType(t8, d), "Track should be converted back.");
					report.check_eq("back_ramp_untouched", to, GSMetro.GetUndergroundRailType(ramp_tile, d), "The ramp outside the second area should keep the new type.");
				}
				cm = null;
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
