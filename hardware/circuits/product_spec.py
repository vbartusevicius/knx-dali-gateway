import copy
import os
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[2]
BASE_PATH = REPO / "hardware/product/spec.yaml"
DEFAULT_VARIANT = "v3"


def spec_path():
    variant = os.environ.get("HARDWARE_VARIANT", DEFAULT_VARIANT)
    path = REPO / "hardware/circuits/variants" / variant / "spec.yaml"
    if not variant.isidentifier() or not path.exists():
        raise ValueError(f"Invalid hardware variant: {variant}")
    return path


def merge(base, overrides):
    result = copy.deepcopy(base)
    for key, value in overrides.items():
        if isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = merge(result[key], value)
        else:
            result[key] = copy.deepcopy(value)
    return result


def load_spec():
    return merge(yaml.safe_load(BASE_PATH.read_text()), yaml.safe_load(spec_path().read_text()))
