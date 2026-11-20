"""Exercise lexical and project-wide binding navigation through zanc."""

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
    static void Patterns(object source) {
        int matched = 0;
        if (source is Navigation matched) { object witness = matched; }
        Navigation casted = source as Navigation;
        matched++;
    }
    static int Deconstruct(int input) {
        var (left, right) = (input, 2);
        return left + right;
    }
    static int Handler() {
        int caught = 0;
        try { caught++; }
        catch (Navigation caught) { object witness = caught; }
        return caught;
    }
}
delegate int IntOp(int value);
"""


def run(zanc, *args, cwd):
    return subprocess.run([zanc, *args], cwd=cwd, capture_output=True,
                          timeout=30, check=False)


def location(path, line, fragment, name):
    text = path.read_bytes().splitlines()[line - 1]
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
    expect("parameter", "source", loc(21, "object source", "source"), [
        loc(23, "source is", "source"), loc(24, "source as", "source"),
    ])
    expect("local", "matched", loc(23, "Navigation matched", "matched"), [
        loc(23, "= matched", "matched"),
    ])
    expect("local", "matched", loc(22, "int matched", "matched"), [
        loc(25, "matched++", "matched"),
    ])
    expect("parameter", "input", loc(27, "int input", "input"), [
        loc(28, "(input,", "input"),
    ])
    expect("local", "left", loc(28, "(left,", "left"), [loc(29, "return left", "left")])
    expect("local", "caught", loc(32, "int caught", "caught"), [
        loc(33, "caught++", "caught"), loc(35, "return caught", "caught"),
    ])
    expect("local", "caught", loc(34, "Navigation caught", "caught"), [
        loc(34, "= caught", "caught"),
    ])
    empty = {"format": FORMAT, "symbol": None, "references": []}
    expect("field", "value", loc(2, "int value", "value"), [
        loc(13, "Navigation.value", "value"),
    ])
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


def check_project(zanc, work):
    alpha = work / "Alpha.zan"
    part = work / "Alpha.Part.zan"
    beta = work / "Beta.zan"
    caller = work / "调用.zan"
    alpha.write_text("""namespace Alpha;
partial class Meter {
    public int amount;
    public int Read(int number) { return amount + number; }
}
""", encoding="utf-8")
    part.write_text("""namespace Alpha;
partial class Meter {
    public int Read(string text) { return this.amount; }
}
""", encoding="utf-8")
    beta.write_text("""namespace Beta;
class Meter {
    public int amount;
    public int Read(int number) { return this.amount; }
}
""", encoding="utf-8")
    caller.write_text("""using Alpha;
class Child : Alpha.Meter {
    int Total(Meter left, Beta.Meter right) {
        return /* 中😀 */ left.Read(2) + left.Read("x") + right.Read(2)
            + left.amount + right.amount + this.amount;
    }
}
""", encoding="utf-8")
    inputs = (str(alpha), str(part), str(beta), str(caller), "--no-stdlib", "--no-packages")

    def query(at):
        result = run(zanc, *inputs, FLAG, at["file"], str(at["line"]), str(at["col"]), cwd=work)
        assert result.returncode == 0, result
        return json.loads(result.stdout.decode("utf-8"))

    def expect(kind, name, declaration, refs, parts=None):
        symbol = {"kind": kind, "name": name, "declaration": declaration}
        if parts:
            symbol["additionalDeclarations"] = parts
        expected = {"format": FORMAT, "symbol": symbol, "references": refs}
        for occurrence in [declaration, *refs, *(parts or [])]:
            actual = query(occurrence)
            assert actual == expected, (occurrence, expected, actual)

    expect("class", "Meter", location(alpha, 2, "class Meter", "Meter"), [
        location(caller, 2, "Alpha.Meter", "Meter"),
        location(caller, 3, "Meter left", "Meter"),
    ], [location(part, 2, "class Meter", "Meter")])
    expect("class", "Meter", location(beta, 2, "class Meter", "Meter"), [
        location(caller, 3, "Beta.Meter", "Meter"),
    ])
    expect("field", "amount", location(alpha, 3, "int amount", "amount"), [
        location(alpha, 4, "return amount", "amount"),
        location(part, 3, "this.amount", "amount"),
        location(caller, 5, "left.amount", "amount"),
        location(caller, 5, "this.amount", "amount"),
    ])
    expect("field", "amount", location(beta, 3, "int amount", "amount"), [
        location(beta, 4, "this.amount", "amount"),
        location(caller, 5, "right.amount", "amount"),
    ])
    expect("method", "Read", location(alpha, 4, "Read(int", "Read"), [
        location(caller, 4, "left.Read(2)", "Read"),
    ])
    expect("method", "Read", location(part, 3, "Read(string", "Read"), [
        location(caller, 4, 'left.Read("x")', "Read"),
    ])
    expect("method", "Read", location(beta, 4, "Read(int", "Read"), [
        location(caller, 4, "right.Read(2)", "Read"),
    ])
    assert not list(work.glob("*.exe"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zanc", type=Path, required=True)
    args = parser.parse_args()
    scratch = Path(__file__).resolve().parents[1] / "_scratch"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="zanc-semantic-refs-", dir=scratch) as temp:
        check(str(args.zanc.resolve()), Path(temp))
        check_project(str(args.zanc.resolve()), Path(temp))
    print("zanc semantic references wire contract passed")
