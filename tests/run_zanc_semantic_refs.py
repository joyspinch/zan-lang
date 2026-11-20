"""Exercise the C checker's local and parameter navigation through zanc."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile


FORMAT = "zan.semantic-refs.v1"
FLAG = "--semantic-refs"
SOURCE = """class Navigation {
    static int value = 3;
    static void Set(out int result) { result = 2; }
    static int Read(int value) {
        int first = value, second = first;
        { int first = second; first += 1; }
        for (int i = 0; i < 1; i++) { first += i; }
        int[] items = new int[1];
        foreach (int item in items) { first += item; }
        Set(out int created);
        first = first + created;
        first += /* 中 */ value;
        return first + value + Navigation.value;
    }
    static int Other(int value) { return value; }
    static void WithLambda(int value, int unused) {
        var captured = value;
        IntOp add = value => value + captured;
        captured = add(value);
    }
}
delegate int IntOp(int value);
"""


def run(zanc, *args, cwd):
    return subprocess.run([zanc, *args], cwd=cwd, capture_output=True,
                          timeout=30, check=False)


def location(path, line, fragment, name):
    text = SOURCE.splitlines()[line - 1].encode("utf-8")
    fragment = fragment.encode("utf-8")
    token = name.encode("utf-8")
    col = text.index(fragment) + fragment.index(token) + 1
    return {"file": str(path), "line": line, "col": col, "length": len(token)}


def check(zanc, work):
    help_result = run(zanc, "--help", cwd=work)
    assert help_result.returncode == 0, help_result
    assert any(line.split()[:1] == [FLAG.encode()]
               for stream in (help_result.stdout, help_result.stderr)
               for line in stream.splitlines()), help_result

    path = work / "导航 example.zan"
    path.write_bytes(SOURCE.replace("\n", "\r\n").encode("utf-8"))
    other = work / "Other.zan"
    other.write_text("class Extra { static int Read(int value) { return value; } }\n",
                     encoding="utf-8")
    output = work / "would-be-output.exe"
    inputs = (str(other), str(path), "--no-stdlib", "--no-packages", "-o", str(output))

    def loc(line, fragment, name):
        return location(path, line, fragment, name)

    def query(at, offset=0):
        # The requested file can use a different spelling from the loaded input.
        result = run(zanc, *inputs, FLAG, path.name, str(at["line"]),
                     str(at["col"] + offset), cwd=work)
        assert result.returncode == 0, result
        assert not output.exists(), output
        return json.loads(result.stdout.decode("utf-8"))

    def expect(kind, name, declaration, references):
        expected = {"format": FORMAT, "symbol": {
            "kind": kind, "name": name, "declaration": declaration,
        }, "references": references}
        assert query(declaration) == expected, expected
        if references:
            assert query(references[-1], len(name) - 1) == expected, expected

    expect("parameter", "value", loc(4, "int value", "value"), [
        loc(5, "= value", "value"), loc(12, "*/ value", "value"),
        loc(13, "+ value", "value"),
    ])
    expect("local", "first", loc(5, "int first", "first"), [
        loc(5, "= first;", "first"), loc(7, "first += i", "first"),
        loc(9, "first += item", "first"), loc(11, "first =", "first"),
        loc(11, "= first", "first"), loc(12, "first +=", "first"),
        loc(13, "return first", "first"),
    ])
    expect("local", "second", loc(5, "second =", "second"), [
        loc(6, "= second", "second"),
    ])
    expect("local", "first", loc(6, "int first", "first"), [
        loc(6, "first +=", "first"),
    ])
    expect("local", "i", loc(7, " i =", "i"), [
        loc(7, "i <", "i"), loc(7, "i++", "i"), loc(7, "+= i;", "i"),
    ])
    expect("local", "item", loc(9, "int item", "item"), [loc(9, "+= item", "item")])
    expect("local", "created", loc(10, "int created", "created"), [
        loc(11, "+ created", "created"),
    ])
    expect("parameter", "value", loc(15, "int value", "value"), [
        loc(15, "return value", "value"),
    ])
    expect("parameter", "value", loc(16, "int value", "value"), [
        loc(17, "= value", "value"), loc(19, "add(value)", "value"),
    ])
    expect("parameter", "unused", loc(16, "int unused", "unused"), [])
    expect("parameter", "value", loc(18, "value =>", "value"), [
        loc(18, "=> value", "value"),
    ])
    expect("local", "captured", loc(17, "var captured", "captured"), [
        loc(18, "+ captured", "captured"), loc(19, "captured =", "captured"),
    ])
    expect("local", "add", loc(18, "add =", "add"), [loc(19, "add(value)", "add")])
    empty = {"format": FORMAT, "symbol": None, "references": []}
    assert query(loc(13, "Navigation.value", "value")) == empty
    assert query(loc(13, "return first", "first"), len("first")) == empty

    for args in ((FLAG,), (FLAG, path.name, "0", "1"),
                 (FLAG, path.name, "1", "4294967296"),
                 (FLAG, "missing.zan", "1", "1"),
                 (FLAG, path.name, "1", "1", "--dump-ast")):
        invalid = run(zanc, *inputs, *args, cwd=work)
        assert invalid.returncode != 0 and not invalid.stdout, invalid
        assert FLAG.encode() in invalid.stderr, invalid

    other.write_text("class Extra { static int Bad() { return \"wrong\"; } }\n",
                     encoding="utf-8")
    invalid = run(zanc, *inputs, FLAG, path.name, "4", "25",
                  "--diagnostics-format=jsonl", cwd=work)
    assert invalid.returncode != 0 and not invalid.stdout, invalid
    assert b"zan-diagnostic-v1" in invalid.stderr and not output.exists(), invalid


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zanc", type=Path, required=True)
    args = parser.parse_args()
    scratch = Path(__file__).resolve().parents[1] / "_scratch"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="zanc-semantic-refs-", dir=scratch) as temp:
        check(str(args.zanc.resolve()), Path(temp))
    print("zanc semantic references wire contract passed")
