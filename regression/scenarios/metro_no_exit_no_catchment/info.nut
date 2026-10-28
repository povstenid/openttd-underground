class MetroNoExitNoCatchment extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_no_exit_no_catchment"; }
	function GetShortName()   { return "MNEC"; }
	function GetDescription() { return "GS metro regression: an underground station without exits keeps existing but has no catchment and gets no passengers."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-19"; }
	function CreateInstance() { return "MetroNoExitNoCatchment"; }
}

RegisterGS(MetroNoExitNoCatchment());
