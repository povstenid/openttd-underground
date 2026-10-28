class MetroSoilPrices extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_soil_prices"; }
	function GetShortName()   { return "MSOP"; }
	function GetDescription() { return "GS metro regression: soil by depth below the surface, underground prices and refunds from the underground settings, depth limits of tunnels and stations."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-21"; }
	function CreateInstance() { return "MetroSoilPrices"; }
}
RegisterGS(MetroSoilPrices());
