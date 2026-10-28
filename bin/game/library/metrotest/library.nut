class MetroTestLibrary extends GSLibrary {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "Metro scenario helpers"; }
	function GetShortName()   { return "MTRT"; }
	function GetDescription() { return "Shared GS helpers for metro scenario tests."; }
	function GetVersion()     { return 1; }
	function GetDate()        { return "2026-03-19"; }
	function GetCategory()    { return "test"; }
	function CreateInstance() { return "MetroTestLib"; }
}

RegisterLibrary(MetroTestLibrary());
