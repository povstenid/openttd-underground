class MetroExitPavilionOnHouse extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_exit_pavilion_on_house"; }
	function GetShortName()   { return "MEPH"; }
	function GetDescription() { return "GS metro regression: a pavilion exit demolishes a town house on its tile, but not road or another company's rail."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-21"; }
	function CreateInstance() { return "MetroExitPavilionOnHouse"; }
}
RegisterGS(MetroExitPavilionOnHouse());
