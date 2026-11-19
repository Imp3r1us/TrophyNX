using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.Serialization.Formatters.Binary;

[Serializable] public enum Difficulty { Easy, Normal, Hard = 7 }
[Serializable] public class Stats { public int kills; public float distance; public double money; public long bigScore; }
[Serializable] public class Node { public string name; public Node next; }
[Serializable] public class SaveData {
    public int level;
    public bool beatBoss;
    public string playerName;
    public string sameName;
    public Difficulty difficulty;
    public Stats stats = new Stats();
    public Stats statsAgain;
    public List<int> collected = new List<int>();
    public List<string> emptyList = new List<string>();
    public Dictionary<string, bool> flags = new Dictionary<string, bool>();
    public Dictionary<int, Stats> perLevel = new Dictionary<int, Stats>();
    public HashSet<string> unlocked = new HashSet<string>();
    public int[] scores;
    public string[] names;
    public object[] mixed;
    public int[,] grid;
    public DateTime saved;
    public TimeSpan played;
    public decimal gold;
    public char grade;
    public byte b; public sbyte sb; public short s16; public ushort u16; public uint u32; public ulong u64;
    public Node loop;
    public int Coins { get; set; }
    public List<Stats> history = new List<Stats>();
}
class P {
    static void Main(string[] a) {
        var bf = new BinaryFormatter();
        for (int v = 0; v < 2; v++) {
            var d = new SaveData();
            d.level = v == 0 ? 1 : 12; d.beatBoss = v == 1; d.playerName = "Player01"; d.sameName = d.playerName;
            d.difficulty = v == 0 ? Difficulty.Normal : Difficulty.Hard;
            d.stats.kills = v * 250; d.stats.distance = 1.5f + v; d.stats.money = 1e10 + v; d.stats.bigScore = 9007199254740993L;
            d.statsAgain = d.stats;
            d.collected.AddRange(v == 0 ? new[] { 3 } : new[] { 3, 5, 8 });
            d.flags["MetNPC"] = true; d.flags["Ending_A"] = v == 1;
            d.perLevel[1] = new Stats { kills = 4 }; if (v == 1) d.perLevel[2] = new Stats { kills = 9 };
            d.unlocked.Add("Sword"); if (v == 1) d.unlocked.Add("Bow");
            d.scores = new[] { 10, 20, 30 }; d.names = new[] { "a", null, "c", "a" };
            d.mixed = new object[300]; d.mixed[0] = 42; d.mixed[299] = "end"; d.mixed[5] = d.stats;
            d.grid = new int[2, 3] { { 1, 2, 3 }, { 4, 5, 6 } };
            d.saved = new DateTime(2026, 11, 19, 10, 0, 0, DateTimeKind.Utc); d.played = TimeSpan.FromHours(3 + v);
            d.gold = 123.45m; d.grade = 'é';
            d.b = 200; d.sb = -5; d.s16 = -300; d.u16 = 65000; d.u32 = 4000000000; d.u64 = 12345678901234UL;
            d.loop = new Node { name = "A" }; d.loop.next = new Node { name = "B", next = d.loop };
            d.Coins = 77 * v;
            d.history.Add(new Stats { kills = 1 }); d.history.Add(null);
            using var f = File.Create(a[0] + "/save" + v + ".dat");
            bf.Serialize(f, d);
        }
        using (var f = File.Create(a[0] + "/string.dat")) bf.Serialize(f, "base64-payload==");
        using (var f = File.Create(a[0] + "/list.dat")) bf.Serialize(f, new List<string> { "x", "y" });
    }
}
