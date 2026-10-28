class MetroTestLib {
	_suite = null;
	_fixture = null;
	_seed = null;
	_checks = null;
	_metrics = null;
	_finished = false;

	constructor(suite, fixture = null, seed = null)
	{
		this._suite = suite;
		this._fixture = fixture;
		this._seed = seed;
		this._checks = [];
		this._metrics = {};
	}

	function _stringify(value)
	{
		if (value == null) return "null";
		if (typeof value == "bool") return value ? "true" : "false";
		return value.tostring();
	}

	function _append_check(id, ok, expected, actual, message)
	{
		this._checks.append({
			id = id,
			status = ok ? "pass" : "fail",
			expected = this._stringify(expected),
			actual = this._stringify(actual),
			message = message == null ? "" : this._stringify(message)
		});
	}

	function check_true(id, cond, message = "")
	{
		this._append_check(id, cond, true, cond, message);
		return cond;
	}

	function check_eq(id, expected, actual, message = "")
	{
		local ok = expected == actual;
		this._append_check(id, ok, expected, actual, message);
		return ok;
	}

	function check_range(id, min_value, max_value, actual, message = "")
	{
		local ok = actual >= min_value && actual <= max_value;
		this._append_check(id, ok, min_value + ".." + max_value, actual, message);
		return ok;
	}

	function metric(name, value)
	{
		this._metrics[name] <- value;
	}

	function finish(message = null)
	{
		if (this._finished) throw "MetroTestLib.finish() can only be called once";
		this._finished = true;

		local failed = false;
		foreach (check in this._checks) {
			if (check.status != "pass") {
				failed = true;
				break;
			}
		}

		local final_message = message;
		if (final_message == null) {
			final_message = failed ? "one or more checks failed" : "all checks passed";
		}

		local report = {
			suite = this._suite,
			fixture = this._fixture,
			seed = this._seed,
			tick = GSController.GetTick(),
			status = failed ? "fail" : "pass",
			message = final_message,
			checks = this._checks,
			metrics = this._metrics
		};

		if (!GSTest.Report(report)) throw "Unable to emit structured metro scenario report";
		return !failed;
	}
}

class MetroCompanyLib {
	_report = null;

	constructor(report)
	{
		this._report = report;
	}

	function ensure_company()
	{
		local company = GSCompany.ResolveCompanyID(GSCompany.COMPANY_FIRST);
		if (company == GSCompany.COMPANY_INVALID) {
			company = GSMetro.CreateCompany(GSCompany.COMPANY_INVALID);
		}

		this._report.check_true("company_available", company != GSCompany.COMPANY_INVALID, "Expected a playable company for the metro scenario.");
		if (company == GSCompany.COMPANY_INVALID) return company;

		this._report.check_true("company_funded", GSCompany.ChangeBankBalance(company, 1000000000, GSCompany.EXPENSES_OTHER, GSMap.TILE_INVALID), "Expected the test company to receive funding.");

		local company_mode = GSCompanyMode(company);
		local railtypes = GSRailTypeList();
		local railtype = railtypes.Begin();
		this._report.check_true("railtype_available", !railtypes.IsEnd(), "Expected at least one available rail type for metro test construction.");
		if (!railtypes.IsEnd()) {
			GSRail.SetCurrentRailType(railtype);
		}
		GSCompany.SetLoanAmount(0);
		company_mode = null;

		return company;
	}
}

class MetroBuildLib {
	function make_tile(x, y)
	{
		return GSMap.GetTileIndex(x, y);
	}

	function tile_x(tile)
	{
		return GSMap.GetTileX(tile);
	}

	function tile_y(tile)
	{
		return GSMap.GetTileY(tile);
	}

	function find_buildable_rectangle(width, height)
	{
		local max_x = GSMap.GetMapSizeX() - width - 1;
		local max_y = GSMap.GetMapSizeY() - height - 1;

		for (local y = 1; y <= max_y; y++) {
			for (local x = 1; x <= max_x; x++) {
				local tile = this.make_tile(x, y);
				if (GSTile.IsBuildableRectangle(tile, width, height)) return tile;
			}
		}

		return null;
	}

