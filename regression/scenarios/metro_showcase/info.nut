class MetroShowcase extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_showcase"; }
	function GetShortName()   { return "MSHW"; }
	function GetDescription() { return "GS metro demo: a city line with three underground stations on two depths, ramps, exits and towns above the tunnel."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-19"; }
	function CreateInstance() { return "MetroShowcase"; }
}

RegisterGS(MetroShowcase());
