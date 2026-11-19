#pragma once
#include "evaluator.hpp"
#include "loader.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// "nero diff": what changes between two sets of emitted documents.
//   nero diff a.nero b.nero                        the documents of a against the documents of b, by position
//   nero diff a.nero b.nero --match metadata.name  pair documents by a unique scalar identity
//   nero diff a.nero b.nero --match-k8s           pair Kubernetes resources by API identity
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
    std::string before_json, after_json;
    std::string document;
    std::string identity;
};

inline std::string diff_show(const Value& v) {
    if (v.kind == Value::Secret)
        return v.secret_key.empty() ? "secret(" + v.s + ")" : "secret(" + v.s + ", " + v.secret_key + ")";
    return to_json(v, false);
}

inline Change diff_change(char kind, const std::string& path, const Value* before, const Value* after) {
    Change change;
    change.kind = kind;
    change.path = path;
    if (before) {
        change.before = diff_show(*before);
        change.before_json = to_json(*before, false);
    }
    if (after) {
        change.after = diff_show(*after);
        change.after_json = to_json(*after, false);
    }
    return change;
}

inline std::string diff_json_value(const std::string& value, bool present) {
    return present ? value : "null";
}

inline std::string diff_json_changes(const std::vector<Change>& changes) {
    std::string json = "[";
    for (size_t i = 0; i < changes.size(); i++) {
        if (i) json += ",";
        const Change& change = changes[i];
        std::string kind(1, change.kind);
        json += "{\"type\":";
        json_string(kind, json);
        json += ",\"path\":";
        json_string(change.path, json);
        json += ",\"before\":" + diff_json_value(change.before_json, change.kind != '+');
        json += ",\"after\":" + diff_json_value(change.after_json, change.kind != '-');
        json += ",\"document\":";
        if (change.document.empty()) json += "null";
        else json_string(change.document, json);
        json += ",\"identity\":";
        if (change.identity.empty()) json += "null";
        else json_string(change.identity, json);
        json += "}";
    }
    return json + "]";
}

inline std::string diff_json_strings(const std::vector<std::string>& values) {
    std::string json = "[";
    for (size_t i = 0; i < values.size(); i++) {
        if (i) json += ",";
        json_string(values[i], json);
    }
    return json + "]";
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
    if (a.kind == Value::Secret) return a.s == b.s && a.secret_key == b.secret_key;
    return a.s == b.s;     // Str
}

inline bool diff_equal(const Value& a, const Value& b) {
    if (a.kind != b.kind) return false;
    if (a.kind != Value::List && a.kind != Value::Record) return diff_same_scalar(a, b);
    if (a.items.size() != b.items.size()) return false;
    if (a.kind == Value::List) {
        for (size_t k = 0; k < a.items.size(); k++)
            if (!diff_equal(a.items[k], b.items[k])) return false;
        return true;
    }
    for (size_t k = 0; k < a.keys.size(); k++) {
        const Value* other = diff_field(b, a.keys[k]);
        if (!other || !diff_equal(a.items[k], *other)) return false;
    }
    return true;
}