	function build_surface_x(x1, x2, y)
	{
		local start_x = x1;
		local end_x = x2;
		if (start_x > end_x) {
			start_x = x2;
			end_x = x1;
		}

		for (local x = start_x; x <= end_x; x++) {
			if (!GSRail.BuildRailTrack(this.make_tile(x, y), GSRail.RAILTRACK_NE_SW)) return false;
		}
		return true;
	}

	function build_surface_y(x, y1, y2)
	{
		local start_y = y1;
		local end_y = y2;
		if (start_y > end_y) {
			start_y = y2;
			end_y = y1;
		}

		for (local y = start_y; y <= end_y; y++) {
			if (!GSRail.BuildRailTrack(this.make_tile(x, y), GSRail.RAILTRACK_NW_SE)) return false;
		}
		return true;
	}

	/**
	 * Level a rectangle (ignoring "already flat" errors) and report whether
	 * every tile in it ended up flat.
	 */
	function level_area(top_left, width, height)
	{
		local x = this.tile_x(top_left);
		local y = this.tile_y(top_left);
		GSTile.LevelTiles(top_left, this.make_tile(x + width, y + height));

		for (local dy = 0; dy < height; dy++) {
			for (local dx = 0; dx < width; dx++) {
				if (GSTile.GetSlope(this.make_tile(x + dx, y + dy)) != GSTile.SLOPE_FLAT) return false;
			}
		}
		return true;
	}

	/**
	 * Level a rectangle like level_area and raise it until it lies at least at
	 * min_height, so levels below the surface stay above the deepest allowed one.
	 */
	function level_area_min_height(top_left, width, height, min_height)
	{
		if (!this.level_area(top_left, width, height)) return false;
		for (local i = 0; i < 8 && GSTile.GetMinHeight(top_left) < min_height; i++) {
			/* LevelTiles levels to the north corner of its first tile. */
			if (!GSTile.RaiseTile(top_left, GSTile.SLOPE_ELEVATED)) return false;
			if (!this.level_area(top_left, width, height)) return false;
		}
		return GSTile.GetMinHeight(top_left) >= min_height;
	}

	/**
	 * Raise a flat rectangle of tiles (x1, y1) .. (x2, y2) one level, leaving
	 * one-level inclines around it: tiles (x1 - 1, y) become SLOPE_SW and
	 * tiles (x2 + 1, y) SLOPE_NE for y1 <= y <= y2.
	 * @return The height of the surroundings (the foot of the inclines) or null on failure.
	 */
	function raise_block(x1, y1, x2, y2)
	{
		local base_z = GSTile.GetMinHeight(this.make_tile(x1, y1));
		local corners = [
			[GSTile.CORNER_N, GSTile.SLOPE_N],
			[GSTile.CORNER_W, GSTile.SLOPE_W],
			[GSTile.CORNER_S, GSTile.SLOPE_S],
			[GSTile.CORNER_E, GSTile.SLOPE_E]
		];
		for (local y = y1; y <= y2; y++) {
			for (local x = x1; x <= x2; x++) {
				local t = this.make_tile(x, y);
				local mask = 0;
				foreach (c in corners) {
					if (GSTile.GetCornerHeight(t, c[0]) == base_z) mask = mask | c[1];
				}
				if (mask != 0 && !GSTile.RaiseTile(t, mask)) return null;
			}
		}
		for (local y = y1; y <= y2; y++) {
			for (local x = x1; x <= x2; x++) {
				local t = this.make_tile(x, y);
				if (GSTile.GetSlope(t) != GSTile.SLOPE_FLAT || GSTile.GetMinHeight(t) != base_z + 1) return null;
			}
			if (GSTile.GetSlope(this.make_tile(x1 - 1, y)) != GSTile.SLOPE_SW) return null;
			if (GSTile.GetSlope(this.make_tile(x2 + 1, y)) != GSTile.SLOPE_NE) return null;
		}
		return base_z;
	}

