#!/usr/bin/env python3
"""Validate the generated Kubernetes demo against released API schemas."""
from copy import deepcopy
import subprocess

import kubernetes_validate
import yaml


def main():
    result = subprocess.run(
        ["./nero", "eval", "examples/k8s.nero", "--yaml"],
        check=True,
        capture_output=True,
        text=True,
    )
    documents = list(yaml.safe_load_all(result.stdout))
    if len(documents) != 6:
        raise AssertionError("expected six Kubernetes documents, got %d" % len(documents))

    kinds = [document["kind"] for document in documents]
    if kinds != ["Deployment", "Service"] * 3:
        raise AssertionError("unexpected Kubernetes document kinds: %r" % kinds)

    for version in ("1.28", "1.30", "1.33"):
        for document in documents:
            kubernetes_validate.validate(document, version, strict=True)

    invalid = deepcopy(documents[0])
    invalid["spec"]["replicas"] = "3"
    try:
        kubernetes_validate.validate(invalid, "1.33", strict=True)
    except kubernetes_validate.ValidationError:
        print("Kubernetes manifests passed schema checks; quoted replicas was rejected")
        return 0
    raise AssertionError("Kubernetes schema validation accepted replicas as a string")


if __name__ == "__main__":
    raise SystemExit(main())
