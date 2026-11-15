# Namespace: Sample.Docs

## class Widget

A widget that does something.

### Methods

#### `Widget Create(int size)`

Creates a widget of the given size.

**Returns:** The new widget.

#### `int Size() { return this.size; }`

The current size.

#### `void Grow(int by) { this.size = this.size + by; }`

---

## struct Point

A point in the plane.

### Methods

#### `int Norm() { return this.x + this.y; }`

Manhattan distance from the origin.

---

## interface IDrawable

What a widget can be asked to do.

### Methods

#### `void Draw();`

Renders the drawable.

---

## class Rule

规则定义（多行 summary 块）。

### Methods

#### `Rule Create()`

Creates an empty rule.

#### `bool HasName() { return this.names.Count > 0; }`

True when the rule has a name.

#### `Rule Join( int seed, bool strict )`

---

