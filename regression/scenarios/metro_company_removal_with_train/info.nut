class MetroCompanyRemovalWithTrain extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_company_removal_with_train"; }
	function GetShortName()   { return "MCRT"; }
	function GetDescription() { return "GS metro regression: merging or removing a company while its train is underground must transfer or clean up the metro without dangling references."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroCompanyRemovalWithTrain"; }
}

RegisterGS(MetroCompanyRemovalWithTrain());
