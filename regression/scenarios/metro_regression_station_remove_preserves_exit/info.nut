class MetroRegressionStationRemovePreservesExit extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_regression_station_remove_preserves_exit"; }
	function GetShortName()   { return "MRSE"; }
	function GetDescription() { return "GS metro regression: removing an underground station must preserve the remaining surface exit complex."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-03-19"; }
	function CreateInstance() { return "MetroRegressionStationRemovePreservesExit"; }
}

RegisterGS(MetroRegressionStationRemovePreservesExit());