	/**
	 * Build a one-level ridge along the X axis on a flat row, suitable for a
	 * vanilla tunnel: tile (x0, y) becomes SLOPE_SW, tiles x0+1 .. x0+inner_len
	 * become flat one level higher and tile x0+inner_len+1 becomes SLOPE_NE.
	 * @return Table {start, end, base_z} or null on failure.
	 */
	function build_ridge_x(x0, y, inner_len)
	{
		local base_z = GSTile.GetMinHeight(this.make_tile(x0, y));
		if (!GSTile.RaiseTile(this.make_tile(x0 + 1, y), GSTile.SLOPE_ELEVATED)) return null;
		for (local i = 2; i <= inner_len; i++) {
			if (!GSTile.RaiseTile(this.make_tile(x0 + i, y), GSTile.SLOPE_SW)) return null;
		}

		local start = this.make_tile(x0, y);
		local end = this.make_tile(x0 + inner_len + 1, y);
		if (GSTile.GetSlope(start) != GSTile.SLOPE_SW) return null;
		if (GSTile.GetSlope(end) != GSTile.SLOPE_NE) return null;
		for (local i = 1; i <= inner_len; i++) {
			local t = this.make_tile(x0 + i, y);
			if (GSTile.GetSlope(t) != GSTile.SLOPE_FLAT || GSTile.GetMinHeight(t) != base_z + 1) return null;
		}

		return { start = start, end = end, base_z = base_z };
	}

	function build_surface_station_x(tile, station_id = GSStation.STATION_NEW)
	{
		return GSRail.BuildRailStation(tile, GSRail.RAILTRACK_NE_SW, 1, 2, station_id);
	}

	function build_surface_station_y(tile, station_id = GSStation.STATION_NEW)
	{
		return GSRail.BuildRailStation(tile, GSRail.RAILTRACK_NW_SE, 1, 2, station_id);
	}
}

class MetroLib {
	_builder = null;

	constructor(builder)
	{
		this._builder = builder;
	}

	function _slope_for_dir(dir)
	{
		switch (dir) {
			case GSMetro.DIAGDIR_NE: return GSTile.SLOPE_NE;
			case GSMetro.DIAGDIR_SE: return GSTile.SLOPE_SE;
			case GSMetro.DIAGDIR_SW: return GSTile.SLOPE_SW;
			case GSMetro.DIAGDIR_NW: return GSTile.SLOPE_NW;
		}
		return GSTile.SLOPE_INVALID;
	}

	function ensure_portal_slope(tile, dir)
	{
		local target = this._slope_for_dir(dir);
		if (target == GSTile.SLOPE_INVALID) return false;
		local current = GSTile.GetSlope(tile);
		if (current == target) return true;
		if (current != GSTile.SLOPE_FLAT) return false;
		return GSTile.RaiseTile(tile, target);
	}

	function build_portal(tile, dir, depth)
	{
		this.ensure_portal_slope(tile, dir);
		return GSMetro.BuildPortal(tile, dir, depth);
	}

	function build_underground_x(x1, x2, y, depth)
	{
		local start_x = x1;
		local end_x = x2;
		if (start_x > end_x) {
			start_x = x2;
			end_x = x1;
		}

		for (local x = start_x; x <= end_x; x++) {
			if (!GSMetro.BuildUndergroundRail(this._builder.make_tile(x, y), GSRail.RAILTRACK_NE_SW, depth)) return false;
		}
		return true;
	}

	function build_underground_y(x, y1, y2, depth)
	{
		local start_y = y1;
		local end_y = y2;
		if (start_y > end_y) {
			start_y = y2;
			end_y = y1;
		}

		for (local y = start_y; y <= end_y; y++) {
			if (!GSMetro.BuildUndergroundRail(this._builder.make_tile(x, y), GSRail.RAILTRACK_NW_SE, depth)) return false;
		}
		return true;
	}

	function build_station_x(tile, depth, station_id = GSStation.STATION_NEW)
	{
		return GSMetro.BuildUndergroundStation(tile, GSRail.RAILTRACK_NE_SW, depth, station_id);
	}

	function build_station_y(tile, depth, station_id = GSStation.STATION_NEW)
	{
		return GSMetro.BuildUndergroundStation(tile, GSRail.RAILTRACK_NW_SE, depth, station_id);
	}
}

class MetroWorldLib {
	_builder = null;

	constructor(builder)
	{
		this._builder = builder;
	}

	function _list_to_array(list)
	{
		local result = [];
		for (local item = list.Begin(); !list.IsEnd(); item = list.Next()) {
			result.append(item);
		}
		return result;
	}

	function _snapshot_items(list)
	{
		local items = {};
		foreach (item in this._list_to_array(list)) {
			items[item.tostring()] <- item;
		}
		return items;
	}

