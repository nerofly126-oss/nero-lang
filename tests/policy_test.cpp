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

    put("digest.nero",
        "emit {apiVersion: \"v1\", kind: \"Pod\", spec: {automountServiceAccountToken: false, containers: [{image: \"app@sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\", securityContext: {allowPrivilegeEscalation: false, runAsNonRoot: true}, resources: {limits: {cpu: \"1\", memory: \"1Gi\"}}}]}}\n");
    result = run_policy("digest.nero");
    assert(result.code == 0);

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
