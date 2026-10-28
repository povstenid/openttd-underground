class MetroSignalTypesSave extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_signal_types_save"; }
	function GetShortName()   { return "MSTS"; }
	function GetDescription() { return "GS metro regression: every underground signal type and variant, written to a savegame."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroSignalTypesSave"; }
}
RegisterGS(MetroSignalTypesSave());
