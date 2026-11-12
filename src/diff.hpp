#pragma once
#include "evaluator.hpp"
#include "loader.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// "nero diff": what changes between two sets of emitted documents.
//   nero diff a.nero b.nero                        the documents of a against the documents of b, by position
//   nero diff f.nero --by PATH X Y                 inside one file: the documents whose PATH is X against those whose PATH is Y
//   --ignore PATH (repeatable) hides a field and everything under it. The --by field is hidden on its own.
// Output, one line per change:   ~ path: before -> after    + path: added value    - path: removed value
// Exit code: 0 identical, 1 different, 2 something went wrong.
// Lists of records that all have a unique 'name' are matched by that name (containers, env vars, ports),
// every other list by position. A secret is compared by its name, its value is never read.

struct Change {
    char kind = '~';          // '~' changed, '+' added, '-' removed
    std::string path;
    std::string before, after;
};

inline std::string diff_show(const Value& v) {
    if (v.kind == Value::Secret) return "secret(" + v.s + ")";
    return to_json(v, false);
}

inline const Value* diff_field(const Value& rec, const std::string& key) {
    for (size_t k = 0; k < rec.keys.size(); k++)
        if (rec.keys[k] == key) return &rec.items[k];
    return nullptr;
}

inline std::string diff_key(const std::string& path, const std::string& key) {
    bool plain = !key.empty();
    for (char c : key)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) plain = false;
    if (plain) return path.empty() ? key : path + "." + key;
    std::string quoted;
    json_string(key, quoted);
    return path + "[" + quoted + "]";
}

// a list of records that all carry a different string 'name'
inline bool diff_named(const Value& v) {
    if (v.kind != Value::List || v.items.empty()) return false;
    std::set<std::string> seen;
    for (const Value& item : v.items) {
        if (item.kind != Value::Record) return false;
        const Value* n = diff_field(item, "name");
        if (!n || n->kind != Value::Str || !seen.insert(n->s).second) return false;
    }
    return true;
}

inline bool diff_same_scalar(const Value& a, const Value& b) {
    if (a.kind != b.kind) return false;
    if (a.kind == Value::Int || a.kind == Value::Bool) return a.i == b.i;
    return a.s == b.s;     // Str, and the name of a Secret
}

inline void diff_into(const Value& a, const Value& b, const std::string& path, std::vector<Change>& out) {
    if (a.kind == Value::Record && b.kind == Value::Record) {
        for (size_t k = 0; k < a.keys.size(); k++) {
            const Value* other = diff_field(b, a.keys[k]);
            std::string p = diff_key(path, a.keys[k]);
            if (other) diff_into(a.items[k], *other, p, out);
            else out.push_back({'-', p, diff_show(a.items[k]), ""});
        }
        for (size_t k = 0; k < b.keys.size(); k++)
            if (!diff_field(a, b.keys[k])) out.push_back({'+', diff_key(path, b.keys[k]), "", diff_show(b.items[k])});
        return;
    }
    if (a.kind == Value::List && b.kind == Value::List) {
        if (diff_named(a) && diff_named(b)) {
            auto label = [&](const Value& item) { return path + "[name=" + diff_field(item, "name")->s + "]"; };
            for (const Value& item : a.items) {
                const Value* match = nullptr;
                for (const Value& cand : b.items)
                    if (diff_field(cand, "name")->s == diff_field(item, "name")->s) match = &cand;
                if (match) diff_into(item, *match, label(item), out);
                else out.push_back({'-', label(item), diff_show(item), ""});
            }
            for (const Value& item : b.items) {
                bool found = false;
                for (const Value& cand : a.items)
                    if (diff_field(cand, "name")->s == diff_field(item, "name")->s) found = true;
                if (!found) out.push_back({'+', label(item), "", diff_show(item)});
            }
            return;
        }
        size_t common = std::min(a.items.size(), b.items.size());
        for (size_t k = 0; k < common; k++) diff_into(a.items[k], b.items[k], path + "[" + std::to_string(k) + "]", out);
        for (size_t k = common; k < a.items.size(); k++)
            out.push_back({'-', path + "[" + std::to_string(k) + "]", diff_show(a.items[k]), ""});
        for (size_t k = common; k < b.items.size(); k++)
            out.push_back({'+', path + "[" + std::to_string(k) + "]", "", diff_show(b.items[k])});
        return;
    }
    if (!diff_same_scalar(a, b)) out.push_back({'~', path, diff_show(a), diff_show(b)});
}

inline bool diff_hidden(const std::string& path, const std::vector<std::string>& ignore) {
    for (const std::string& ig : ignore) {
        if (path == ig) return true;
        if (path.size() > ig.size() && path.compare(0, ig.size(), ig) == 0 && (path[ig.size()] == '.' || path[ig.size()] == '['))
            return true;
    }
    return false;
}

// what a document says about itself, for headers: its kind when it has one
inline std::string diff_label(const Value& doc, size_t index) {
    const Value* kind = doc.kind == Value::Record ? diff_field(doc, "kind") : nullptr;
    if (kind && kind->kind == Value::Str) return kind->s;
    return "document " + std::to_string(index + 1);
}

