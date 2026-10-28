class MetroStationStopPersists extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_station_stop_persists"; }
	function GetShortName()   { return "MSSP"; }
	function GetDescription() { return "GS metro regression: a train stopped by the player at an underground station stays stopped for 30 days and resumes on Start."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroStationStopPersists"; }
}

RegisterGS(MetroStationStopPersists());
