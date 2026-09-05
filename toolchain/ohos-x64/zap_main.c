/* HAP shell entry: the ArkTS/XComponent native module dlopens libmain.so
 * and calls zan_hap_main() on a worker thread; adapts to the module main,
 * exactly like SDL_main.o does for the APK shell. */
int main(int argc, char **argv);

int zan_hap_main(void) {
    char *arg0 = "zan";
    char *argv[1] = { arg0 };
    return main(1, argv);
}
