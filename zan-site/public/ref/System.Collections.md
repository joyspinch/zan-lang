# System.Collections

> 源码: `stdlib/System/Collections/HashSet.zan`, `stdlib/System/Collections/LinkedList.zan`, `stdlib/System/Collections/Queue.zan`, `stdlib/System/Collections/Stack.zan`


## HashSet (class)

唯一项的集合，底层是动态 <c>List</c>，线性
扫描（`Add`、`Contains` 和
`Remove` 均为 O(n)；哈希存储有待通用的哈希设施）。

相等性：因为泛型元素类型在运行时会擦除为引用，
运行时按以下规则判断唯一性：
<list type="bullet">
<item>值类型（int、long、double、bool、enum）按值比较——必须精确相等；以及</item>
<item>引用类型（包括 string）按身份（同一实例）比较——</item>
两个内容相同但实例不同的字符串会被视为不同。</item>
</list>
目前如需基于内容的字符串集合，可用 <c>Dictionary</c> 以
字符串为键，其内建比较即为内容感知。（对引用元素做内容相等比较
需要泛型单态化。）

- List<T> items;

- HashSet()

- bool Add(T v)
  - 若尚不存在则添加该项；已添加返回 true，
    若已有相等项则返回 false。

- bool Contains(T v)
  - 是否存在相等的项。

- bool Remove(T v)
  - 移除相等的项（若存在），返回是否实际移除。

- int Count()
  - 不同项的数量。

- bool IsEmpty()
  - 集合是否为空。

- void Clear()
  - 清空所有项。

- T ElementAt(int i)
  - 按插入索引 <paramref name="i"/>（从 0 开始）取项，供
    枚举使用。顺序反映插入次序，移除后会自动压缩。


## LinkedList (class)

双向链表。两端插入和删除均为 O(1)。适用于
值类型（int、double、bool）及引用类型。可用
`Head` 与 `LinkedListNode{T}.Next` 遍历。

末端访问器（`First`、`Last`、
`RemoveFirst`、`RemoveLast`）要求列表非空，
请先用 `IsEmpty` 或 `Count` 判断。

- LinkedListNode<T> head;

- LinkedListNode<T> tail;

- int count;

- LinkedList()

- void AddLast(T v)
  - 在尾部追加项。O(1)。

- void AddFirst(T v)
  - 在头部插入项。O(1)。

- T RemoveFirst()
  - 移除并返回头部项。O(1)。

- T RemoveLast()
  - 移除并返回尾部项。O(1)。

- T First()
  - 返回头部项但不移除。

- T Last()
  - 返回尾部项但不移除。

- LinkedListNode<T> Head()
  - 头部节点（为空时返回 null），用于遍历。

- LinkedListNode<T> Tail()
  - 尾部节点（为空时返回 null），用于遍历。

- int Count()
  - 项的数量。

- bool IsEmpty()
  - 列表是否为空。

- void Clear()
  - 清空所有项。


## LinkedListNode (class)

`LinkedList{T}` 中的节点，暴露其相邻节点。

- T item;

- LinkedListNode<T> prev;

- LinkedListNode<T> next;

- LinkedListNode(T v)

- T Value()
  - 此节点存储的值。

- LinkedListNode<T> Next()
  - 下一个节点，尾部为 null。

- LinkedListNode<T> Prev()
  - 上一个节点，头部为 null。


## Queue (class)

先进先出（FIFO）集合。由带头尾指针的
单向链表实现，`Enqueue` 与 `Dequeue`
均为 O(1)。适用于值类型（int、double、bool）和引用
类型。

`Dequeue` 与 `Peek` 要求队列非空；
请先用 `IsEmpty` 或 `Count` 判断。

- QueueNode<T> head;

- QueueNode<T> tail;

- int count;

- Queue()

- void Enqueue(T v)
  - 在队尾追加项。O(1)。

- T Dequeue()
  - 移除并返回队首项。O(1)。

- T Peek()
  - 返回队首项但不移除。

- int Count()
  - 队列中的项数。

- bool IsEmpty()
  - 队列是否为空。

- void Clear()
  - 清空所有项。


## QueueNode (class)

`Queue{T}` 单向链表中的一个节点。

- T item;

- QueueNode<T> next;

- QueueNode(T v)


## Stack (class)

后进先出（LIFO）集合。由动态 <c>List</c> 实现，
`Push` 和 `Pop` 均摊 O(1)。适用于值
类型（int、double、bool）及引用类型。

`Pop` 与 `Peek` 要求栈非空；请
先用 `IsEmpty` 或 `Count` 判断。

- List<T> items;

- Stack()

- void Push(T v)
  - 将项压入栈顶。

- T Pop()
  - 移除并返回栈顶项。

- T Peek()
  - 返回栈顶项但不移除。

- int Count()
  - 栈中的项数。

- bool IsEmpty()
  - 栈是否为空。

- void Clear()
  - 清空所有项。

- bool Contains(T v)
  - 栈中是否包含 <paramref name="v"/>。值类型按值比较，
    引用类型（含 string）按身份比较——参见
    <c>HashSet</c> 中的说明。
