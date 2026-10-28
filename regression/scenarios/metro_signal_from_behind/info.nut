class MetroSignalFromBehind extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_signal_from_behind"; }
	function GetShortName()   { return "MSFB"; }
	function GetDescription() { return "GS metro regression: one-way underground signals cannot be passed from behind."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroSignalFromBehind"; }
}

RegisterGS(MetroSignalFromBehind());
