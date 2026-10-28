class MetroCityNetwork extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_city_network"; }
	function GetShortName()   { return "MCNW"; }
	function GetDescription() { return "GS metro demo: towns across the map linked by an underground high-speed ring, suburban lines and city metros."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-19"; }
	function CreateInstance() { return "MetroCityNetwork"; }
}

RegisterGS(MetroCityNetwork());
