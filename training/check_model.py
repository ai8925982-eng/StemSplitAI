"""Validate an ONNX model against the StemSplit AI contract."""
import sys
import numpy as np
import onnxruntime as ort

if len(sys.argv) != 2:
    sys.exit("usage: python check_model.py model.onnx")

sess = ort.InferenceSession(sys.argv[1], providers=["CPUExecutionProvider"])
i, o = sess.get_inputs()[0], sess.get_outputs()[0]
print("input :", i.name, i.shape, i.type)
print("output:", o.name, o.shape, o.type)

assert len(i.shape) == 3 and i.shape[1] in (2, "2"), "input must be [1,2,T]"
T = i.shape[2] if isinstance(i.shape[2], int) else 343980
y = sess.run(None, {i.name: (np.random.randn(1, 2, T) * 0.1).astype("float32")})[0]
assert y.shape == (1, 30, 2, T), f"output must be (1,30,2,{T}), got {y.shape}"
meta = sess.get_modelmeta().custom_metadata_map
print("sample_rate metadata:", meta.get("sample_rate", "(missing -> 44100 assumed)"))
print("OK - model matches the contract")
