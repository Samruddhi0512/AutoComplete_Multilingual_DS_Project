// server.cpp
// Final combined backend with UTF-8 Trie + scoring + recent boost + language detection + /stats endpoint
// Author: (your project)
// Note: Requires httplib.h and nlohmann/json.hpp in include path.

#include <iostream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cstdint>
#include <memory>
#include "httplib.h"
#include "json.hpp"

using json = nlohmann::json;
using namespace std;

// -------------------------
// Configuration
// -------------------------
static const int RECENT_MAX = 20;
static const int TRIE_CANDIDATE_LIMIT = 200; // how many candidates to pull from trie before scoring
static const int RETURN_TOP_K = 10;         // final returned suggestions

// -------------------------
// Globals
// -------------------------
vector<string> words;                    // optional full list
unordered_map<string,int> freq;          // frequency map (learning)
vector<string> recent;                   // last selected words (FIFO)

// -------------------------
// Language enum (script categories)
// NOTE: short names to avoid conflicts with Windows macros
// -------------------------
enum Lang {
    LANG_UNKNOWN,
    LANG_DEV,   // Devanagari (Hindi/Marathi)
    LANG_GUR,   // Gurmukhi (Punjabi)
    LANG_BEN,   // Bengali
    LANG_KAN    // Kannada
};

// -------------------------
// Utility: trim
// -------------------------
string trim(const string &s) {
    size_t start = s.find_first_not_of(" \n\r\t");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \n\r\t");
    return s.substr(start, end - start + 1);
}

// -------------------------
// UTF-8 helpers
// -------------------------

// Count number of Unicode characters (UTF-8) in a string
int utf8_length(const string &s) {
    int len = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++len;
    return len;
}

// Extract first codepoint value (used for detectLanguage)
uint32_t getCodepoint(const string &s) {
    if (s.empty()) return 0;
    const unsigned char *bytes = reinterpret_cast<const unsigned char*>(s.c_str());
    size_t blen = s.size();
    if (bytes[0] < 0x80) return bytes[0];
    if (blen >= 2 && (bytes[0] & 0xE0) == 0xC0) {
        return ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
    }
    if (blen >= 3 && (bytes[0] & 0xF0) == 0xE0) {
        return ((bytes[0] & 0x0F) << 12) |
               ((bytes[1] & 0x3F) << 6) |
               (bytes[2] & 0x3F);
    }
    if (blen >= 4 && (bytes[0] & 0xF8) == 0xF0) {
        return ((bytes[0] & 0x07) << 18) |
               ((bytes[1] & 0x3F) << 12) |
               ((bytes[2] & 0x3F) << 6) |
               (bytes[3] & 0x3F);
    }
    return 0;
}

// Next codepoint and advance index i (used by Trie)
uint32_t nextCodepoint(const string &s, size_t &i) {
    const unsigned char *bytes = reinterpret_cast<const unsigned char*>(s.c_str());
    uint32_t cp = 0;
    if (i >= s.size()) return 0;
    unsigned char b0 = bytes[i];
    if (b0 < 0x80) { cp = b0; i += 1; return cp; }
    if ((b0 & 0xE0) == 0xC0) {
        if (i+1 >= s.size()) return 0;
        cp = ((bytes[i] & 0x1F) << 6) | (bytes[i+1] & 0x3F);
        i += 2; return cp;
    }
    if ((b0 & 0xF0) == 0xE0) {
        if (i+2 >= s.size()) return 0;
        cp = ((bytes[i] & 0x0F) << 12) |
             ((bytes[i+1] & 0x3F) << 6) |
             (bytes[i+2] & 0x3F);
        i += 3; return cp;
    }
    if ((b0 & 0xF8) == 0xF0) {
        if (i+3 >= s.size()) return 0;
        cp = ((bytes[i] & 0x07) << 18) |
             ((bytes[i+1] & 0x3F) << 12) |
             ((bytes[i+2] & 0x3F) << 6) |
             (bytes[i+3] & 0x3F);
        i += 4; return cp;
    }
    // fallback
    i += 1;
    return 0;
}

