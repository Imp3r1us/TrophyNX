#pragma once
// Rule paths as a trie, shared by the readers that build only what the rules can reach (live module, small
// heap): SGB (Hades), JSON. A value's place in the trie is a PathSet; childOf() gives its children's.
// "[*]" = every value of a container; a node where a path ends, or that a path continues from with an index /
// [Field=value] selector, is kept whole (its count and selectors need every child).
#include "byml.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

struct PathTrie {
    struct Node {
        std::map<std::string, int> kids;
        int star = -1;           // "[*]"
        bool whole = false;      // a path ends here, or continues with an index / [Field=value]: keep it all
    };
    std::vector<Node> nodes;
    int child(int cur, const std::string& a) {
        bool star = a == "[*]";
        int found = star ? nodes[cur].star : -1;
        if (!star) {
            auto it = nodes[cur].kids.find(a);
            if (it != nodes[cur].kids.end()) found = it->second;
        }
        if (found >= 0) return found;
        nodes.push_back(Node());
        int n = (int)nodes.size() - 1;
        if (star) nodes[cur].star = n;
        else nodes[cur].kids[a] = n;
        return n;
    }
    bool build(const std::vector<std::string>& paths) {
        nodes.assign(1, Node());
        for (const std::string& p : paths) {
            std::vector<std::string> atoms;
            if (!treeSplitPath(p, atoms) || atoms.empty()) return false;
            int cur = 0;
            for (const std::string& a : atoms) {
                if (a[0] == '[' && a != "[*]") break;   // index / selector: the node it applies to is kept whole
                cur = child(cur, a);
            }
            nodes[cur].whole = true;
        }
        return true;
    }
};

struct PathSet {   // where a value sits in the trie: the nodes it matches, or "keep everything"
    std::vector<int> at;
    bool full = false;
    bool dead() const { return !full && at.empty(); }
};

inline PathSet childOf(const PathTrie& t, const PathSet& p, const std::string& key) {
    PathSet c;
    if (p.full) { c.full = true; return c; }
    auto add = [&](int n) {
        if (t.nodes[n].whole) c.full = true;
        else if (std::find(c.at.begin(), c.at.end(), n) == c.at.end()) c.at.push_back(n);
    };
    for (int n : p.at) {
        const PathTrie::Node& node = t.nodes[n];
        auto it = node.kids.find(key);
        if (it != node.kids.end()) add(it->second);
        if (node.star >= 0) add(node.star);
        if (c.full) break;
    }
    if (c.full) c.at.clear();
    return c;
}

// The trie's root place (whole document kept if a path is just "[...]" at the top).
inline PathSet trieRoot(const PathTrie& t) {
    PathSet p;
    if (t.nodes[0].whole) p.full = true;
    else p.at.push_back(0);
    return p;
}
