class MetroErrorMessages extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_error_messages"; }
	function GetShortName()   { return "MERR"; }
	function GetDescription() { return "GS metro regression: refused metro commands report specific error codes."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroErrorMessages"; }
}

RegisterGS(MetroErrorMessages());
