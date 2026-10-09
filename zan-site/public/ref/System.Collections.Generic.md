# System.Collections.Generic

> 源码: `stdlib/System/Collections/Generic/HashSet.zan`, `stdlib/System/Collections/Generic/IEnumerable.zan`, `stdlib/System/Collections/Generic/IEnumerator.zan`, `stdlib/System/Collections/Generic/KeyValuePair.zan`, `stdlib/System/Collections/Generic/LinkedList.zan`, `stdlib/System/Collections/Generic/Queue.zan`, `stdlib/System/Collections/Generic/Stack.zan`


## HashSet (class)

- Dict <T, int> idx;

- List<T> order;

- List<bool> dead;

- int live;

- HashSet()

- bool Add(T v)

- bool Contains(T v)

- bool Remove(T v)

- void Compact()

- int Count()

- bool IsEmpty()

- void Clear()

- List<T> Keys()

- T ElementAt(int i)


## LinkedList (class)

- LinkedListNode<T> head;

- LinkedListNode<T> tail;

- int count;

- LinkedList()

- void AddLast(T v)

- void AddFirst(T v)

- T RemoveFirst()

- T RemoveLast()

- T First()

- T Last()

- LinkedListNode<T> Head()

- LinkedListNode<T> Tail()

- int Count()

- bool IsEmpty()

- void Clear()


## LinkedListNode (class)

- T item;

- LinkedListNode<T> prev;

- LinkedListNode<T> next;

- LinkedListNode(T v)

- T Value()

- LinkedListNode<T> Next()

- LinkedListNode<T> Prev()


## Queue (class)

- QueueNode<T> head;

- QueueNode<T> tail;

- int count;

- Queue()

- void Enqueue(T v)

- T Dequeue()

- T Peek()

- int Count()

- bool IsEmpty()

- void Clear()


## QueueNode (class)

- T item;

- QueueNode<T> next;

- QueueNode(T v)


## Stack (class)

- List<T> items;

- Stack()

- void Push(T v)

- T Pop()

- T Peek()

- int Count()

- bool IsEmpty()

- void Clear()

- bool Contains(T v)


## IEnumerable (interface)

- IEnumerator<T> GetEnumerator();


## IEnumerator (interface)

- bool MoveNext();

- T Current();


## KVP (struct)

- private K _key;

- private V _value;

- public KVP(K key, V value)

- public K Key { get }

- public V Value { get }


## KeyValuePair (struct)

- private K _key;

- private V _value;

- public KeyValuePair(K key, V value)

- public K Key { get }

- public V Value { get }
