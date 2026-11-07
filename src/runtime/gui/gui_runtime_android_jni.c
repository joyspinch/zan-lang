/* gui_runtime_android_jni.c -- Android JNI 桥接、输入法与系统剪贴板通道 */

/* WebView 桥接 JNI 线程环境与上下文访问 */
static JNIEnv *zan_anw_bridge_env(void) {
    struct android_app *app = g_anw.app;
    if (!app || !app->activity) return NULL;
    JavaVM *vm = app->activity->vm;
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK)
        return env;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) == JNI_OK)
        return env;
    return NULL;
}

static jobject zan_anw_bridge_activity(void) {
    struct android_app *app = g_anw.app;
    return app && app->activity ? (jobject)app->activity->clazz : NULL;
}

/* 当前 Display 模式的 Hz；Surface/恢复后重查，其余最多每秒一次 JNI。 */
EXPORT double zan_gui_refresh_rate(iptr hwnd_val) {
    (void)hwnd_val;
    i64 now = ant_tick_ms();
    if (g_refresh_valid && now - g_refresh_checked_ms < 1000) return g_refresh_hz;
    g_refresh_checked_ms = now;
    g_refresh_valid = 1;
    g_refresh_hz = 60.0;
    struct android_app *app = g_anw.app;
    if (!app || !app->activity || !app->activity->vm) return g_refresh_hz;
    JavaVM *vm = app->activity->vm;
    JNIEnv *prior = NULL;
    int detach = (*vm)->GetEnv(vm, (void **)&prior, JNI_VERSION_1_6) == JNI_EDETACHED;
    JNIEnv *env = zan_anw_bridge_env();
    if (!env) return g_refresh_hz;
    int framed = (*env)->PushLocalFrame(env, 16) == JNI_OK;
    if (!framed) goto done;
    jobject activity = (jobject)app->activity->clazz;
    jclass activity_class = (*env)->GetObjectClass(env, activity);
    if (!activity_class) goto done;
    jmethodID get_manager = (*env)->GetMethodID(env, activity_class, "getWindowManager",
                                               "()Landroid/view/WindowManager;");
    if (!get_manager) goto done;
    jobject manager = (*env)->CallObjectMethod(env, activity, get_manager);
    if ((*env)->ExceptionCheck(env) || !manager) goto done;
    jclass manager_class = (*env)->GetObjectClass(env, manager);
    if (!manager_class) goto done;
    jmethodID get_display = (*env)->GetMethodID(env, manager_class, "getDefaultDisplay",
                                               "()Landroid/view/Display;");
    if (!get_display) goto done;
    jobject display = (*env)->CallObjectMethod(env, manager, get_display);
    if ((*env)->ExceptionCheck(env) || !display) goto done;
    jclass display_class = (*env)->GetObjectClass(env, display);
    if (!display_class) goto done;
    jmethodID get_rate = (*env)->GetMethodID(env, display_class, "getRefreshRate", "()F");
    if (!get_rate) goto done;
    jfloat hz = (*env)->CallFloatMethod(env, display, get_rate);
    if (!(*env)->ExceptionCheck(env) && hz > 1.0f && hz <= 1000.0f)
        g_refresh_hz = (double)hz;
 done:
    /* 可选显示查询失败按契约回退，不能留下异常污染后续 JNI。 */
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (framed) (*env)->PopLocalFrame(env, NULL);
    if (detach) (*vm)->DetachCurrentThread(vm);
    return g_refresh_hz;
}

/* JNI 剪贴板与输入法 (IME) 访问通道 */

