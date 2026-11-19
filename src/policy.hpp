#pragma once
#include "evaluator.hpp"
#include "loader.hpp"
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct PolicyFinding {
    std::string rule, path, message;
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
        else if (kind == "Deployment" || kind == "StatefulSet" || kind == "DaemonSet" || kind == "Job")
            pod_spec_path = {"spec", "template", "spec"};
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
    if (args.size() != 1) {
        err << "usage: nero policy file.nero\n";
        return 2;
    }
    try {
        Program prog = Loader().load(args[0]);
        std::ostringstream sink;
        Evaluator evaluator(prog, sink);
        std::vector<Value> documents;
        evaluator.collect(documents);
        evaluator.run();
        std::vector<PolicyFinding> findings = policy_inspect(documents);
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
