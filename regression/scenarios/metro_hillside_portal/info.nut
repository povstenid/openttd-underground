class MetroHillsidePortal extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_hillside_portal"; }
	function GetShortName()   { return "MHSP"; }
	function GetDescription() { return "GS metro regression: a hillside portal sits at the foot of its incline."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroHillsidePortal"; }
}

RegisterGS(MetroHillsidePortal());