	function _find_new_item(snapshot, list)
	{
		foreach (item in this._list_to_array(list)) {
			if (!(item.tostring() in snapshot)) return item;
		}
		return null;
	}

	function find_passenger_cargo()
	{
		local cargos = GSCargoList();
		for (local cargo = cargos.Begin(); !cargos.IsEnd(); cargo = cargos.Next()) {
			if (!GSCargo.IsValidCargo(cargo)) continue;
			if (!GSCargo.HasCargoClass(cargo, GSCargo.CC_PASSENGERS)) continue;
			return cargo;
		}
		return null;
	}

	function create_town(tile, name, extra_houses = 0)
	{
		local before = GSTownList();
		local snapshot = this._snapshot_items(before);
		if (!GSTown.FoundTown(tile, GSTown.TOWN_SIZE_MEDIUM, false, GSTown.ROAD_LAYOUT_2x2, name)) return null;

		local town_id = this._find_new_item(snapshot, GSTownList());
		if (town_id == null) town_id = GSTile.GetClosestTown(tile);
		if (!GSTown.IsValidTown(town_id)) return null;

		if (extra_houses > 0) {
			GSTown.ExpandTown(town_id, extra_houses);
		}

		return town_id;
	}

	function create_town_in_area(top_left, width, height, name, extra_houses = 0)
	{
		for (local dy = 0; dy < height; dy++) {
			for (local dx = 0; dx < width; dx++) {
				local tile = this._builder.make_tile(this._builder.tile_x(top_left) + dx, this._builder.tile_y(top_left) + dy);
				local town_id = this.create_town(tile, name, extra_houses);
				if (town_id != null && GSTown.IsValidTown(town_id)) return town_id;
			}
		}

		return null;
	}

	function create_town_near(target_tile, radius, name, extra_houses = 0)
	{
		local target_x = this._builder.tile_x(target_tile);
		local target_y = this._builder.tile_y(target_tile);

		for (local dist = 0; dist <= radius; dist++) {
			for (local dy = -dist; dy <= dist; dy++) {
				for (local dx = -dist; dx <= dist; dx++) {
					if (max(abs(dx), abs(dy)) != dist) continue;

					local x = target_x + dx;
					local y = target_y + dy;
					if (x < 0 || y < 0 || x >= GSMap.GetMapSizeX() || y >= GSMap.GetMapSizeY()) continue;

					local tile = this._builder.make_tile(x, y);
					local town_id = this.create_town(tile, name, extra_houses);
					if (town_id != null && GSTown.IsValidTown(town_id)) return town_id;
				}
			}
		}

		return null;
	}

	function build_industry_in_area(industry_type, top_left, width, height)
	{
		for (local dy = 0; dy < height; dy++) {
			for (local dx = 0; dx < width; dx++) {
				local tile = this._builder.make_tile(this._builder.tile_x(top_left) + dx, this._builder.tile_y(top_left) + dy);
				local before = GSIndustryList();
				local snapshot = this._snapshot_items(before);
				if (!GSIndustryType.BuildIndustry(industry_type, tile)) continue;

				local industry_id = this._find_new_item(snapshot, GSIndustryList());
				if (industry_id == null) {
					local tile_industry = GSIndustry.GetIndustryID(tile);
					if (GSIndustry.IsValidIndustry(tile_industry)) industry_id = tile_industry;
				}
				if (industry_id != null && GSIndustry.IsValidIndustry(industry_id)) {
					return { industry = industry_id, tile = tile };
				}
			}
		}

		return null;
	}

	function build_industry_near(industry_type, target_tile, radius, min_stations_around = 0)
	{
		local target_x = this._builder.tile_x(target_tile);
		local target_y = this._builder.tile_y(target_tile);

		for (local dist = 0; dist <= radius; dist++) {
			for (local dy = -dist; dy <= dist; dy++) {
				for (local dx = -dist; dx <= dist; dx++) {
					if (max(abs(dx), abs(dy)) != dist) continue;

					local x = target_x + dx;
					local y = target_y + dy;
					if (x < 0 || y < 0 || x >= GSMap.GetMapSizeX() || y >= GSMap.GetMapSizeY()) continue;

					local tile = this._builder.make_tile(x, y);
					local before = GSIndustryList();
					local snapshot = this._snapshot_items(before);
					if (!GSIndustryType.BuildIndustry(industry_type, tile)) continue;

					local industry_id = this._find_new_item(snapshot, GSIndustryList());
					if (industry_id == null) {
						local tile_industry = GSIndustry.GetIndustryID(tile);
						if (GSIndustry.IsValidIndustry(tile_industry)) industry_id = tile_industry;
					}

					if (industry_id != null && GSIndustry.IsValidIndustry(industry_id) &&
							GSIndustry.GetAmountOfStationsAround(industry_id) >= min_stations_around) {
						return { industry = industry_id, tile = tile };
					}
				}
			}
		}

		return null;
	}

