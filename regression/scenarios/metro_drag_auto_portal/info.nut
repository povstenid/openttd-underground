class MetroDragAutoPortal extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_drag_auto_portal"; }
	function GetShortName()   { return "MDAP"; }
	function GetDescription() { return "GS metro regression: dragging underground rail out of a hill builds the portal and surface rail."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroDragAutoPortal"; }
}

RegisterGS(MetroDragAutoPortal());
