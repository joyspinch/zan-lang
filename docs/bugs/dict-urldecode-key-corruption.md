# Dictionary keys corrupted when built from Split + Substring + UrlDecode

**Status:** Fixed (2026-07-27) — A8-2 (use-after-free / heap corruption). See TASKS.md A8-2.

## Summary

Building a `Dictionary<string, string>` whose keys come from
`Encoding.UrlDecode(item.Substring(...))`, where `item` is an element of the
`List<string>` returned by `string.Split(...)`, corrupts some of the stored
keys. Reading `dict.Keys` afterwards yields garbage bytes for a subset of the
entries, and a later pass over those keys crashes (exit 127 / SIGSEGV).

The individual pieces are each fine; only the full combination fails:

- `Split` + `Substring` + `UrlDecode` collected into a `List<string>` — OK
- `UrlDecode` results added to a `Dictionary` directly — OK
- `Substring` temporaries used as `Dictionary` keys — OK
- `Split` element → `Substring` → `UrlDecode` → `Dictionary` key — **corrupt**

This looks like the decoded key string being freed (or its buffer reused)
while the dictionary still references it, i.e. a missing retain on the key
path when the key is a temporary derived from a `Split` element.

## Minimal repro

`docs/bugs/repro_dict_urldecode_key.zan`:

```zan
using System;
using System.Text;

class Program {
    static void Main() {
        string body = "360buy_param_json=%7b%22a%22%3a%22b%22%7d&method=jingdong.mock.ware.get&app_key=test-appkey&v=2.0&timestamp=2026-07-26+22%3a00%3a00%2b0800&access_token=tok-123&sign=ABC";
        Dictionary<string, string> f = new Dictionary<string, string>();
        List<string> pairs = body.Split("&");
        int i = 0;
        while (i < pairs.Count) {
            string p = pairs[i];
            int eq = p.IndexOf("=");
            if (eq > 0) {
                string k = Encoding.UrlDecode(p.Substring(0, eq));
                string v = Encoding.UrlDecode(p.Substring(eq + 1));
                if (!f.ContainsKey(k)) { f.Add(k, v); }
            }
            i = i + 1;
        }
        List<string> ks = f.Keys;
        int j = 0;
        while (j < ks.Count) {
            Console.WriteLine("k[" + Convert.ToString(j) + "]=[" + ks[j] + "]");
            j = j + 1;
        }
    }
}
```

Observed (keys 2 and 3 corrupted, then exit 127):

```
k[0]=[360buy_param_json]
k[1]=[method]
k[2]=[@<garbage>]
k[3]=[]
k[4]=[timestamp]
k[5]=[access_token]
k[6]=[sign]
```

Expected: all seven keys read back intact.

Note the corruption is data dependent — fewer or shorter pairs often survive,
which is consistent with a reuse-after-free rather than an off-by-one.

## Impact

Any code parsing an `application/x-www-form-urlencoded` body (or a URL query)
into a dictionary in this idiomatic way. Found while writing a mock JD gateway
for `tests/conformance/sdk_jd_client.zan`; the SDK itself is unaffected
because it only signs dictionaries it builds locally.
