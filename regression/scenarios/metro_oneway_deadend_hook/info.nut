class MetroOnewayDeadendHook extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_oneway_deadend_hook"; }
	function GetShortName()   { return "MODH"; }
	function GetDescription() { return "GS metro regression: trains at a terminus must not run on past a one-way signal into a dead end behind the platforms."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-28"; }
	function CreateInstance() { return "MetroOnewayDeadendHook"; }
}

RegisterGS(MetroOnewayDeadendHook());