/* JNI 字符串转换：UTF-16 转 UTF-8 (支持代理对合并) */
static char *ant_utf16_to_utf8(const jchar *in, jsize n) {
    char *out = (char *)malloc((size_t)n * 4 + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (jsize i = 0; i < n; ) {
        unsigned cp = in[i++];
        if (cp >= 0xD800 && cp <= 0xDBFF && i < n &&
            in[i] >= 0xDC00 && in[i] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (unsigned)(in[i++] - 0xDC00);
        }
        if (cp < 0x80) {
            out[o++] = (char)cp;
        } else if (cp < 0x800) {
            out[o++] = (char)(0xC0 | (cp >> 6));
            out[o++] = (char)(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            out[o++] = (char)(0xE0 | (cp >> 12));
            out[o++] = (char)(0x80 | ((cp >> 6) & 63));
            out[o++] = (char)(0x80 | (cp & 63));
        } else {
            out[o++] = (char)(0xF0 | (cp >> 18));
            out[o++] = (char)(0x80 | ((cp >> 12) & 63));
            out[o++] = (char)(0x80 | ((cp >> 6) & 63));
            out[o++] = (char)(0x80 | (cp & 63));
        }
    }
    out[o] = 0;
    return out;
}

/* UTF-8 转 UTF-16 (非法字节替换为 U+FFFD) */
static jchar *ant_utf8_to_utf16(const char *in, jsize *out_len) {
    size_t n = strlen(in);
    jchar *out = (jchar *)malloc((n + 1) * sizeof(jchar));
    if (!out) return NULL;
    size_t i = 0, o = 0;
    while (i < n) {
        unsigned char b = (unsigned char)in[i];
        unsigned cp; int extra;
        if (b < 0x80) { cp = b; extra = 0; i += 1; }
        else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; extra = 1; i += 1; }
        else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; extra = 2; i += 1; }
        else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; extra = 3; i += 1; }
        else { out[o++] = 0xFFFD; i += 1; continue; }
        int ok = 1;
        for (int k = 0; k < extra; k++) {
            if (i >= n || (in[i] & 0xC0) != 0x80) { ok = 0; break; }
            cp = (cp << 6) | (unsigned char)(in[i++] & 0x3F);
        }
        if (!ok || (extra == 0 && cp > 0x7F) ||
            (extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
            (extra == 3 && cp < 0x10000) || cp > 0x10FFFF) {
            out[o++] = 0xFFFD;
            continue;
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out[o++] = (jchar)(0xD800 + (cp >> 10));
            out[o++] = (jchar)(0xDC00 + (cp & 0x3FF));
        } else {
            out[o++] = (jchar)cp;
        }
    }
    *out_len = (jsize)o;
    return out;
}

static jclass g_ime_cls;
static jmethodID g_ime_show, g_ime_hide;
static void ant_ime_commit(JNIEnv *env, jclass clazz, jstring text);
static void ant_ime_set_composing(JNIEnv *env, jclass clazz, jstring text);

static char g_composing[256];
static size_t g_composing_len;

static void ant_composing_store(const char *utf8) {
    pthread_mutex_lock(&g_aq_lock);
    if (utf8) {
        size_t n = strlen(utf8);
        if (n >= sizeof(g_composing)) n = sizeof(g_composing) - 1;
        memcpy(g_composing, utf8, n);
        g_composing[n] = 0;
        g_composing_len = n;
    } else {
        g_composing[0] = 0;
        g_composing_len = 0;
    }
    aq_push_locked(14, 0, 0, 0, 0, 0);
    pthread_mutex_unlock(&g_aq_lock);
}

static int ant_ime_init(JNIEnv *env, jobject act) {
    static int done = -1; /* -1 untried, 0 ok, 1 failed */
    if (done == 0) return 0;
    if (done == 1) return -1;
    jclass aclazz = (*env)->GetObjectClass(env, act);
    jmethodID gcl = (*env)->GetMethodID(env, aclazz, "getClassLoader",
                                        "()Ljava/lang/ClassLoader;");
    (*env)->DeleteLocalRef(env, aclazz);
    if (!gcl) goto fail;
    jobject loader = (*env)->CallObjectMethod(env, act, gcl);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); goto fail; }
    jclass lclazz = (*env)->GetObjectClass(env, loader);
    jmethodID load = (*env)->GetMethodID(env, lclazz, "loadClass",
                                         "(Ljava/lang/String;)Ljava/lang/Class;");
    (*env)->DeleteLocalRef(env, lclazz);
    if (!load) { (*env)->DeleteLocalRef(env, loader); goto fail; }
    jstring name = (*env)->NewStringUTF(env, "dev.zan.app.ZanIme");
    jclass cls = (jclass)(*env)->CallObjectMethod(env, loader, load, name);
    (*env)->DeleteLocalRef(env, loader);
    (*env)->DeleteLocalRef(env, name);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); goto fail; }
    if (!cls) goto fail;
    g_ime_show = (*env)->GetStaticMethodID(env, cls, "show",
                                           "(Landroid/app/Activity;)V");
    g_ime_hide = (*env)->GetStaticMethodID(env, cls, "hide",
                                           "(Landroid/app/Activity;)V");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); }
    if (!g_ime_show || !g_ime_hide) {
        (*env)->DeleteLocalRef(env, cls);
        goto fail;
    }
    g_ime_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);
    if (!g_ime_cls) goto fail;
    /* 动态注册 JNI zanCommit 本地回调方法 */
    {
        static const JNINativeMethod k_methods[] = {
            { "zanCommit", "(Ljava/lang/String;)V", (void *)&ant_ime_commit },
            { "zanSetComposing", "(Ljava/lang/String;)V",
              (void *)&ant_ime_set_composing },
        };
    if ((*env)->RegisterNatives(env, g_ime_cls, k_methods, 2) != 0 ||
        (*env)->ExceptionCheck(env)) {
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        ZAN_LOG("RegisterNatives FAILED");
        goto fail;
    }
    ZAN_LOG("RegisterNatives ok (commit+setComposing)");
    }
    done = 0;
    return 0;
