class MetroPortalAfterScissors extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_portal_after_scissors"; }
	function GetShortName()   { return "MPAS"; }
	function GetDescription() { return "GS metro regression: trains from depots on a surface section right behind a terminus crossover."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-28"; }
	function CreateInstance() { return "MetroPortalAfterScissors"; }
}

RegisterGS(MetroPortalAfterScissors());
