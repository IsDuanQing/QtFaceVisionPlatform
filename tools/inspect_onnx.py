#!/usr/bin/env python3
"""Print ONNX graph inputs and outputs for TensorRT integration checks."""

import argparse
import sys


def shape_of(value_info):
    dims = []
    tensor_type = value_info.type.tensor_type
    for dim in tensor_type.shape.dim:
        if dim.dim_value:
            dims.append(str(dim.dim_value))
        elif dim.dim_param:
            dims.append(dim.dim_param)
        else:
            dims.append("?")
    return "[" + ", ".join(dims) + "]"


def elem_type_of(value_info, onnx):
    elem_type = value_info.type.tensor_type.elem_type
    return onnx.TensorProto.DataType.Name(elem_type)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("onnx_path", help="Path to the ONNX model")
    args = parser.parse_args()

    try:
        import onnx
    except ImportError:
        print(
            "Missing dependency: onnx. Install it in a Python 3.8+ environment "
            "with: pip install onnx",
            file=sys.stderr,
        )
        return 2

    model = onnx.load(args.onnx_path)
    onnx.checker.check_model(model)

    print("Inputs:")
    for value_info in model.graph.input:
        print(
            f"  name={value_info.name} "
            f"dtype={elem_type_of(value_info, onnx)} "
            f"shape={shape_of(value_info)}"
        )

    print("Outputs:")
    for value_info in model.graph.output:
        print(
            f"  name={value_info.name} "
            f"dtype={elem_type_of(value_info, onnx)} "
            f"shape={shape_of(value_info)}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
