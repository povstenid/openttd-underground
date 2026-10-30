class MetroStationJoinSurface extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_station_join_surface"; }
	function GetShortName()   { return "MSJS"; }
	function GetDescription() { return "GS metro regression: underground platforms and surface stations (airport, rail station) join each other."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-30"; }
	function CreateInstance() { return "MetroStationJoinSurface"; }
}

RegisterGS(MetroStationJoinSurface());