fail:
    done = 1;
    return -1;
}

static void ant_ime_commit(JNIEnv *env, jclass clazz, jstring text) {
    (void)clazz;
    if (!env || !text) return;
    jsize n = (*env)->GetStringLength(env, text);
    const jchar *chars = (*env)->GetStringChars(env, text, NULL);
    if (!chars) return;
    pthread_mutex_lock(&g_aq_lock);
    for (jsize i = 0; i < n; ) {
        unsigned cp = chars[i++];
        if (cp >= 0xD800 && cp <= 0xDBFF && i < n &&
            chars[i] >= 0xDC00 && chars[i] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (unsigned)(chars[i++] - 0xDC00);
        }
        aq_push_locked(6, 0, 0, 0, (i32)cp, 0);
    }
    g_composing[0] = 0;
    g_composing_len = 0;
    pthread_mutex_unlock(&g_aq_lock);
    (*env)->ReleaseStringChars(env, text, chars);
}

static void ant_ime_set_composing(JNIEnv *env, jclass clazz, jstring text) {
    (void)clazz;
    if (!env) return;
    if (!text) { ant_composing_store(NULL); return; }
    jsize n = (*env)->GetStringLength(env, text);
    const jchar *chars = (*env)->GetStringChars(env, text, NULL);
    if (!chars) { ant_composing_store(NULL); return; }
    char *utf8 = ant_utf16_to_utf8(chars, n);
    (*env)->ReleaseStringChars(env, text, chars);
    if (!utf8) return;
    ant_composing_store(utf8);
    free(utf8);
}

JNIEXPORT void JNICALL
Java_dev_zan_app_ZanIme_zanSetComposing(JNIEnv *env, jclass clazz,
                                        jstring text) {
    ant_ime_set_composing(env, clazz, text);
}

JNIEXPORT void JNICALL
Java_dev_zan_app_ZanIme_zanCommit(JNIEnv *env, jclass clazz, jstring text) {
    ant_ime_commit(env, clazz, text);
}

EXPORT i32 zan_gui_set_clipboard(const char *utf8) {
    JNIEnv *env = zan_anw_bridge_env();
    jobject ctx = zan_anw_bridge_activity();
    if (!env || !ctx || !utf8) return 1;
    jsize len = 0;
    jchar *u16 = ant_utf8_to_utf16(utf8, &len);
    if (!u16) return 1;
    jstring text = (*env)->NewString(env, u16, len);
    free(u16);
    if (!text) { if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env); return 1; }
    jclass cctx = (*env)->FindClass(env, "android/content/Context");
    jmethodID getService = cctx ? (*env)->GetMethodID(env, cctx, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;") : NULL;
    jstring name = getService ? (*env)->NewStringUTF(env, "clipboard") : NULL;
    jobject cm = getService
        ? (*env)->CallObjectMethod(env, ctx, getService, name) : NULL;
    if (name) (*env)->DeleteLocalRef(env, name);
    if (cctx) (*env)->DeleteLocalRef(env, cctx);
    if ((*env)->ExceptionCheck(env) || !cm) {
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, text);
        return 1;
    }
    jclass cd = (*env)->FindClass(env, "android/content/ClipData");
    jmethodID npt = cd ? (*env)->GetStaticMethodID(env, cd, "newPlainText",
        "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)"
        "Landroid/content/ClipData;") : NULL;
    jstring label = npt ? (*env)->NewStringUTF(env, "zan") : NULL;
    jobject clip = npt
        ? (*env)->CallStaticObjectMethod(env, cd, npt, label, text) : NULL;
    if (label) (*env)->DeleteLocalRef(env, label);
    if (cd) (*env)->DeleteLocalRef(env, cd);
    if ((*env)->ExceptionCheck(env) || !clip) {
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, cm);
        (*env)->DeleteLocalRef(env, text);
        return 1;
    }
    jclass cmc = (*env)->GetObjectClass(env, cm);
    jmethodID setc = (*env)->GetMethodID(env, cmc, "setPrimaryClip",
                                         "(Landroid/content/ClipData;)V");
    (*env)->DeleteLocalRef(env, cmc);
    if (setc) (*env)->CallVoidMethod(env, cm, setc, clip);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, clip);
    (*env)->DeleteLocalRef(env, cm);
    (*env)->DeleteLocalRef(env, text);
    return setc ? 0 : 1;
}

