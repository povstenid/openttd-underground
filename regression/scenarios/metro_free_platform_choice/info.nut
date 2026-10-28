class MetroFreePlatformChoice extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_free_platform_choice"; }
	function GetShortName()   { return "MFPC"; }
	function GetDescription() { return "GS metro regression: with one platform held by another train, a train takes the free platform beside it."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-21"; }
	function CreateInstance() { return "MetroFreePlatformChoice"; }
}
RegisterGS(MetroFreePlatformChoice());
