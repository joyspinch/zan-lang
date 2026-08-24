# System.Collections.Generic

> 源码: `stdlib/System/Collections/Generic/KeyValuePair.zan`


## KVP (struct)

<c>KeyValuePair</c> 的简写别名：<c>KVP(K, V)</c> 与
<c>KeyValuePair<K, V></c> 是同一个结构体。

- private K _key;

- private V _value;

- public KVP(K key, V value)
  - 用一个键和一个值构造键值对。

- public K Key { get }
  - 键。

- public V Value { get }
  - 值。


## KeyValuePair (struct)

一个键/值对，C# 的 <c>KeyValuePair<K, V></c> 等价物：结构体，只读
<c>Key</c> / <c>Value</c> 属性，构造函数一次性赋值。用于字典条目迭代
与通用键值序列（<c>KVP(K, V)</c> 是其简写）。

KeyValuePair<string, int> kv = new KeyValuePair<string, int>("a", 1);
Console.WriteLine(kv.Key);   // a
Console.WriteLine(kv.Value); // 1

- private K _key;

- private V _value;

- public KeyValuePair(K key, V value)
  - 用一个键和一个值构造键值对。

- public K Key { get }
  - 键。

- public V Value { get }
  - 值。
