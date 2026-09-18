# C# compatibility oracle

This directory treats C# behavior as executable input, not as naming guidance.
Each case has matching `.cs` and `.zan` programs plus one checked-in `.expected`
file produced by the C# program. Observable output must match byte-for-byte after
line-ending normalization.

`manifest.json` is the compatibility ledger. `pass` means the Zan program must
match C# now. `xfail` records a confirmed semantic defect; it is not an alternate
expected result. A fix changes that entry to `pass` in the same commit.

Run the live C# oracle and the current Zan comparison:

```text
python tests/csharp_compat/run_oracle.py --dotnet dotnet --zanc build/zanc.exe
```

At a compatibility checkpoint, reject every remaining known red case:

```text
python tests/csharp_compat/run_oracle.py --dotnet dotnet --zanc build/zanc.exe --require-zan-pass
```

Temporary projects and executables are written under `_scratch/csharp-compat`
and removed after the run.
