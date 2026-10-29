class MetroStationRemoveWithExits extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_station_remove_with_exits"; }
	function GetShortName()   { return "MSRX"; }
	function GetDescription() { return "GS metro regression: removing the last platform of an underground station removes its exits, and the station goes away."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-30"; }
	function CreateInstance() { return "MetroStationRemoveWithExits"; }
}

RegisterGS(MetroStationRemoveWithExits());
