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

struct PolicyFinding {
    std::string rule, path, message;
};

struct PolicyRule {
    std::string id, kind, path, op, message;
    std::vector<std::string> path_parts;
    Value expected;
};

inline const Value* policy_field(const Value* value, const std::string& key) {
    if (!value || value->kind != Value::Record) return nullptr;
    for (size_t i = 0; i < value->keys.size(); i++)
        if (value->keys[i] == key) return &value->items[i];
    return nullptr;
}

inline const Value* policy_path(const Value* value, const std::vector<std::string>& path) {
    for (const std::string& key : path) value = policy_field(value, key);
    return value;
}

inline bool policy_value_equal(const Value& left, const Value& right) {
    if (left.kind != right.kind) return false;
    if (left.kind == Value::Int || left.kind == Value::Bool) return left.i == right.i;
    if (left.kind == Value::Str) return left.s == right.s;
    if (left.kind == Value::Secret) return left.s == right.s && left.secret_key == right.secret_key;
    if (left.items.size() != right.items.size()) return false;
    if (left.kind == Value::List) {
        for (size_t i = 0; i < left.items.size(); i++)
            if (!policy_value_equal(left.items[i], right.items[i])) return false;
        return true;
    }
    for (size_t i = 0; i < left.keys.size(); i++) {
        const Value* match = policy_field(&right, left.keys[i]);
        if (!match || !policy_value_equal(left.items[i], *match)) return false;
    }
    return true;
}

inline bool policy_rule_path(const Value& root, const std::vector<std::string>& parts, size_t index,
                             std::vector<const Value*>& matches) {
    if (index == parts.size()) {
        matches.push_back(&root);
        return true;
    }
    const std::string& part = parts[index];
    bool wildcard = part.size() >= 3 && part.compare(part.size() - 3, 3, "[*]") == 0;
    std::string field = wildcard ? part.substr(0, part.size() - 3) : part;
    const Value* value = policy_field(&root, field);
    if (!value) return true;
    if (!wildcard) return policy_rule_path(*value, parts, index + 1, matches);
    if (value->kind != Value::List) return false;
    for (const Value& item : value->items)
        if (!policy_rule_path(item, parts, index + 1, matches)) return false;
    return true;
}

inline std::vector<PolicyRule> policy_load_rules(const std::string& file) {
    Program prog = Loader().load(file);
    std::ostringstream sink;
    Evaluator evaluator(prog, sink);
    std::vector<Value> values;
    evaluator.collect(values);
    evaluator.run();

    std::vector<PolicyRule> rules;
    std::set<std::string> ids;
    for (const Value& value : values) {
        if (value.kind != Value::Record)
            throw std::runtime_error("custom policy rules must emit records");
        const Value* id = policy_field(&value, "id");
        const Value* path = policy_field(&value, "path");
        const Value* op = policy_field(&value, "op");
        const Value* message = policy_field(&value, "message");
        const Value* kind = policy_field(&value, "kind");
        const Value* expected = policy_field(&value, "value");
        bool valid_id = id && id->kind == Value::Str && id->s.size() >= 3 && id->s.size() <= 32 &&
                        std::all_of(id->s.begin(), id->s.end(), [](unsigned char c) {
                            return std::isupper(c) || std::isdigit(c) || c == '_';
                        });
        if (!valid_id)
            throw std::runtime_error("custom policy rule id must contain 3-32 uppercase letters, digits or underscores");
        if (!ids.insert(id->s).second) throw std::runtime_error("duplicate custom policy rule " + id->s);
        if (!path || path->kind != Value::Str || path->s.empty() || path->s.back() == '.' ||
            !op || op->kind != Value::Str ||
            !message || message->kind != Value::Str || message->s.empty())
            throw std::runtime_error("custom policy rule requires string path, op and message fields");
        if (kind && kind->kind != Value::Str) throw std::runtime_error("custom policy rule kind must be a string");
        if (op->s != "exists" && op->s != "missing" && op->s != "equals" &&
            op->s != "notEquals" && op->s != "contains")
            throw std::runtime_error("unsupported custom policy operator " + op->s);
        bool needs_value = op->s == "equals" || op->s == "notEquals" || op->s == "contains";
        if (needs_value != (expected != nullptr))
            throw std::runtime_error("custom policy operator " + op->s + (needs_value ? " requires value" : " does not accept value"));
        std::vector<std::string> parts;
        std::istringstream path_stream(path->s);
        std::string part;
        while (std::getline(path_stream, part, '.')) {
            bool wildcard = part.size() >= 3 && part.compare(part.size() - 3, 3, "[*]") == 0;
            std::string field = wildcard ? part.substr(0, part.size() - 3) : part;
            if (field.empty() || field.find_first_of("[]") != std::string::npos)
                throw std::runtime_error("invalid custom policy path " + path->s);
            parts.push_back(part);
        }
        rules.push_back({id->s, kind ? kind->s : "", path->s, op->s, message->s, std::move(parts),
                         expected ? *expected : Value::record()});
    }
    return rules;
}

