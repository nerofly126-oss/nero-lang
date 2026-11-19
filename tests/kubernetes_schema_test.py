#!/usr/bin/env python3
"""Validate the generated Kubernetes demo against released API schemas."""
from copy import deepcopy
import json
import subprocess
import tempfile

import kubernetes_validate
import yaml


def json_documents(text):
    decoder = json.JSONDecoder()
    documents = []
    offset = 0
    while offset < len(text):
        while offset < len(text) and text[offset].isspace():
            offset += 1
        if offset == len(text):
            break
        document, offset = decoder.raw_decode(text, offset)
        documents.append(document)
    return documents


def main():
    yaml_source = r'''emit {
        plain: "line1\nline2",
        one_trailing: "line1\nline2\n",
        many_trailing: "line1\nline2\n\n",
        indented: " leading\n  deeper\nend",
        blank_start: "\nstart\n",
        blank_only: "\n\n",
        nested: {script: "echo one\necho two"},
        items: ["first\nsecond", "third\nfourth"],
        item_records: [{script: "first\nsecond"}],
    }'''
    with tempfile.TemporaryDirectory() as tmp:
        source = tmp + "/multiline.nero"
        with open(source, "w", encoding="utf-8") as output:
            output.write(yaml_source)
        multiline_yaml = subprocess.run(
            ["./nero", "eval", source, "--yaml"],
            check=True,
            capture_output=True,
            text=True,
        )
        multiline_json = subprocess.run(
            ["./nero", "eval", source],
            check=True,
            capture_output=True,
            text=True,
        )
        if yaml.safe_load(multiline_yaml.stdout) != json.loads(multiline_json.stdout):
            raise AssertionError("multiline YAML strings did not round-trip to the same values")

    result = subprocess.run(
        ["./nero", "eval", "examples/k8s.nero", "--yaml"],
        check=True,
        capture_output=True,
        text=True,
    )
    documents = list(yaml.safe_load_all(result.stdout))
    if len(documents) != 6:
        raise AssertionError("expected six Kubernetes documents, got %d" % len(documents))

    json_result = subprocess.run(
        ["./nero", "eval", "examples/k8s.nero"],
        check=True,
        capture_output=True,
        text=True,
    )
    if json_documents(json_result.stdout) != documents:
        raise AssertionError("JSON and YAML output differ for the Kubernetes demo")

    preflight = subprocess.run(
        ["./nero", "check", "examples/k8s.nero"],
        capture_output=True,
        text=True,
    )
    if preflight.returncode != 0:
        raise AssertionError("static check rejected the Kubernetes demo: %s" % preflight.stderr)

    invalid_source = subprocess.run(
        ["./nero", "eval", "examples/k8s_bad.nero", "--yaml"],
        capture_output=True,
        text=True,
    )
    if invalid_source.returncode == 0 or invalid_source.stdout:
        raise AssertionError("n+ emitted a Kubernetes manifest with a string replicas value")
    if "field 'replicas' of DeploymentSpec must be an int" not in invalid_source.stderr:
        raise AssertionError("n+ rejected the bad manifest for an unexpected reason")

    kinds = [document["kind"] for document in documents]
    if kinds != ["Deployment", "Service"] * 3:
        raise AssertionError("unexpected Kubernetes document kinds: %r" % kinds)

    for version in ("1.28", "1.30", "1.33"):
        for document in documents:
            kubernetes_validate.validate(document, version, strict=True)

    extended = deepcopy(documents[0])
    container = extended["spec"]["template"]["spec"]["containers"][0]
    container["command"] = ["/app/server"]
    container["args"] = ["--port", "8080"]
    container["ports"][0]["name"] = "http"
    extended["metadata"]["annotations"] = {"example.org/managed-by": "nero"}
    extended["spec"]["template"]["metadata"]["annotations"] = {"example.org/revision": "1"}
    for version in ("1.28", "1.30", "1.33"):
        kubernetes_validate.validate(extended, version, strict=True)

    with tempfile.TemporaryDirectory() as tmp:
        secret_source = tmp + "/secret-ref.nero"
        with open(secret_source, "w", encoding="utf-8") as output:
            output.write(r'''emit {
                apiVersion: "v1",
                kind: "Pod",
                metadata: {name: "secret-ref-check"},
                spec: {containers: [{
                    name: "app",
                    image: "example.invalid/app:1.0",
                    env: [{name: "PASSWORD", valueFrom: secret("database", "password")}],
                }]},
            }''')
        secret_yaml = subprocess.run(
            ["./nero", "eval", secret_source, "--yaml"],
            check=True,
            capture_output=True,
            text=True,
        )
        secret_doc = yaml.safe_load(secret_yaml.stdout)
        value_from = secret_doc["spec"]["containers"][0]["env"][0]["valueFrom"]
        if value_from != {"secretKeyRef": {"name": "database", "key": "password"}}:
            raise AssertionError("secret(name, key) did not emit a Kubernetes secretKeyRef")
        kubernetes_validate.validate(secret_doc, "1.33", strict=True)

    invalid = deepcopy(documents[0])
    invalid["spec"]["replicas"] = "3"
    try:
        kubernetes_validate.validate(invalid, "1.33", strict=True)
    except kubernetes_validate.ValidationError:
        print("JSON and YAML match; Kubernetes schemas passed; optional fields and quoted replicas were checked")
        return 0
    raise AssertionError("Kubernetes schema validation accepted replicas as a string")


if __name__ == "__main__":
    raise SystemExit(main())
