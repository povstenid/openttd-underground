class MetroDragExitFlat extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_drag_exit_flat"; }
	function GetShortName()   { return "MDEF"; }
	function GetDescription() { return "GS metro regression: an underground drag ending on surface rail on flat ground leaves through a portal."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroDragExitFlat"; }
}

RegisterGS(MetroDragExitFlat());