inline void diff_into(const Value& a, const Value& b, const std::string& path, std::vector<Change>& out,
                      bool list_position = false) {
    if (a.kind == Value::Record && b.kind == Value::Record) {
        for (size_t k = 0; k < a.keys.size(); k++) {
            const Value* other = diff_field(b, a.keys[k]);
            std::string p = diff_key(path, a.keys[k]);
            if (other) diff_into(a.items[k], *other, p, out, list_position);
            else out.push_back(diff_change('-', p, &a.items[k], nullptr));
        }
        for (size_t k = 0; k < b.keys.size(); k++)
            if (!diff_field(a, b.keys[k])) out.push_back(diff_change('+', diff_key(path, b.keys[k]), nullptr, &b.items[k]));
        return;
    }
    if (a.kind == Value::List && b.kind == Value::List) {
        if (!list_position && diff_named(a) && diff_named(b)) {
            auto label = [&](const Value& item) { return path + "[name=" + diff_field(item, "name")->s + "]"; };
            for (const Value& item : a.items) {
                const Value* match = nullptr;
                for (const Value& cand : b.items)
                    if (diff_field(cand, "name")->s == diff_field(item, "name")->s) match = &cand;
                if (match) diff_into(item, *match, label(item), out, list_position);
                else out.push_back(diff_change('-', label(item), &item, nullptr));
            }
            for (const Value& item : b.items) {
                bool found = false;
                for (const Value& cand : a.items)
                    if (diff_field(cand, "name")->s == diff_field(item, "name")->s) found = true;
                if (!found) out.push_back(diff_change('+', label(item), nullptr, &item));
            }
            return;
        }
        if (!list_position && a.items.size() <= 256 && b.items.size() <= 256) {
            size_t rows = a.items.size() + 1, cols = b.items.size() + 1;
            std::vector<size_t> lcs(rows * cols, 0);
            auto cell = [&](size_t i, size_t j) -> size_t& { return lcs[i * cols + j]; };
            for (size_t i = a.items.size(); i-- > 0;)
                for (size_t j = b.items.size(); j-- > 0;)
                    cell(i, j) = diff_equal(a.items[i], b.items[j])
                                     ? 1 + cell(i + 1, j + 1)
                                     : std::max(cell(i + 1, j), cell(i, j + 1));

            std::vector<std::pair<size_t, size_t>> matches;
            size_t i = 0, j = 0;
            while (i < a.items.size() && j < b.items.size()) {
                if (diff_equal(a.items[i], b.items[j])) {
                    matches.push_back({i++, j++});
                } else if (cell(i + 1, j) >= cell(i, j + 1)) {
                    i++;
                } else {
                    j++;
                }
            }

            auto compare_gap = [&](size_t left_end, size_t right_end, size_t& left, size_t& right) {
                size_t common = std::min(left_end - left, right_end - right);
                for (size_t k = 0; k < common; k++)
                    diff_into(a.items[left + k], b.items[right + k],
                              path + "[" + std::to_string(left + k) + "]", out, list_position);
                left += common;
                right += common;
                while (left < left_end) {
                    out.push_back(diff_change('-', path + "[" + std::to_string(left) + "]", &a.items[left], nullptr));
                    left++;
                }
                while (right < right_end) {
                    out.push_back(diff_change('+', path + "[" + std::to_string(right) + "]", nullptr, &b.items[right]));
                    right++;
                }
            };

            size_t left = 0, right = 0;
            for (const auto& match : matches) {
                compare_gap(match.first, match.second, left, right);
                left = match.first + 1;
                right = match.second + 1;
            }
            compare_gap(a.items.size(), b.items.size(), left, right);
        } else {
            size_t common = std::min(a.items.size(), b.items.size());
            for (size_t k = 0; k < common; k++)
                diff_into(a.items[k], b.items[k], path + "[" + std::to_string(k) + "]", out, list_position);
            for (size_t k = common; k < a.items.size(); k++)
                out.push_back(diff_change('-', path + "[" + std::to_string(k) + "]", &a.items[k], nullptr));
            for (size_t k = common; k < b.items.size(); k++)
                out.push_back(diff_change('+', path + "[" + std::to_string(k) + "]", nullptr, &b.items[k]));
        }
        return;
    }
    if (!diff_same_scalar(a, b)) out.push_back(diff_change('~', path, &a, &b));
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

inline std::string diff_identity(const Value& doc, const std::string& path, std::string& shown) {
    const Value* cur = &doc;
    size_t start = 0;
    while (true) {
        size_t dot = path.find('.', start);
        std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (cur->kind != Value::Record || !(cur = diff_field(*cur, key)))
            throw std::runtime_error("document has no value at " + path);
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    if (cur->kind == Value::Str) {
        shown = to_json(*cur, false);
        return "s:" + cur->s;
    }
    if (cur->kind == Value::Int) {
        shown = std::to_string(cur->i);
        return "i:" + shown;
    }
    if (cur->kind == Value::Bool) {
        shown = cur->i ? "true" : "false";
        return "b:" + shown;
    }
    throw std::runtime_error("match field " + path + " must be a string, int or bool");
}

inline std::string diff_kubernetes_identity(const Value& doc, std::string& shown) {
    if (doc.kind != Value::Record)
        throw std::runtime_error("Kubernetes document must be a record");
    const Value* api_version = diff_field(doc, "apiVersion");
    const Value* kind = diff_field(doc, "kind");
    const Value* metadata = diff_field(doc, "metadata");
    const Value* name = metadata && metadata->kind == Value::Record ? diff_field(*metadata, "name") : nullptr;
    const Value* name_space = metadata && metadata->kind == Value::Record ? diff_field(*metadata, "namespace") : nullptr;
    if (!api_version || api_version->kind != Value::Str || api_version->s.empty() ||
        !kind || kind->kind != Value::Str || kind->s.empty() || !name || name->kind != Value::Str || name->s.empty())
        throw std::runtime_error("Kubernetes identity requires string apiVersion, kind and metadata.name");
    if (name_space && name_space->kind != Value::Str)
        throw std::runtime_error("Kubernetes identity metadata.namespace must be a string when present");

    std::string ns = name_space ? name_space->s : "";
    auto part = [](const std::string& value) { return std::to_string(value.size()) + ":" + value; };
    std::string key = part(api_version->s) + part(kind->s) + part(name_space ? "present" : "absent") +
                      part(ns) + part(name->s);
    std::string api_text, kind_text, name_text, namespace_text;
    json_string(api_version->s, api_text);
    json_string(kind->s, kind_text);
    json_string(name->s, name_text);
    if (name_space) json_string(ns, namespace_text);
    else namespace_text = "<unset>";
    shown = "apiVersion=" + api_text + ", kind=" + kind_text + ", namespace=" + namespace_text + ", name=" + name_text;
    return key;
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
inline std::vector<Change> diff_pair(const Value& a, const Value& b, const std::vector<std::string>& ignore,
                                    std::vector<std::string>* ignored_paths = nullptr,
                                    const std::vector<std::string>* report_ignore = nullptr,
                                    const std::string& report_prefix = "", bool list_position = false) {
    std::vector<Change> all, keep;
    diff_into(a, b, "", all, list_position);
    for (Change& c : all) {
        if (diff_hidden(c.path, ignore)) {
            bool report = !report_ignore || !diff_hidden(c.path, *report_ignore);
            std::string shown_path = report_prefix + (c.path.empty() ? "(value)" : c.path);
            if (ignored_paths && report &&
                std::find(ignored_paths->begin(), ignored_paths->end(), shown_path) == ignored_paths->end())
                ignored_paths->push_back(shown_path);
        } else {
            keep.push_back(std::move(c));
        }
    }
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

inline void diff_print_ignored(const std::vector<std::string>& paths, std::ostream& os) {
    for (const std::string& path : paths)
        os << "! ignored change at " << (path.empty() ? "(value)" : path) << "\n";
}

inline std::string diff_github_escape(const std::string& value) {
    std::string out;
    for (char c : value) {
        if (c == '%') out += "%25";
        else if (c == '\r') out += "%0D";
        else if (c == '\n') out += "%0A";
        else if (c == ',') out += "%2C";
        else if (c == ':') out += "%3A";
        else out += c;
    }
    return out;
}

inline std::string diff_html_escape(const std::string& value) {
    std::string out;
    for (char c : value) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
    }
    return out;
}

inline int diff_main(const std::vector<std::string>& args, std::ostream& out, std::ostream& err,
                     std::vector<Change>* structured = nullptr,
                     std::vector<std::string>* ignored_structured = nullptr) {
    bool json_output = std::find(args.begin(), args.end(), "--json") != args.end();
    bool github_output = std::find(args.begin(), args.end(), "--github") != args.end();
    if (json_output && github_output) {
        err << "error: --json and --github cannot be used together\n";
        return 2;
    }
    if (json_output) {
        std::vector<std::string> plain_args;
        for (const std::string& arg : args)
            if (arg != "--json") plain_args.push_back(arg);
        std::ostringstream plain_out, plain_err;
        std::vector<Change> changes;
        std::vector<std::string> ignored_paths;
        int code = diff_main(plain_args, plain_out, plain_err, &changes, &ignored_paths);
        std::string output_json, error_json;
        json_string(plain_out.str(), output_json);
        json_string(plain_err.str(), error_json);
        const char* status = code == 0 ? "identical" : code == 1 ? "different" : "error";
        out << "{\"status\":\"" << status << "\",\"exitCode\":" << code
            << ",\"changes\":" << diff_json_changes(changes)
            << ",\"ignoredPaths\":" << diff_json_strings(ignored_paths)
            << ",\"output\":" << output_json << ",\"error\":" << error_json << "}\n";
        return code;
    }
    if (github_output) {
        std::vector<std::string> plain_args;
        for (const std::string& arg : args)
            if (arg != "--github") plain_args.push_back(arg);
        std::ostringstream plain_out, plain_err;
        int code = diff_main(plain_args, plain_out, plain_err);
        auto annotations = [&](const std::string& text, const char* level) {
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line))
                out << "::" << level << " title=n+ diff::" << diff_github_escape(line) << "\n";
        };
        annotations(plain_out.str(), code == 2 ? "error" : "notice");
        annotations(plain_err.str(), "error");
        if (const char* summary_path = std::getenv("GITHUB_STEP_SUMMARY")) {
            if (*summary_path) {
                std::ofstream summary(summary_path, std::ios::app);
                if (summary) {
                    summary << "\n## n+ configuration diff\n\n<pre>\n";
                    summary << diff_html_escape(plain_out.str().empty() ? plain_err.str() : plain_out.str());
                    summary << "</pre>\n";
                } else {
                    err << "error: could not append GitHub step summary\n";
                    return 2;
                }
            }
        }
        return code;
    }
    std::vector<std::string> pos, ignore;
    std::string by, match;
    bool match_k8s = false;
    bool allow_ignored = false;
    bool list_position = false;
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--match-k8s") {
            match_k8s = true;
        } else if (args[i] == "--allow-ignored") {
            allow_ignored = true;
        } else if (args[i] == "--list-position") {
            list_position = true;
        } else if (args[i] == "--by" || args[i] == "--match" || args[i] == "--ignore") {
            if (i + 1 >= args.size()) { err << "error: " << args[i] << " needs a value\n"; return 2; }
            if (args[i] == "--by") by = args[++i];
            else if (args[i] == "--match") match = args[++i];
            else ignore.push_back(args[++i]);
        } else if (args[i].compare(0, 2, "--") == 0) {
            err << "error: unknown option " << args[i] << "\n";
            return 2;
        } else {
            pos.push_back(args[i]);
        }
    }
    bool one_file = !by.empty();
    if ((!one_file && pos.size() != 2) || (one_file && pos.size() != 3) ||
        ((!match.empty() || match_k8s) && (one_file || (match_k8s && !match.empty())))) {
        err << "usage: nero diff a.nero b.nero [--ignore PATH] [--allow-ignored] [--list-position] [--json|--github]\n"
               "       nero diff a.nero b.nero --match PATH [--ignore PATH] [--allow-ignored] [--list-position] [--json|--github]\n"
               "       nero diff a.nero b.nero --match-k8s [--ignore PATH] [--allow-ignored] [--list-position] [--json|--github]\n"
               "       nero diff file.nero --by PATH X Y [--ignore PATH] [--allow-ignored] [--list-position] [--json|--github]\n";
        return 2;
    }
    if (allow_ignored && ignore.empty()) {
        err << "error: --allow-ignored requires at least one --ignore PATH\n";
        return 2;
    }
    std::vector<std::string> report_ignore;
    if (one_file) report_ignore.push_back(by);
    if (one_file) ignore.push_back(by);

    try {
        std::vector<Value> auto_docs_a, auto_docs_b;
        if (!one_file && match.empty() && !match_k8s) {
            auto_docs_a = diff_collect(pos[0]);
            auto_docs_b = diff_collect(pos[1]);
            auto looks_kubernetes = [](const std::vector<Value>& docs) {
                for (const Value& doc : docs)
                    if (diff_field(doc, "apiVersion") && diff_field(doc, "kind")) return true;
                return false;
            };
            match_k8s = looks_kubernetes(auto_docs_a) || looks_kubernetes(auto_docs_b);
        }
        if (!match.empty() || match_k8s) {
            std::vector<Value> docs_a = auto_docs_a.empty() ? diff_collect(pos[0]) : std::move(auto_docs_a);
            std::vector<Value> docs_b = auto_docs_b.empty() ? diff_collect(pos[1]) : std::move(auto_docs_b);
            std::map<std::string, size_t> left_ids, right_ids;
            std::vector<std::string> left_ids_by_doc(docs_a.size()), right_ids_by_doc(docs_b.size());
            std::vector<std::string> left_shown(docs_a.size()), right_shown(docs_b.size());
            std::vector<std::string> ignored_paths;
            for (size_t k = 0; k < docs_a.size(); k++) {
                std::string id = match_k8s
                                     ? diff_kubernetes_identity(docs_a[k], left_shown[k])
                                     : diff_identity(docs_a[k], match, left_shown[k]);
                left_ids_by_doc[k] = id;
                if (!left_ids.emplace(id, k).second)
                    throw std::runtime_error("duplicate " + (match_k8s ? std::string("resource identity") : "value for " + match) +
                                             " in left file: " + left_shown[k]);
            }
            for (size_t k = 0; k < docs_b.size(); k++) {
                std::string id = match_k8s
                                     ? diff_kubernetes_identity(docs_b[k], right_shown[k])
                                     : diff_identity(docs_b[k], match, right_shown[k]);
                right_ids_by_doc[k] = id;
                if (!right_ids.emplace(id, k).second)
                    throw std::runtime_error("duplicate " + (match_k8s ? std::string("resource identity") : "value for " + match) +
                                             " in right file: " + right_shown[k]);
            }

            size_t total = 0;
            for (size_t k = 0; k < docs_a.size(); k++) {
                auto found = right_ids.find(left_ids_by_doc[k]);
                if (found == right_ids.end()) continue;
                size_t other = found->second;
                std::string report_prefix = diff_label(docs_a[k], k) + " (" +
                                            (match_k8s ? left_shown[k] : match + "=" + left_shown[k]) + ").";
                std::vector<Change> changes = diff_pair(docs_a[k], docs_b[other], ignore, &ignored_paths,
                                                        &report_ignore, report_prefix, list_position);
                for (Change& change : changes) {
                    change.document = diff_label(docs_a[k], k);
                    change.identity = match_k8s ? left_shown[k] : match + "=" + left_shown[k];
                    if (structured) structured->push_back(change);
                }
                if (changes.empty()) continue;
                out << "@ " << diff_label(docs_a[k], k) << " ("
                    << (match_k8s ? left_shown[k] : match + "=" + left_shown[k]) << ")\n";
                diff_print(changes, out);
                total += changes.size();
            }
            for (size_t k = 0; k < docs_a.size(); k++) {
                if (right_ids.count(left_ids_by_doc[k])) continue;
                if (structured) {
                    Change change = diff_change('-', "", &docs_a[k], nullptr);
                    change.document = diff_label(docs_a[k], k);
                    change.identity = match_k8s ? left_shown[k] : match + "=" + left_shown[k];
                    structured->push_back(change);
                }
                out << "- " << diff_label(docs_a[k], k) << " ["
                    << (match_k8s ? left_shown[k] : match + "=" + left_shown[k]) << "] (only on the left)\n";
                total++;
            }
            for (size_t k = 0; k < docs_b.size(); k++) {
                if (left_ids.count(right_ids_by_doc[k])) continue;
                if (structured) {
                    Change change = diff_change('+', "", nullptr, &docs_b[k]);
                    change.document = diff_label(docs_b[k], k);
                    change.identity = match_k8s ? right_shown[k] : match + "=" + right_shown[k];
                    structured->push_back(change);
                }
                out << "+ " << diff_label(docs_b[k], k) << " ["
                    << (match_k8s ? right_shown[k] : match + "=" + right_shown[k]) << "] (only on the right)\n";
                total++;
            }
            diff_print_ignored(ignored_paths, out);
            if (ignored_structured)
                ignored_structured->insert(ignored_structured->end(), ignored_paths.begin(), ignored_paths.end());
            if (total == 0 && ignored_paths.empty()) {
                out << "no differences\n";
                return 0;
            }
            if (total == 0 && allow_ignored) {
                out << "no unignored differences\n";
                return 0;
            }
            if (total == 0) {
                out << "ignored changes require --allow-ignored to pass\n";
                return 1;
            }
            out << total << (total == 1 ? " change\n" : " changes\n");
            return 1;
        }

        std::vector<const Value*> left, right;
        std::vector<Value> docs_a, docs_b;
        std::string head_suffix;
        std::vector<std::string> ignored_paths;
        if (one_file) {
            docs_a = diff_collect(pos[0]);
            left = diff_select(docs_a, by, pos[1]);
            right = diff_select(docs_a, by, pos[2]);
            head_suffix = " (" + pos[1] + " -> " + pos[2] + ")";
        } else {
            docs_a = auto_docs_a.empty() ? diff_collect(pos[0]) : std::move(auto_docs_a);
            docs_b = auto_docs_b.empty() ? diff_collect(pos[1]) : std::move(auto_docs_b);
            for (const Value& d : docs_a) left.push_back(&d);
            for (const Value& d : docs_b) right.push_back(&d);
        }

        size_t total = 0;
        bool headers = left.size() > 1 || right.size() > 1 || one_file;
        size_t common = std::min(left.size(), right.size());
        for (size_t k = 0; k < common; k++) {
            std::string report_prefix = diff_label(*left[k], k) +
                                        (one_file ? head_suffix : " [document " + std::to_string(k + 1) + "]") + ".";
            std::vector<Change> changes = diff_pair(*left[k], *right[k], ignore, &ignored_paths,
                                                    &report_ignore, report_prefix, list_position);
            for (Change& change : changes) {
                change.document = diff_label(*left[k], k);
                if (one_file) change.identity = pos[1] + " -> " + pos[2];
                else change.identity = "document " + std::to_string(k + 1);
                if (structured) structured->push_back(change);
            }
            if (changes.empty()) continue;
            if (headers) out << "@ " << diff_label(*left[k], k) << head_suffix << "\n";
            diff_print(changes, out);
            total += changes.size();
        }
        for (size_t k = common; k < left.size(); k++, total++) {
            if (structured) {
                Change change = diff_change('-', "", left[k], nullptr);
                change.document = diff_label(*left[k], k);
                change.identity = "document " + std::to_string(k + 1);
                structured->push_back(change);
            }
            out << "- " << diff_label(*left[k], k) << " (only on the left)\n";
        }
        for (size_t k = common; k < right.size(); k++, total++) {
            if (structured) {
                Change change = diff_change('+', "", nullptr, right[k]);
                change.document = diff_label(*right[k], k);
                change.identity = "document " + std::to_string(k + 1);
                structured->push_back(change);
            }
            out << "+ " << diff_label(*right[k], k) << " (only on the right)\n";
        }

        diff_print_ignored(ignored_paths, out);
        if (ignored_structured)
            ignored_structured->insert(ignored_structured->end(), ignored_paths.begin(), ignored_paths.end());
        if (total == 0 && ignored_paths.empty()) {
            out << "no differences\n";
            return 0;
        }
        if (total == 0 && allow_ignored) {
            out << "no unignored differences\n";
            return 0;
        }
        if (total == 0) {
            out << "ignored changes require --allow-ignored to pass\n";
            return 1;
        }
        out << total << (total == 1 ? " change\n" : " changes\n");
        return 1;
    } catch (const std::exception& e) {
        err << "error: " << e.what() << "\n";
        return 2;
    }
}