	function find_buildable_industry_pair()
	{
		local industry_types = this._list_to_array(GSIndustryTypeList());

		foreach (raw_type in industry_types) {
			if (!GSIndustryType.IsValidIndustryType(raw_type)) continue;
			if (!GSIndustryType.CanBuildIndustry(raw_type)) continue;
			if (!GSIndustryType.IsRawIndustry(raw_type)) continue;

			local produced = GSIndustryType.GetProducedCargo(raw_type);
			foreach (cargo in this._list_to_array(produced)) {
				if (!GSCargo.IsValidCargo(cargo)) continue;
				if (!GSCargo.IsFreight(cargo)) continue;

				foreach (processing_type in industry_types) {
					if (!GSIndustryType.IsValidIndustryType(processing_type)) continue;
					if (!GSIndustryType.CanBuildIndustry(processing_type)) continue;
					if (!GSIndustryType.IsProcessingIndustry(processing_type)) continue;

					local accepted = GSIndustryType.GetAcceptedCargo(processing_type);
					if (!accepted.HasItem(cargo)) continue;

					return {
						raw_type = raw_type,
						processing_type = processing_type,
						cargo = cargo
					};
				}
			}
		}

		return null;
	}

	function station_accepts_cargo(station_id, cargo)
	{
		if (!GSStation.IsValidStation(station_id) || !GSCargo.IsValidCargo(cargo)) return false;
		local accepted = GSCargoList_StationAccepting(station_id);
		return accepted.HasItem(cargo);
	}
}

class MetroTrainLib {
	function find_rail_engine()
	{
		local engines = GSEngineList(GSVehicle.VT_RAIL);
		for (local engine = engines.Begin(); !engines.IsEnd(); engine = engines.Next()) {
			if (!GSEngine.IsBuildable(engine)) continue;
			if (GSEngine.IsWagon(engine)) continue;
			if (!GSEngine.CanRunOnRail(engine, GSRail.GetCurrentRailType())) continue;
			if (!GSEngine.HasPowerOnRail(engine, GSRail.GetCurrentRailType())) continue;
			return engine;
		}
		return null;
	}

	function find_rail_wagon()
	{
		local engines = GSEngineList(GSVehicle.VT_RAIL);
		for (local engine = engines.Begin(); !engines.IsEnd(); engine = engines.Next()) {
			if (!GSEngine.IsBuildable(engine)) continue;
			if (!GSEngine.IsWagon(engine)) continue;
			if (!GSEngine.CanRunOnRail(engine, GSRail.GetCurrentRailType())) continue;
			return engine;
		}
		return null;
	}

	function find_rail_wagon_for_cargo(depot_tile, cargo)
	{
		local engines = GSEngineList(GSVehicle.VT_RAIL);
		for (local engine = engines.Begin(); !engines.IsEnd(); engine = engines.Next()) {
			if (!GSEngine.IsBuildable(engine)) continue;
			if (!GSEngine.IsWagon(engine)) continue;
			if (!GSEngine.CanRunOnRail(engine, GSRail.GetCurrentRailType())) continue;

			local default_capacity = GSEngine.GetCapacity(engine);
			if (GSEngine.GetCargoType(engine) == cargo && default_capacity > 0) {
				return { engine = engine, refit = false, capacity = default_capacity };
			}

			if (!GSEngine.CanRefitCargo(engine, cargo)) continue;

			local refit_capacity = GSVehicle.GetBuildWithRefitCapacity(depot_tile, engine, cargo);
			if (refit_capacity > 0) {
				return { engine = engine, refit = true, capacity = refit_capacity };
			}
		}

		return null;
	}

	function build_train(depot_tile, engine)
	{
		return GSVehicle.BuildVehicle(depot_tile, engine);
	}