// the text of a scalar at a dotted path like metadata.name, or "" when it is not there
inline std::string diff_at(const Value& doc, const std::string& path, bool& found) {
    const Value* cur = &doc;
    size_t start = 0;
    while (true) {
        size_t dot = path.find('.', start);
        std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (cur->kind != Value::Record || !(cur = diff_field(*cur, key))) { found = false; return ""; }
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    found = true;
    if (cur->kind == Value::Str) return cur->s;
    if (cur->kind == Value::Int) return std::to_string(cur->i);
    if (cur->kind == Value::Bool) return cur->i ? "true" : "false";
    found = false;
    return "";
}

// everything a file emits, in order. print output is not part of a diff.
inline std::vector<Value> diff_collect(const std::string& file) {
    Program prog = Loader().load(file);
    std::ostringstream sink;
    Evaluator ev(prog, sink);
    std::vector<Value> docs;
    ev.collect(docs);
    ev.run();
    return docs;
}

// the documents of one file whose PATH equals value. When none match, the error lists what is there.
inline std::vector<const Value*> diff_select(const std::vector<Value>& docs, const std::string& path, const std::string& value) {
    std::vector<const Value*> hit;
    std::vector<std::string> seen;
    for (const Value& d : docs) {
        bool found = false;
        std::string text = diff_at(d, path, found);
        if (!found) continue;
        if (text == value) hit.push_back(&d);
        else if (std::find(seen.begin(), seen.end(), text) == seen.end()) seen.push_back(text);
    }
    if (hit.empty()) {
        std::string msg = "no document has " + path + " equal to \"" + value + "\"";
        if (!seen.empty()) {
            msg += " (found:";
            for (size_t k = 0; k < seen.size() && k < 10; k++) msg += (k ? ", " : " ") + seen[k];
            msg += ")";
        }
        throw std::runtime_error(msg);
    }
    return hit;
}

// one pair of documents: its changes, filtered
inline std::vector<Change> diff_pair(const Value& a, const Value& b, const std::vector<std::string>& ignore) {
    std::vector<Change> all, keep;
    diff_into(a, b, "", all);
    for (Change& c : all)
        if (!diff_hidden(c.path, ignore)) keep.push_back(std::move(c));
    return keep;
}

inline void diff_print(const std::vector<Change>& changes, std::ostream& os) {
    for (const Change& c : changes) {
        std::string p = c.path.empty() ? "(value)" : c.path;
        if (c.kind == '~') os << "~ " << p << ": " << c.before << " -> " << c.after << "\n";
        else if (c.kind == '+') os << "+ " << p << ": " << c.after << "\n";
        else os << "- " << p << ": " << c.before << "\n";
    }
}

inline int diff_main(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    std::vector<std::string> pos, ignore;
    std::string by;
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--by" || args[i] == "--ignore") {
            if (i + 1 >= args.size()) { err << "error: " << args[i] << " needs a value\n"; return 2; }
            if (args[i] == "--by") by = args[++i];
            else ignore.push_back(args[++i]);
        } else if (args[i].compare(0, 2, "--") == 0) {
            err << "error: unknown option " << args[i] << "\n";
            return 2;
        } else {
            pos.push_back(args[i]);
        }
    }
    bool one_file = !by.empty();
    if ((!one_file && pos.size() != 2) || (one_file && pos.size() != 3)) {
        err << "usage: nero diff a.nero b.nero [--ignore PATH]\n"
               "       nero diff file.nero --by PATH X Y [--ignore PATH]\n";
        return 2;
    }
    if (one_file) ignore.push_back(by);

    try {
        std::vector<const Value*> left, right;
        std::vector<Value> docs_a, docs_b;
        std::string head_suffix;
        if (one_file) {
            docs_a = diff_collect(pos[0]);
            left = diff_select(docs_a, by, pos[1]);
            right = diff_select(docs_a, by, pos[2]);
            head_suffix = " (" + pos[1] + " -> " + pos[2] + ")";
        } else {
            docs_a = diff_collect(pos[0]);
            docs_b = diff_collect(pos[1]);
            for (const Value& d : docs_a) left.push_back(&d);
            for (const Value& d : docs_b) right.push_back(&d);
        }

        size_t total = 0;
        bool headers = left.size() > 1 || right.size() > 1 || one_file;
        size_t common = std::min(left.size(), right.size());
        for (size_t k = 0; k < common; k++) {
            std::vector<Change> changes = diff_pair(*left[k], *right[k], ignore);
            if (changes.empty()) continue;
            if (headers) out << "@ " << diff_label(*left[k], k) << head_suffix << "\n";
            diff_print(changes, out);
            total += changes.size();
        }
        for (size_t k = common; k < left.size(); k++, total++) out << "- " << diff_label(*left[k], k) << " (only on the left)\n";
        for (size_t k = common; k < right.size(); k++, total++) out << "+ " << diff_label(*right[k], k) << " (only on the right)\n";

        if (total == 0) {
            out << "no differences\n";
            return 0;
        }
        out << total << (total == 1 ? " change\n" : " changes\n");
        return 1;
    } catch (const std::exception& e) {
        err << "error: " << e.what() << "\n";
        return 2;
    }
}