inline void policy_apply_rules(const std::vector<Value>& documents, const std::vector<PolicyRule>& rules,
                               std::vector<PolicyFinding>& findings) {
    for (size_t i = 0; i < documents.size(); i++) {
        const Value& document = documents[i];
        const Value* kind_value = policy_field(&document, "kind");
        for (const PolicyRule& rule : rules) {
            if (!rule.kind.empty() && (!kind_value || kind_value->kind != Value::Str || kind_value->s != rule.kind))
                continue;
            std::vector<const Value*> matches;
            if (!policy_rule_path(document, rule.path_parts, 0, matches))
                throw std::runtime_error("custom policy path wildcard requires a list at " + rule.path);
            bool failed = false;
            if (rule.op == "exists") failed = matches.empty();
            else if (rule.op == "missing") failed = !matches.empty();
            else if (rule.op == "equals")
                failed = matches.empty() || std::any_of(matches.begin(), matches.end(), [&](const Value* value) {
                    return !policy_value_equal(*value, rule.expected);
                });
            else if (rule.op == "notEquals")
                failed = matches.empty() || std::any_of(matches.begin(), matches.end(), [&](const Value* value) {
                    return policy_value_equal(*value, rule.expected);
                });
            else {
                failed = matches.empty();
                for (const Value* value : matches) {
                    bool contains = value->kind == Value::Str && rule.expected.kind == Value::Str &&
                                    value->s.find(rule.expected.s) != std::string::npos;
                    contains = contains || (value->kind == Value::List &&
                        std::any_of(value->items.begin(), value->items.end(), [&](const Value& item) {
                            return policy_value_equal(item, rule.expected);
                        }));
                    if (!contains) failed = true;
                }
            }
            if (failed) findings.push_back({rule.id, (kind_value && kind_value->kind == Value::Str ? kind_value->s : "document") +
                                            "[" + std::to_string(i + 1) + "]." + rule.path, rule.message});
        }
    }
}