	function attach_wagon(train, depot_tile, wagon_engine)
	{
		local wagon = GSVehicle.BuildVehicle(depot_tile, wagon_engine);
		if (wagon == GSVehicle.VEHICLE_INVALID) {
			return { wagon = wagon, attached = false };
		}

		return {
			wagon = wagon,
			attached = GSVehicle.MoveWagon(wagon, 0, train, 0)
		};
	}

	function attach_cargo_wagon(train, depot_tile, cargo)
	{
		local spec = this.find_rail_wagon_for_cargo(depot_tile, cargo);
		if (spec == null) {
			return { wagon = GSVehicle.VEHICLE_INVALID, attached = false, spec = null };
		}

		local wagon = spec.refit ? GSVehicle.BuildVehicleWithRefit(depot_tile, spec.engine, cargo) : GSVehicle.BuildVehicle(depot_tile, spec.engine);
		if (wagon == GSVehicle.VEHICLE_INVALID) {
			return { wagon = wagon, attached = false, spec = spec };
		}

		return {
			wagon = wagon,
			attached = GSVehicle.MoveWagon(wagon, 0, train, 0),
			spec = spec
		};
	}

	function append_surface_order(vehicle, station_tile, order_flags = GSOrder.OF_NONE)
	{
		return GSOrder.AppendOrder(vehicle, station_tile, order_flags);
	}

	function append_underground_order(vehicle, station_id, order_flags = GSOrder.OF_NONE)
	{
		return GSMetro.AppendOrderByStationID(vehicle, station_id, order_flags);
	}

