# Namespace: Sample.Docs

## class Widget

A widget that does something.

### Methods

#### `Widget Create(int size)`

Creates a widget of the given size.

**Returns:** The new widget.

#### `Widget w = new Widget();`

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

---

