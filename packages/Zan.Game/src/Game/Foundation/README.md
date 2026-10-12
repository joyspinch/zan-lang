# Game.Foundation

`Game.Foundation` contains renderer-neutral building blocks shared by board,
card, tactics, puzzle, idle and lightweight arcade games:

- deterministic fixed-step timing and replay-safe random numbers;
- semantic input actions with multiple key bindings;
- one-shot and repeating timers;
- a stack-based scene lifecycle.

GUI runtime host integration lives in `Game.Foundation.Gui`, keeping headless
simulations and servers independent from the windowing driver.