inline void policy_inspect_container(const Value& container, const std::string& path,
                                     bool pod_run_as_non_root, std::vector<PolicyFinding>& findings,
                                     bool require_limits = true) {
    const Value* image = policy_field(&container, "image");
    if (!image || image->kind != Value::Str || image->s.empty()) {
        findings.push_back({"K8S101", path + ".image", "container image is missing"});
    } else {
        size_t slash = image->s.rfind('/');
        size_t colon = image->s.rfind(':');
        size_t at = image->s.rfind('@');
        bool tagged = colon != std::string::npos && (slash == std::string::npos || colon > slash) &&
                      (at == std::string::npos || colon < at);
        size_t digest = image->s.rfind("@sha256:");
        bool digested = digest != std::string::npos && image->s.size() - digest == 8 + 64;
        if (digested)
            for (size_t i = digest + 8; i < image->s.size(); i++)
                if (!std::isxdigit(static_cast<unsigned char>(image->s[i]))) digested = false;
        bool latest = image->s.size() >= 7 && image->s.compare(image->s.size() - 7, 7, ":latest") == 0;
        if ((!tagged && !digested) || (latest && !digested))
            findings.push_back({"K8S101", path + ".image", "use an explicit image tag or SHA-256 digest"});
    }

    const Value* security = policy_field(&container, "securityContext");
    const Value* privileged = policy_field(security, "privileged");
    if (privileged && privileged->kind == Value::Bool && privileged->i)
        findings.push_back({"K8S201", path + ".securityContext.privileged", "privileged containers are not allowed"});
    const Value* escalation = policy_field(security, "allowPrivilegeEscalation");
    if (!escalation || escalation->kind != Value::Bool || escalation->i)
        findings.push_back({"K8S202", path + ".securityContext.allowPrivilegeEscalation",
                            "set allowPrivilegeEscalation to false"});
    const Value* run_as_non_root = policy_field(security, "runAsNonRoot");
    if (run_as_non_root && (run_as_non_root->kind != Value::Bool || !run_as_non_root->i))
        findings.push_back({"K8S203", path + ".securityContext.runAsNonRoot",
                            "set runAsNonRoot to true on the container or pod"});
    else if (!run_as_non_root && !pod_run_as_non_root)
        findings.push_back({"K8S203", path + ".securityContext.runAsNonRoot",
                            "set runAsNonRoot to true on the container or pod"});
    const Value* run_as_user = policy_field(security, "runAsUser");
    if (run_as_user && run_as_user->kind == Value::Int && run_as_user->i == 0)
        findings.push_back({"K8S206", path + ".securityContext.runAsUser", "container must not run as UID 0"});

    if (require_limits) {
        const Value* limits = policy_path(&container, {"resources", "limits"});
        if (!policy_field(limits, "cpu") || !policy_field(limits, "memory"))
            findings.push_back({"K8S204", path + ".resources.limits", "set both CPU and memory limits"});
    }
}

inline std::vector<PolicyFinding> policy_inspect(const std::vector<Value>& documents) {
    std::vector<PolicyFinding> findings;
    for (size_t i = 0; i < documents.size(); i++) {
        const Value& doc = documents[i];
        const Value* kind_value = policy_field(&doc, "kind");
        if (!kind_value || kind_value->kind != Value::Str) {
            findings.push_back({"K8S000", "document[" + std::to_string(i + 1) + "]", "document has no string kind"});
            continue;
        }
        const std::string& kind = kind_value->s;
        std::string root = kind + "[" + std::to_string(i + 1) + "]";
        if (kind == "Service") {
            const Value* type = policy_path(&doc, {"spec", "type"});
            if (type && type->kind == Value::Str && (type->s == "LoadBalancer" || type->s == "NodePort"))
                findings.push_back({"K8S301", root + ".spec.type", "service is externally reachable; review the exposure"});
            continue;
        }

        std::vector<std::string> pod_spec_path;
        if (kind == "Pod") pod_spec_path = {"spec"};
        else if (kind == "Deployment" || kind == "ReplicaSet" || kind == "ReplicationController" ||
                 kind == "StatefulSet" || kind == "DaemonSet" || kind == "Job")
            pod_spec_path = {"spec", "template", "spec"};
        else if (kind == "PodTemplate") pod_spec_path = {"template", "spec"};
        else if (kind == "CronJob") pod_spec_path = {"spec", "jobTemplate", "spec", "template", "spec"};
        else {
            findings.push_back({"K8S000", root, "policy does not know how to inspect this resource kind"});
            continue;
        }

        const Value* pod_spec = policy_path(&doc, pod_spec_path);
        std::string pod_path = root;
        for (const std::string& part : pod_spec_path) pod_path += "." + part;
        const Value* pod_security = policy_field(pod_spec, "securityContext");
        const Value* pod_non_root = policy_field(pod_security, "runAsNonRoot");
        bool pod_run_as_non_root = pod_non_root && pod_non_root->kind == Value::Bool && pod_non_root->i;
        const Value* pod_run_as_user = policy_field(pod_security, "runAsUser");
        if (pod_run_as_user && pod_run_as_user->kind == Value::Int && pod_run_as_user->i == 0)
            findings.push_back({"K8S206", pod_path + ".securityContext.runAsUser", "pod must not run as UID 0"});
        const Value* auto_mount = policy_field(pod_spec, "automountServiceAccountToken");
        if (!auto_mount || auto_mount->kind != Value::Bool || auto_mount->i)
            findings.push_back({"K8S205", pod_path + ".automountServiceAccountToken",
                                "set automountServiceAccountToken to false unless the workload needs API access"});

        const Value* containers = policy_field(pod_spec, "containers");
        if (!containers || containers->kind != Value::List || containers->items.empty()) {
            findings.push_back({"K8S000", pod_path + ".containers", "workload has no inspectable containers"});
        } else {
            for (size_t j = 0; j < containers->items.size(); j++)
                policy_inspect_container(containers->items[j], pod_path + ".containers[" + std::to_string(j) + "]",
                                         pod_run_as_non_root, findings);
        }
        for (const char* group_name : {"initContainers", "ephemeralContainers"}) {
            std::string group = group_name;
            const Value* extra = policy_field(pod_spec, group);
            if (!extra || extra->kind != Value::List) continue;
            for (size_t j = 0; j < extra->items.size(); j++)
                policy_inspect_container(extra->items[j], pod_path + "." + group + "[" + std::to_string(j) + "]",
                                         pod_run_as_non_root, findings, group != "ephemeralContainers");
        }
    }
    return findings;
}

