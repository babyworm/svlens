# Python API

Install locally with `python3 -m pip install .`, then point the wrapper at a
built `svlens` executable through `SVLENS_BINARY` or the `binary=` argument.
The package uses the CLI and JSON report contracts; it is not an in-process
pybind11 binding.

```python
import svlens

report = svlens.conn(["rtl/top.sv"], top="soc_top", binary="build/svlens")
print(report["summary"])

combined = svlens.all_modes(filelist="rtl/filelist.f", top="soc_top",
                            binary="build/svlens")
print(combined["cdc"]["crossings"])
```

An issue count can make the CLI exit nonzero while still producing a valid
report; these functions return that report. Compilation errors and missing
reports raise `svlens.SvlensError`.
