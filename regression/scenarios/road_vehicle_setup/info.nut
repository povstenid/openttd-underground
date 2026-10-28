class RoadVehicleSetup extends AIInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "road_vehicle_setup"; }
	function GetShortName()   { return "RVST"; }
	function GetDescription() { return "Scenario smoke test for building a road line, depot, vehicle and orders."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-03-18"; }
	function CreateInstance() { return "RoadVehicleSetup"; }
	function UseAsRandomAI()  { return false; }
}

RegisterAI(RoadVehicleSetup());
