class MetroStationExits extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_station_exits"; }
	function GetShortName()   { return "MSEX"; }
	function GetDescription() { return "GS metro regression: pavilion and underpass station exits, their distance limit, catchment and removal."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-19"; }
	function CreateInstance() { return "MetroStationExits"; }
}

RegisterGS(MetroStationExits());
