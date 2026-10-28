class MetroSignalTypesReload extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_signal_types_reload"; }
	function GetShortName()   { return "MSTR"; }
	function GetDescription() { return "GS metro regression: underground signal types and variants survive save and load."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroSignalTypesReload"; }
}
RegisterGS(MetroSignalTypesReload());
