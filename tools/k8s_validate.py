#!/usr/bin/env python3
"""Validate YAML documents with versioned Kubernetes OpenAPI schemas."""
import argparse
import sys

try:
    import kubernetes_validate
    import yaml
except ImportError as error:
    sys.stderr.write(
        "error: install the validator dependencies with "
        "'python3 -m pip install kubernetes-validate==1.37.0' (" + str(error) + ")\n"
    )
    raise SystemExit(2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="Kubernetes schema version, for example 1.37")
    parser.add_argument("file", nargs="?", default="-", help="YAML file to validate, or - for standard input")
    args = parser.parse_args()

    try:
        if args.file == "-":
            text = sys.stdin.read()
        else:
            with open(args.file, encoding="utf-8") as source:
                text = source.read()
        documents = [document for document in yaml.safe_load_all(text) if document is not None]
        if not documents:
            raise ValueError("input contains no YAML documents")
        for index, document in enumerate(documents, start=1):
            if not isinstance(document, dict):
                raise ValueError("document %d must be a Kubernetes object" % index)
            try:
                kubernetes_validate.validate(document, args.version, strict=True)
            except kubernetes_validate.ValidationError as error:
                raise ValueError("document %d: %s" % (index, error)) from error
    except (OSError, ValueError, yaml.YAMLError) as error:
        sys.stderr.write("error: " + str(error) + "\n")
        return 1

    print("validated %d Kubernetes document%s against version %s schemas" %
          (len(documents), "" if len(documents) == 1 else "s", args.version))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