	function wait_for_departure(vehicle, max_ticks)
	{
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			if (!GSVehicle.IsInDepot(vehicle)) return ticks;
			GSController.Sleep(1);
		}
		return null;
	}

	function wait_for_movement(vehicle, max_ticks)
	{
		local initial_location = GSVehicle.GetLocation(vehicle);
		local max_speed = 0;

		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			local speed = GSVehicle.GetCurrentSpeed(vehicle);
			if (speed > max_speed) max_speed = speed;

			if (speed > 0 && GSVehicle.GetLocation(vehicle) != initial_location) {
				return { ticks = ticks, max_speed = max_speed };
			}
			GSController.Sleep(1);
		}

		return { ticks = null, max_speed = max_speed };
	}

	function wait_for_underground_entry(vehicle, max_ticks)
	{
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			if (GSMetro.IsVehicleUnderground(vehicle)) {
				return { ticks = ticks, tile = GSMetro.GetVehicleUndergroundTile(vehicle), depth = GSMetro.GetVehicleUndergroundDepth(vehicle) };
			}
			GSController.Sleep(1);
		}
		return null;
	}

	function wait_for_surface_tile(vehicle, target_tile, max_ticks)
	{
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			if (!GSMetro.IsVehicleUnderground(vehicle) && GSVehicle.GetLocation(vehicle) == target_tile) return ticks;
			GSController.Sleep(1);
		}
		return null;
	}

	function wait_for_portal_surface_or_entry(vehicle, target_tile, max_ticks)
	{
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			if (!GSMetro.IsVehicleUnderground(vehicle) && GSVehicle.GetLocation(vehicle) == target_tile) {
				return { ticks = ticks, seen_surface = true };
			}
			if (GSMetro.IsVehicleUnderground(vehicle)) {
				return { ticks = ticks, seen_surface = false };
			}
			GSController.Sleep(1);
		}
		return null;
	}

	function wait_for_underground_station(vehicle, station_id, max_ticks)
	{
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			if (GSMetro.IsVehicleUnderground(vehicle) && GSVehicle.GetState(vehicle) == GSVehicle.VS_AT_STATION) {
				local tile = GSMetro.GetVehicleUndergroundTile(vehicle);
				local depth = GSMetro.GetVehicleUndergroundDepth(vehicle);
				if (GSMetro.GetUndergroundStationID(tile, depth) == station_id) return ticks;
			}
			GSController.Sleep(1);
		}
		return null;
	}

	function wait_for_surface_station(vehicle, station_tile, max_ticks)
	{
		local station_id = GSStation.GetStationID(station_tile);
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			if (!GSMetro.IsVehicleUnderground(vehicle) && GSVehicle.GetState(vehicle) == GSVehicle.VS_AT_STATION) {
				local tile = GSVehicle.GetLocation(vehicle);
				if (GSStation.IsValidStation(station_id) && GSStation.GetStationID(tile) == station_id) return ticks;
			}
			GSController.Sleep(1);
		}
		return null;
	}

	function wait_for_vehicle_cargo(vehicle, cargo, max_ticks, min_amount = 1)
	{
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			local amount = GSVehicle.GetCargoLoad(vehicle, cargo);
			if (amount >= min_amount) {
				return { ticks = ticks, amount = amount };
			}
			GSController.Sleep(1);
		}
		return null;
	}

	function wait_for_vehicle_cargo_at_most(vehicle, cargo, max_ticks, max_amount = 0)
	{
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			local amount = GSVehicle.GetCargoLoad(vehicle, cargo);
			if (amount <= max_amount) {
				return { ticks = ticks, amount = amount };
			}
			GSController.Sleep(1);
		}
		return null;
	}

	function wait_for_station_waiting_cargo(station_id, cargo, max_ticks, min_amount = 1)
	{
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			local amount = GSStation.GetCargoWaiting(station_id, cargo);
			if (amount >= min_amount) {
				return { ticks = ticks, amount = amount };
			}
			GSController.Sleep(1);
		}
		return null;
	}

	function wait_for_industry_last_month_production(industry_id, cargo, max_ticks, min_amount = 1)
	{
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			local amount = GSIndustry.GetLastMonthProduction(industry_id, cargo);
			if (amount >= min_amount) {
				return { ticks = ticks, amount = amount };
			}
			GSController.Sleep(1);
		}
		return null;
	}

	function wait_for_town_pickup(company, cargo, town_id, max_ticks, min_amount = 1)
	{
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			local amount = GSCargoMonitor.GetTownPickupAmount(company, cargo, town_id, true);
			if (amount >= min_amount) {
				GSCargoMonitor.GetTownPickupAmount(company, cargo, town_id, false);
				return { ticks = ticks, amount = amount };
			}
			GSController.Sleep(1);
		}
		GSCargoMonitor.GetTownPickupAmount(company, cargo, town_id, false);
		return null;
	}

	function wait_for_town_delivery(company, cargo, town_id, max_ticks, min_amount = 1)
	{
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			local amount = GSCargoMonitor.GetTownDeliveryAmount(company, cargo, town_id, true);
			if (amount >= min_amount) {
				GSCargoMonitor.GetTownDeliveryAmount(company, cargo, town_id, false);
				return { ticks = ticks, amount = amount };
			}
			GSController.Sleep(1);
		}
		GSCargoMonitor.GetTownDeliveryAmount(company, cargo, town_id, false);
		return null;
	}

	function wait_for_industry_pickup(company, cargo, industry_id, max_ticks, min_amount = 1)
	{
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			local amount = GSCargoMonitor.GetIndustryPickupAmount(company, cargo, industry_id, true);
			if (amount >= min_amount) {
				GSCargoMonitor.GetIndustryPickupAmount(company, cargo, industry_id, false);
				return { ticks = ticks, amount = amount };
			}
			GSController.Sleep(1);
		}
		GSCargoMonitor.GetIndustryPickupAmount(company, cargo, industry_id, false);
		return null;
	}

	function wait_for_industry_delivery(company, cargo, industry_id, max_ticks, min_amount = 1)
	{
		for (local ticks = 0; ticks <= max_ticks; ticks++) {
			local amount = GSCargoMonitor.GetIndustryDeliveryAmount(company, cargo, industry_id, true);
			if (amount >= min_amount) {
				GSCargoMonitor.GetIndustryDeliveryAmount(company, cargo, industry_id, false);
				return { ticks = ticks, amount = amount };
			}
			GSController.Sleep(1);
		}
		GSCargoMonitor.GetIndustryDeliveryAmount(company, cargo, industry_id, false);
		return null;
	}
}

MetroTestLib.MetroCompanyLib <- MetroCompanyLib;
MetroTestLib.MetroBuildLib <- MetroBuildLib;
MetroTestLib.MetroLib <- MetroLib;
MetroTestLib.MetroWorldLib <- MetroWorldLib;
MetroTestLib.MetroTrainLib <- MetroTrainLib;
