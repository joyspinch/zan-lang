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

The live oracle must match every checked-in `.expected` file. Zan failures are
accepted only for entries explicitly marked `xfail`; an unexpected pass fails the
run so the stale marker cannot become permanent. Temporary projects and binaries
are written under `_scratch/csharp-compat` and removed after the run.

## The gate runs the Zan side only

The `csharp_compat_oracle` ctest case runs the same runner with `--skip-csharp`:
the checked-in `.expected` files *are* the C# programs' measured output, so the
gate needs no .NET SDK. The rules are unchanged — a `pass` case must match
byte-for-byte, an `xfail` case must still not match (otherwise the runner reports
XPASS and the gate fails, so a stale xfail marker cannot become permanent).

```text
python tests/csharp_compat/run_oracle.py --zanc build/zanc.exe --skip-csharp
```

The live C# oracle still runs the same cases with `--dotnet`; run it whenever the
`.expected` files are refreshed, so the baseline never drifts from real C#.

