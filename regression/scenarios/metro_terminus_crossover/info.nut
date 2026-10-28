class MetroTerminusCrossover extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_terminus_crossover"; }
	function GetShortName()   { return "MTXO"; }
	function GetDescription() { return "GS metro regression: multi-car trains reversing at underground termini with a scissors crossover."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-19"; }
	function CreateInstance() { return "MetroTerminusCrossover"; }
}

RegisterGS(MetroTerminusCrossover());
