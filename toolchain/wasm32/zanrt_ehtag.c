/* Defines the __cpp_exception tag (the backend only declares it as an import)
 * and a real __cxa_throw the compiler's raised invokes link against: the wasm
 * backend does not rewrite __cxa_throw into the `throw` instruction, so the
 * raise must happen in C via __builtin_wasm_throw and unwind through the
 * caller's invoke edge. */
void __cxa_throw(void *obj, void *tinfo, void *dest) {
    (void)tinfo;
    (void)dest;
    __builtin_wasm_throw(0, obj);
}