// Convert a codepoint to UTF-8 string
string codepointToUtf8(uint32_t cp) {
    string out;
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

// -------------------------
// Language detection & filtering
// -------------------------
Lang detectLanguage(const string &prefix) {
    if (prefix.empty()) return LANG_UNKNOWN;
    uint32_t cp = getCodepoint(prefix);
    if (cp >= 0x0900 && cp <= 0x097F) return LANG_DEV;
    if (cp >= 0x0A00 && cp <= 0x0A7F) return LANG_GUR;
    if (cp >= 0x0980 && cp <= 0x09FF) return LANG_BEN;
    if (cp >= 0x0C80 && cp <= 0x0CFF) return LANG_KAN;
    return LANG_UNKNOWN;
}

bool wordMatchesLanguage(const string &word, Lang lang) {
    if (word.empty()) return false;
    if (lang == LANG_UNKNOWN) return true;
    uint32_t cp = getCodepoint(word);
    switch (lang) {
        case LANG_DEV: return (cp >= 0x0900 && cp <= 0x097F);
        case LANG_GUR: return (cp >= 0x0A00 && cp <= 0x0A7F);
        case LANG_BEN: return (cp >= 0x0980 && cp <= 0x09FF);
        case LANG_KAN: return (cp >= 0x0C80 && cp <= 0x0CFF);
        default: return true;
    }
}

// -------------------------
// Trie (UTF-8) Implementation
// -------------------------
struct TrieNode {
    bool isEnd = false;
    unordered_map<uint32_t, TrieNode*> children;
    TrieNode() = default;
};

TrieNode* trieRoot = new TrieNode();

void insertTrie(const string &word) {
    TrieNode* node = trieRoot;
    size_t i = 0;
    while (i < word.size()) {
        uint32_t cp = nextCodepoint(word, i);
        if (cp == 0) break;
        auto it = node->children.find(cp);
        if (it == node->children.end()) {
            TrieNode* nn = new TrieNode();
            node->children[cp] = nn;
            node = nn;
        } else node = it->second;
    }
    node->isEnd = true;
}

void deleteTrieRecursive(TrieNode* node) {
    if (!node) return;
    for (auto &p : node->children) deleteTrieRecursive(p.second);
    delete node;
}

// Depth-first collect up to k words, building current string as we go
void dfsCollect(TrieNode* node, const string &current, vector<string> &results, int k) {
    if (!node) return;
    if ((int)results.size() >= k) return;
    if (node->isEnd) results.push_back(current);
    // iterate children in arbitrary order (unordered_map) — good enough
    for (auto &p : node->children) {
        uint32_t cp = p.first;
        TrieNode* next = p.second;
        string out = codepointToUtf8(cp);
        dfsCollect(next, current + out, results, k);
        if ((int)results.size() >= k) return;
    }
}

// Return up to k candidate words starting with prefix (prefix itself must be UTF-8)
vector<string> trieGetPrefix(const string &prefix, int k) {
    TrieNode* node = trieRoot;
    size_t i = 0;
    // Traverse trie along prefix codepoints
    while (i < prefix.size()) {
        uint32_t cp = nextCodepoint(prefix, i);
        if (cp == 0) return {};
        auto it = node->children.find(cp);
        if (it == node->children.end()) return {};
        node = it->second;
    }
    vector<string> results;
    dfsCollect(node, prefix, results, k);
    return results;
}

// -------------------------
// Load/save recent.json & freq.json
// -------------------------
void loadRecent() {
    ifstream f("recent.json");
    if (!f.good()) { /* start empty */ return; }
    json j;
    try { f >> j; } catch(...) { return; }
    recent.clear();
    for (auto &w : j) if (w.is_string()) recent.push_back(w.get<string>());
}

void saveRecent() {
    json j = json::array();
    for (auto &w : recent) j.push_back(w);
    ofstream f("recent.json");
    f << j.dump(4);
}

void loadFrequency() {
    ifstream f("freq.json");
    if (!f.good()) { /* start fresh */ return; }
    json j;
    try { f >> j; } catch(...) { return; }
    for (auto &p : j.items()) freq[p.key()] = p.value();
}

void saveFrequency() {
    json j;
    for (auto &p : freq) j[p.first] = p.second;
    ofstream f("freq.json");
    f << j.dump(4);
}

// -------------------------
// Load words.txt and build trie
// Supports comments (# ...) and section headers (--- ...)
// -------------------------
void loadWordsAndBuildTrie() {
    ifstream file("words.txt");
    if (!file.good()) {
        cerr << "Error: words.txt not found\n";
        return;
    }
    string line;
    while (getline(file, line)) {
        string w = trim(line);
        if (w.empty()) continue;
        if (w[0] == '#') continue;
        if (w.rfind("---", 0) == 0) continue;
        words.push_back(w);
        if (freq.find(w) == freq.end()) freq[w] = 0;
        insertTrie(w); // add to trie
    }
    sort(words.begin(), words.end());
    cout << "Loaded " << words.size() << " words and built Trie.\n";
}

// -------------------------
// Suggestion logic using Trie, language filtering, scoring and recent boost
// -------------------------
vector<string> getSuggestions(const string &prefix) {
    vector<string> finalRes;

    if (prefix.empty()) return finalRes;

    // detect language/script
    Lang lang = detectLanguage(prefix);

    // get candidates quickly from trie
    vector<string> candidates = trieGetPrefix(prefix, TRIE_CANDIDATE_LIMIT);

    struct Item { string w; double score; };
    vector<Item> scored;

    unordered_set<string> seen; // avoid duplicates

    for (auto &w : candidates) {
        if (seen.count(w)) continue;
        seen.insert(w);

        // script filter
        if (!wordMatchesLanguage(w, lang)) continue;

        int len = utf8_length(w);
        int f = 0;
        auto itf = freq.find(w);
        if (itf != freq.end()) f = itf->second;

        double score = (f * 2.0) + (1.0 / max(1, len));

        // recent boost
        if (find(recent.begin(), recent.end(), w) != recent.end()) score += 5.0;

        scored.push_back({w, score});
    }

    // In rare cases trie may not find (e.g., if prefix in unknown script), fallback to linear scan (small)
    if (scored.empty()) {
        // fallback: search words vector (should be rare)
        for (auto &w : words) {
            if (w.rfind(prefix, 0) != 0) continue;
            if (!wordMatchesLanguage(w, lang)) continue;
            int len = utf8_length(w);
            int f = (freq.count(w) ? freq[w] : 0);
            double score = (f * 2.0) + (1.0 / max(1, len));
            if (find(recent.begin(), recent.end(), w) != recent.end()) score += 5.0;
            scored.push_back({w, score});
        }
    }

    // sort by score desc, fallback alphabetical
    sort(scored.begin(), scored.end(), [](const Item &a, const Item &b) {
        if (a.score != b.score) return a.score > b.score;
        return a.w < b.w;
    });

    // prepare final result top-K
    int limit = min((int)scored.size(), RETURN_TOP_K);
    for (int i = 0; i < limit; ++i) finalRes.push_back(scored[i].w);

    return finalRes;
}

// -------------------------
// Main server: endpoints
// -------------------------
int main() {
    // load persisted data
    loadFrequency();
    loadRecent();
    loadWordsAndBuildTrie();

    httplib::Server server;

    cout << "🚀 Server running at http://127.0.0.1:9090\n";

    // GET /suggest?prefix=...
    server.Get("/suggest", [&](const httplib::Request &req, httplib::Response &res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Headers", "*");
        res.set_header("Access-Control-Allow-Methods", "*");

        if (!req.has_param("prefix")) {
            res.set_content("[]", "application/json");
            return;
        }
        string prefix = req.get_param_value("prefix");
        prefix = trim(prefix);
        if (prefix.empty()) {
            res.set_content("[]", "application/json");
            return;
        }
        auto list = getSuggestions(prefix);
        json j = json::array();
        for (auto &w : list) j.push_back(w);
        res.set_content(j.dump(), "application/json");
    });

    // GET /stats (words + freq) for dashboard
    server.Get("/stats", [&](const httplib::Request &req, httplib::Response &res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Headers", "*");
        res.set_header("Access-Control-Allow-Methods", "*");

        json j = json::array();
        for (auto &p : freq) {
            json item;
            item["word"] = p.first;
            item["freq"] = p.second;
            j.push_back(item);
        }
        res.set_content(j.dump(4), "application/json");
    });

    // POST /select { "word": "..." }
    server.Post("/select", [&](const httplib::Request &req, httplib::Response &res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Headers", "*");
        res.set_header("Access-Control-Allow-Methods", "*");

        if (req.body.empty()) {
            res.set_content("{\"ok\":false}", "application/json");
            return;
        }
        try {
            auto j = json::parse(req.body);
            if (!j.contains("word")) {
                res.set_content("{\"ok\":false}", "application/json");
                return;
            }
            string word = j["word"];

            // frequency update
            freq[word] += 1;
            saveFrequency();

            // update recent (FIFO keep last RECENT_MAX)
            recent.push_back(word);
            if ((int)recent.size() > RECENT_MAX) recent.erase(recent.begin());
            saveRecent();

            json out = { {"ok", true}, {"message", "Learning + recent saved"} };
            res.set_content(out.dump(), "application/json");
        } catch (...) {
            res.set_content("{\"ok\":false}", "application/json");
        }
    });

    // POST /addword  --> Add new word dynamically
    server.Post("/addword", [&](const httplib::Request &req, httplib::Response &res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Headers", "*");
        res.set_header("Access-Control-Allow-Methods", "*");

        if (req.body.empty()) {
            res.set_content("{\"status\":\"error\", \"msg\":\"empty body\"}", "application/json");
            return;
        }

        try {
            auto j = json::parse(req.body);
            if (!j.contains("word")) {
                res.set_content("{\"status\":\"error\", \"msg\":\"missing word field\"}", "application/json");
                return;
            }

            string word = trim(j["word"].get<string>());

            if (word.empty()) {
                res.set_content("{\"status\":\"error\", \"msg\":\"empty word\"}", "application/json");
                return;
            }

            // 1. Check if exists
            if (freq.find(word) != freq.end()) {
                res.set_content("{\"status\":\"exists\", \"msg\":\"word already present\"}", "application/json");
                return;
            }

            // 2. Append to words.txt
            ofstream wf("words.txt", ios::app);
            wf << word << "\n";
            wf.close();

            // 3. Add to freq.json
            freq[word] = 0;
            saveFrequency();

            // 4. Insert into trie + RAM list
            insertTrie(word);
            words.push_back(word);
            sort(words.begin(), words.end());

            // 5. Success
            json out = {
                {"status","ok"},
                {"message","Word added successfully"}
            };
            res.set_content(out.dump(), "application/json");
        }
        catch (...) {
            res.set_content("{\"status\":\"error\", \"msg\":\"Invalid JSON\"}", "application/json");
        }
    });

    server.listen("127.0.0.1", 9090);

    // cleanup trie memory on exit (optional in many OSes)
    // deleteTrieRecursive(trieRoot);

    return 0;
}