/* 读取系统剪贴板文本并返回 UTF-8 字符串 */
EXPORT const char *zan_gui_get_clipboard(void) {
    static char *g_clip_buf = NULL;
    JNIEnv *env = zan_anw_bridge_env();
    jobject ctx = zan_anw_bridge_activity();
    if (!env || !ctx) return "";
    jclass cctx = (*env)->FindClass(env, "android/content/Context");
    jmethodID getService = cctx ? (*env)->GetMethodID(env, cctx, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;") : NULL;
    jstring name = getService ? (*env)->NewStringUTF(env, "clipboard") : NULL;
    jobject cm = getService
        ? (*env)->CallObjectMethod(env, ctx, getService, name) : NULL;
    if (name) (*env)->DeleteLocalRef(env, name);
    if (cctx) (*env)->DeleteLocalRef(env, cctx);
    if ((*env)->ExceptionCheck(env) || !cm) {
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        return "";
    }
    jclass cmc = (*env)->GetObjectClass(env, cm);
    jmethodID getc = (*env)->GetMethodID(env, cmc, "getPrimaryClip",
                                         "()Landroid/content/ClipData;");
    jobject clip = getc ? (*env)->CallObjectMethod(env, cm, getc) : NULL;
    (*env)->DeleteLocalRef(env, cmc);
    jstring text = NULL;
    if (!(*env)->ExceptionCheck(env) && clip) {
        jclass clc = (*env)->GetObjectClass(env, clip);
        jmethodID geti = (*env)->GetMethodID(env, clc, "getItemAt",
            "(I)Landroid/content/ClipData$Item;");
        jobject item = geti ? (*env)->CallObjectMethod(env, clip, geti, 0) : NULL;
        (*env)->DeleteLocalRef(env, clc);
        if (!(*env)->ExceptionCheck(env) && item) {
            jclass itc = (*env)->GetObjectClass(env, item);
            jmethodID gt = (*env)->GetMethodID(env, itc, "getText",
                                               "()Ljava/lang/CharSequence;");
            jobject cs = gt ? (*env)->CallObjectMethod(env, item, gt) : NULL;
            (*env)->DeleteLocalRef(env, itc);
            if (!(*env)->ExceptionCheck(env) && cs) {
                jclass csc = (*env)->GetObjectClass(env, cs);
                jmethodID ts = (*env)->GetMethodID(env, csc, "toString",
                                                   "()Ljava/lang/String;");
                (*env)->DeleteLocalRef(env, csc);
                if (ts) text = (jstring)(*env)->CallObjectMethod(env, cs, ts);
                (*env)->DeleteLocalRef(env, cs);
            }
            (*env)->DeleteLocalRef(env, item);
        }
        (*env)->DeleteLocalRef(env, clip);
    }
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); }
    (*env)->DeleteLocalRef(env, cm);
    if (!text) return "";
    jsize n = (*env)->GetStringLength(env, text);
    const jchar *chars = (*env)->GetStringChars(env, text, NULL);
    if (!chars) { (*env)->DeleteLocalRef(env, text); return ""; }
    char *nb = ant_utf16_to_utf8(chars, n);
    (*env)->ReleaseStringChars(env, text, chars);
    (*env)->DeleteLocalRef(env, text);
    if (!nb) return "";
    free(g_clip_buf);
    g_clip_buf = nb;
    return g_clip_buf;
}

EXPORT int  zan_gui_drop_pending(void)             { return 0; }
EXPORT const char *zan_gui_drop_take(void)         { return ""; }
EXPORT void zan_gui_set_ime_pos(i32 x, i32 y)      { (void)x; (void)y; }

EXPORT i32 zan_gui_set_ime_open(i32 on) {
    struct android_app *app = g_anw.app;
    if (!app || !app->activity) return 1;
    JNIEnv *env = zan_anw_bridge_env();
    jobject act = zan_anw_bridge_activity();
    if (env && act && ant_ime_init(env, act) == 0) {
        (*env)->CallStaticVoidMethod(env, g_ime_cls,
                                     on ? g_ime_show : g_ime_hide, act);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        return 0;
    }
    if (on) {
        ANativeActivity_showSoftInput(app->activity,
            ANATIVEACTIVITY_SHOW_SOFT_INPUT_FORCED);
    } else {
        ANativeActivity_hideSoftInput(app->activity,
            ANATIVEACTIVITY_HIDE_SOFT_INPUT_NOT_ALWAYS);
    }
    return 0;
}

EXPORT const char *zan_gui_ime_composing(void) {
    static char buf[sizeof(g_composing)];
    pthread_mutex_lock(&g_aq_lock);
    memcpy(buf, g_composing, g_composing_len + 1);
    pthread_mutex_unlock(&g_aq_lock);
    return buf;
}
