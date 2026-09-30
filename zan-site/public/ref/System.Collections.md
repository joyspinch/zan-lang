# System.Collections

> 源码: `stdlib/System/Collections/CollectionsCompat.zan`


## HashSet (class)

向后兼容转发：指向 System.Collections.Generic.HashSet{T}。

- System.Collections.Generic.HashSet<T> _inner;

- HashSet()

- bool Add(T item)

- bool Remove(T item)

- bool Contains(T item)

- void Clear()

- int Count()

- List<T> Keys()

- T ElementAt(int i)


## Queue (class)

向后兼容转发：指向 System.Collections.Generic.Queue{T}。

- System.Collections.Generic.Queue<T> _inner;

- Queue()

- void Enqueue(T v)

- T Dequeue()

- T Peek()

- int Count()

- bool IsEmpty()

- void Clear()


## Stack (class)

向后兼容转发：指向 System.Collections.Generic.Stack{T}。

- System.Collections.Generic.Stack<T> _inner;

- Stack()

- void Push(T v)

- T Pop()

- T Peek()

- int Count()

- bool IsEmpty()

- void Clear()
