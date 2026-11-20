// TrophyNX - DELTARUNE (Switch) trophy patch for UndertaleModTool (GUI: Scripts > Run other script; CLI: see README.md).
//
// The Switch version ships the PS4/PS5 trophy code (obj_event_manager) but only turns it on for PlayStation, and
// unlocks only ever go to PSN. This script, run on chapter1_switch .. chapter4_switch game.win of YOUR OWN dump:
//   1. obj_initializer2 (Create): calls enable_trophies() on every console, not only PS4/PS5;
//   2. obj_event_manager (Create): queue_trophy() and unlock_trophy() also set global.flag[2400 + trophy id] = 1.
//      Queued trophies are only unlocked at the end of the room, but the chapter-end ones are queued right before
//      the completion save, so the flag is set at queue time too. global.flag[0..2499] is part of every save
//      (filechN_S in deltarune.sav), which is where TrophyNX reads it.
// Flags 2400..2429 are not used by any chapter (1-5; checked on every global.flag / scr_flag_get / scr_flag_set /
// "...flag = N" use); chapters 2-5 save flags 0..2499, chapter 1 saves 0..9998.
// Chapter 5 and the chapter-select launcher have no trophy code: the script leaves them untouched.
// Running it twice on the same file does nothing the second time.
using System;
using System.Linq;
using System.Text.RegularExpressions;

EnsureDataLoaded();

const int FLAG_BASE = 2400;
const string MARK = "TrophyNX";

UndertaleCode FindCode(string name) => Data.Code.FirstOrDefault(c => c.Name?.Content == name);

GlobalDecompileContext ctx = new(Data);
var settings = Data.ToolInfo.DecompilerSettings;
string Decompile(UndertaleCode c) => new Underanalyzer.Decompiler.DecompileContext(ctx, c, settings).DecompileToString();

UndertaleCode mgr = FindCode("gml_Object_obj_event_manager_Create_0");
UndertaleCode init = FindCode("gml_Object_obj_initializer2_Create_0");
if (mgr is null || init is null)
{
    Console.WriteLine("TrophyNX: no obj_event_manager / obj_initializer2 in this file (launcher?): nothing to patch.");
    return;
}
string mgrText = Decompile(mgr);
string initText = Decompile(init);
if (!mgrText.Contains("unlock_trophy = function"))
{
    Console.WriteLine("TrophyNX: this chapter has no trophy code (chapter 5?): nothing to patch.");
    return;
}
if (Regex.IsMatch(mgrText, @"global\.flag\[" + FLAG_BASE + @" \+ arg0\] = 1"))   // comments don't survive compiling
{
    Console.WriteLine("TrophyNX: already patched.");
    return;
}

// Inserts `line` as the first statement of `name = function(arg0) { ... }`.
string AddToFunction(string text, string name, string line)
{
    var re = new Regex(@"(\b" + name + @" = function\(arg0\)\s*\{)");
    if (re.Matches(text).Count != 1)
        throw new ScriptException("TrophyNX: '" + name + "' not found exactly once in obj_event_manager; unknown game version.");
    return re.Replace(text, "$1\n    " + line);
}
string setFlag = "global.flag[" + FLAG_BASE + " + arg0] = 1; // " + MARK + ": trophy unlocks go to the save";
string newMgr = AddToFunction(mgrText, "queue_trophy", setFlag);
newMgr = AddToFunction(newMgr, "unlock_trophy", setFlag);

// if (os_type == os_ps4 || os_type == os_ps5) { with (event_manager) { enable_trophies(); } }
//   -> with (event_manager) { enable_trophies(); }
var enableRe = new Regex(@"if \(os_type == os_ps4 \|\| os_type == os_ps5\)\s*\{\s*(with \(event_manager\)\s*\{\s*enable_trophies\(\);\s*\})\s*\}");
if (enableRe.Matches(initText).Count != 1)
    throw new ScriptException("TrophyNX: the PS4/PS5-only enable_trophies() call was not found in obj_initializer2; unknown game version.");
string newInit = enableRe.Replace(initText, "// " + MARK + ": trophies on every console\n    $1");

UndertaleModLib.Compiler.CodeImportGroup group = new(Data);
group.QueueReplace(mgr, newMgr);
group.QueueReplace(init, newInit);
group.Import();

// Check the result by decompiling it again.
string checkMgr = Decompile(mgr), checkInit = Decompile(init);
int flagWrites = Regex.Matches(checkMgr, @"global\.flag\[" + FLAG_BASE + @" \+ arg0\] = 1").Count;
bool psOnly = Regex.IsMatch(checkInit, @"os_ps5\)\s*\{\s*with \(event_manager\)\s*\{\s*enable_trophies");
if (flagWrites != 2 || psOnly || !checkInit.Contains("enable_trophies()"))
    throw new ScriptException("TrophyNX: patch did not apply cleanly (flag writes: " + flagWrites + ").");
Console.WriteLine("TrophyNX: patched - trophies enabled, unlocks saved to global.flag[" + FLAG_BASE + ".." + (FLAG_BASE + 29) + "].");
