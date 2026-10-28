class MetroDepotViaPortal extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_depot_via_portal"; }
	function GetShortName()   { return "MDVP"; }
	function GetDescription() { return "GS metro regression: a train sent to the depot while underground must surface through a portal and reach the depot."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-06-10"; }
	function CreateInstance() { return "MetroDepotViaPortal"; }
}

RegisterGS(MetroDepotViaPortal());