inline int policy_main(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    std::vector<std::string> files, disabled, enabled;
    std::string rules_file;
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--disable" || args[i] == "--only") {
            if (i + 1 >= args.size()) {
                err << "error: " << args[i] << " needs a rule ID\n";
                return 2;
            }
            bool is_disabled = args[i] == "--disable";
            std::string rule = args[++i];
            (is_disabled ? disabled : enabled).push_back(rule);
        } else if (args[i] == "--rules") {
            if (i + 1 >= args.size()) {
                err << "error: --rules needs a file path\n";
                return 2;
            }
            rules_file = args[++i];
        } else if (args[i].compare(0, 2, "--") == 0) {
            err << "error: unknown option " << args[i] << "\n";
            return 2;
        } else {
            files.push_back(args[i]);
        }
    }
    if (files.size() != 1) {
        err << "usage: nero policy file.nero [--rules FILE] [--disable RULE]... [--only RULE]...\n";
        return 2;
    }
    try {
        std::vector<PolicyRule> custom_rules = rules_file.empty() ? std::vector<PolicyRule>() : policy_load_rules(rules_file);
        std::set<std::string> known_rules = {"K8S000", "K8S101", "K8S201", "K8S202", "K8S203",
                                             "K8S204", "K8S205", "K8S206", "K8S301"};
        for (const PolicyRule& rule : custom_rules)
            if (!known_rules.insert(rule.id).second)
                throw std::runtime_error("custom policy rule ID conflicts with a built-in rule: " + rule.id);
        for (const std::string& rule : disabled)
            if (!known_rules.count(rule)) throw std::runtime_error("unknown policy rule " + rule);
        for (const std::string& rule : enabled)
            if (!known_rules.count(rule)) throw std::runtime_error("unknown policy rule " + rule);

        Program prog = Loader().load(files[0]);
        std::ostringstream sink;
        Evaluator evaluator(prog, sink);
        std::vector<Value> documents;
        evaluator.collect(documents);
        evaluator.run();
        std::vector<PolicyFinding> findings = policy_inspect(documents);
        policy_apply_rules(documents, custom_rules, findings);
        auto selected = [&](const std::string& rule) {
            if (std::find(disabled.begin(), disabled.end(), rule) != disabled.end()) return false;
            return enabled.empty() || std::find(enabled.begin(), enabled.end(), rule) != enabled.end();
        };
        findings.erase(std::remove_if(findings.begin(), findings.end(), [&](const PolicyFinding& finding) {
            return !selected(finding.rule);
        }), findings.end());
        if (findings.empty()) {
            out << "ok: Kubernetes policy checks passed for " << documents.size() << " documents\n";
            return 0;
        }
        for (const PolicyFinding& finding : findings)
            out << finding.rule << " " << finding.path << ": " << finding.message << "\n";
        out << findings.size() << (findings.size() == 1 ? " policy finding\n" : " policy findings\n");
        return 1;
    } catch (const std::exception& e) {
        err << "error: " << e.what() << "\n";
        return 2;
    }
}
