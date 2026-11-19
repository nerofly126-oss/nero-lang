#include "../src/policy.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

static std::string tmp;

static void put(const std::string& name, const std::string& text) {
    std::ofstream(std::filesystem::path(tmp) / name) << text;
}

struct Result { int code; std::string out, err; };

static Result run_policy(const std::string& file) {
    std::ostringstream out, err;
    int code = policy_main({(std::filesystem::path(tmp) / file).string()}, out, err);
    return {code, out.str(), err.str()};
}

static Result run_policy_args(const std::vector<std::string>& args) {
    std::ostringstream out, err;
    int code = policy_main(args, out, err);
    return {code, out.str(), err.str()};
}

int main() {
    auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    tmp = (std::filesystem::temp_directory_path() / ("nero_policy_" + std::to_string(nonce))).string();
    assert(std::filesystem::create_directory(tmp));

    put("good.nero",
        "emit {apiVersion: \"apps/v1\", kind: \"Deployment\", spec: {template: {spec: {automountServiceAccountToken: false, containers: [{image: \"registry/app:v1\", securityContext: {allowPrivilegeEscalation: false, runAsNonRoot: true}, resources: {limits: {cpu: \"1\", memory: \"1Gi\"}}}]}}}}\n"
        "emit {apiVersion: \"v1\", kind: \"Service\", spec: {type: \"ClusterIP\"}}\n");
    Result result = run_policy("good.nero");
    assert(result.code == 0 && result.out == "ok: Kubernetes policy checks passed for 2 documents\n");

    put("bad.nero",
        "emit {apiVersion: \"apps/v1\", kind: \"Deployment\", spec: {template: {spec: {initContainers: [{image: \"init:latest\"}], containers: [{image: \"app:latest\", securityContext: {privileged: true, allowPrivilegeEscalation: true, runAsNonRoot: false, runAsUser: 0}}]}}}}\n"
        "emit {apiVersion: \"v1\", kind: \"Service\", spec: {type: \"LoadBalancer\"}}\n");
    result = run_policy("bad.nero");
    assert(result.code == 1);
    assert(result.out.find("K8S101") != std::string::npos);
    assert(result.out.find("initContainers[0].image") != std::string::npos);
    assert(result.out.find("K8S201") != std::string::npos);
    assert(result.out.find("K8S202") != std::string::npos);
    assert(result.out.find("K8S203") != std::string::npos);
    assert(result.out.find("K8S204") != std::string::npos);
    assert(result.out.find("K8S205") != std::string::npos);
    assert(result.out.find("K8S206") != std::string::npos);
    assert(result.out.find("K8S301") != std::string::npos);
    result = run_policy_args({(std::filesystem::path(tmp) / "bad.nero").string(), "--disable", "K8S101"});
    assert(result.code == 1 && result.out.find("K8S101") == std::string::npos);
    result = run_policy_args({(std::filesystem::path(tmp) / "bad.nero").string(), "--only", "K8S101"});
    assert(result.code == 1 && result.out.find("K8S101") != std::string::npos &&
           result.out.find("K8S201") == std::string::npos);
    result = run_policy_args({(std::filesystem::path(tmp) / "good.nero").string(), "--disable"});
    assert(result.code == 2);
    result = run_policy_args({(std::filesystem::path(tmp) / "good.nero").string(), "--only", "K8S999"});
    assert(result.code == 2 && result.err.find("unknown policy rule K8S999") != std::string::npos);

    put("custom_rules.nero",
        "emit {id: \"ORG001\", kind: \"Deployment\", path: \"metadata.labels.team\", op: \"exists\", message: \"set a team label\"}\n"
        "emit {id: \"ORG002\", kind: \"Deployment\", path: \"spec.template.spec.containers[*].image\", op: \"contains\", value: \"registry.example.com/\", message: \"use the approved registry\"}\n"
        "emit {id: \"ORG003\", kind: \"Deployment\", path: \"spec.replicas\", op: \"equals\", value: 3, message: \"production must use three replicas\"}\n");
    put("custom_bad.nero",
        "emit {kind: \"Deployment\", metadata: {labels: {app: \"web\"}}, spec: {replicas: 2, template: {spec: {containers: [{image: \"registry.example.com/web:v1\"}, {image: \"docker.io/other:v1\"}]}}}}\n");
    result = run_policy_args({(std::filesystem::path(tmp) / "custom_bad.nero").string(),
                              "--rules", (std::filesystem::path(tmp) / "custom_rules.nero").string()});
    assert(result.code == 1 && result.out.find("ORG001") != std::string::npos &&
           result.out.find("ORG002") != std::string::npos && result.out.find("ORG003") != std::string::npos);
    result = run_policy_args({(std::filesystem::path(tmp) / "custom_bad.nero").string(),
                              "--rules", (std::filesystem::path(tmp) / "custom_rules.nero").string(),
                              "--only", "ORG001"});
    assert(result.code == 1 && result.out.find("ORG001") != std::string::npos &&
           result.out.find("ORG002") == std::string::npos);
    put("custom_good.nero",
        "emit {kind: \"Deployment\", metadata: {labels: {team: \"platform\"}}, spec: {replicas: 3, template: {spec: {containers: [{image: \"registry.example.com/web:v1\"}]}}}}\n");
    result = run_policy_args({(std::filesystem::path(tmp) / "custom_good.nero").string(),
                              "--rules", (std::filesystem::path(tmp) / "custom_rules.nero").string(),
                              "--only", "ORG001", "--only", "ORG002", "--only", "ORG003"});
    assert(result.code == 0);
    put("bad_rules.nero", "emit {id: \"ORG004\", path: \"spec.replicas\", op: \"unknown\", message: \"invalid\"}\n");
    result = run_policy_args({(std::filesystem::path(tmp) / "custom_good.nero").string(),
                              "--rules", (std::filesystem::path(tmp) / "bad_rules.nero").string()});
    assert(result.code == 2 && result.err.find("unsupported custom policy operator") != std::string::npos);

    put("digest.nero",
        "emit {apiVersion: \"v1\", kind: \"Pod\", spec: {automountServiceAccountToken: false, containers: [{image: \"app@sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\", securityContext: {allowPrivilegeEscalation: false, runAsNonRoot: true}, resources: {limits: {cpu: \"1\", memory: \"1Gi\"}}}]}}\n");
    result = run_policy("digest.nero");
    assert(result.code == 0);

    const std::string safe_pod_spec =
        "{automountServiceAccountToken: false, containers: [{image: \"app:v1\", "
        "securityContext: {allowPrivilegeEscalation: false, runAsNonRoot: true}, "
        "resources: {limits: {cpu: \"1\", memory: \"1Gi\"}}}]}";
    put("template_kinds.nero",
        "emit {kind: \"ReplicaSet\", spec: {template: {spec: " + safe_pod_spec + "}}}\n"
        "emit {kind: \"ReplicationController\", spec: {template: {spec: " + safe_pod_spec + "}}}\n"
        "emit {kind: \"PodTemplate\", template: {spec: " + safe_pod_spec + "}}\n");
    result = run_policy("template_kinds.nero");
    assert(result.code == 0 && result.out == "ok: Kubernetes policy checks passed for 3 documents\n");

    put("tagged_digest.nero",
        "emit {apiVersion: \"v1\", kind: \"Pod\", spec: {automountServiceAccountToken: false, containers: [{image: \"app:latest@sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\", securityContext: {allowPrivilegeEscalation: false, runAsNonRoot: true}, resources: {limits: {cpu: \"1\", memory: \"1Gi\"}}}]}}\n");
    result = run_policy("tagged_digest.nero");
    assert(result.code == 0);

    put("bad_digest.nero",
        "emit {apiVersion: \"v1\", kind: \"Pod\", spec: {automountServiceAccountToken: false, containers: [{image: \"app@sha256:abc\", securityContext: {allowPrivilegeEscalation: false, runAsNonRoot: true}, resources: {limits: {cpu: \"1\", memory: \"1Gi\"}}}]}}\n");
    result = run_policy("bad_digest.nero");
    assert(result.code == 1 && result.out.find("K8S101") != std::string::npos);

    put("unknown.nero", "emit {kind: \"CustomThing\"}\n");
    result = run_policy("unknown.nero");
    assert(result.code == 1 && result.out.find("K8S000") != std::string::npos);
    std::ostringstream out, err;
    assert(policy_main({}, out, err) == 2);

    std::filesystem::remove_all(tmp);
    std::puts("all policy tests passed");
    return 0;
}
