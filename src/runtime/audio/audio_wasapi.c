/* audio_wasapi.c -- Windows WASAPI 硬件音频输出后端驱动 */

#ifdef _WIN32

static DWORD WINAPI zan_audio_thread_proc(LPVOID param) {
    static const CLSID clsid_mmdevice = {0xBCDE0395,0xE52F,0x467C,
        {0x8E,0x3D,0xC4,0x57,0x92,0x91,0x69,0x2E}};
    static const IID iid_immdevice_enum = {0xA95664D2,0x9614,0x4F35,
        {0xA7,0x46,0xDE,0x8D,0xB6,0x36,0x17,0xE6}};
    static const IID iid_iaudioclient = {0x1CB9AD4C,0xDBFA,0x4C32,
        {0xB1,0x78,0xC2,0xF5,0x68,0xA7,0x03,0xB2}};
    static const IID iid_irenderclient = {0xF294ACFC,0x3146,0x4483,
        {0xA7,0xBF,0xAD,0xDC,0xA7,0xC2,0x60,0xE2}};
    IMMDeviceEnumerator *enumr = NULL;
    IMMDevice *dev = NULL;
    WAVEFORMATEX *wf = NULL;
    HANDLE evs[2];
    (void)param;

    evs[0] = zan_audio_stop_evt;
    evs[1] = zan_audio_fill_evt;

    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) {
        zan_audio_set_err("CoInitializeEx failed");
        SetEvent(zan_audio_init_evt);
        return 0;
    }
    if (FAILED(CoCreateInstance(&clsid_mmdevice, NULL, CLSCTX_ALL,
                                &iid_immdevice_enum, (void **)&enumr))
        || !enumr) {
        zan_audio_set_err("MMDeviceEnumerator unavailable");
        CoUninitialize();
        SetEvent(zan_audio_init_evt);
        return 0;
    }
    if (FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(
                enumr, eRender, eConsole, &dev)) || !dev) {
        zan_audio_set_err("no default audio output device");
        IMMDeviceEnumerator_Release(enumr);
        CoUninitialize();
        SetEvent(zan_audio_init_evt);
        return 0;
    }
    if (FAILED(IMMDevice_Activate(dev, &iid_iaudioclient, CLSCTX_ALL,
                                  NULL, (void **)&zan_audio_client))
        || !zan_audio_client) {
        zan_audio_set_err("IAudioClient activate failed");
        goto thread_fail;
    }
    if (FAILED(IAudioClient_GetMixFormat(zan_audio_client, &wf)) || !wf) {
        zan_audio_set_err("GetMixFormat failed");
        goto thread_fail;
    }
    {
        int tag = wf->wFormatTag;
        int bits = wf->wBitsPerSample;
        if (tag == WAVE_FORMAT_EXTENSIBLE
            && wf->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
            unsigned int sub = ((WAVEFORMATEXTENSIBLE *)wf)->SubFormat.Data1;
            if (sub == 3) tag = 3;
            else if (sub == 1) tag = 1;
        }
        if (tag == 3 && bits == 32) zan_audio_dev_fmt = 0;
        else if (tag == 1 && bits == 16) zan_audio_dev_fmt = 1;
        else if (tag == 1 && bits == 32) zan_audio_dev_fmt = 2;
        else if (tag == 1 && bits == 24) zan_audio_dev_fmt = 3;
        else {
            zan_audio_set_err("unsupported device mix format");
            goto thread_fail;
        }
        zan_audio_dev_freq = (int)wf->nSamplesPerSec;
        zan_audio_dev_channels = (int)wf->nChannels;
    }
    if (FAILED(IAudioClient_Initialize(zan_audio_client,
                AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                1000000 /* 100ms in 100ns units */, 0, wf, NULL))) {
        zan_audio_set_err("IAudioClient Initialize failed");
        goto thread_fail;
    }
    if (FAILED(IAudioClient_SetEventHandle(zan_audio_client, zan_audio_fill_evt))) {
        zan_audio_set_err("SetEventHandle failed");
        goto thread_fail;
    }
    if (FAILED(IAudioClient_GetBufferSize(zan_audio_client, &zan_audio_buf_frames))
        || zan_audio_buf_frames == 0) {
        zan_audio_set_err("GetBufferSize failed");
        goto thread_fail;
    }
    if (FAILED(IAudioClient_GetService(zan_audio_client, &iid_irenderclient,
                                       (void **)&zan_audio_render))
        || !zan_audio_render) {
        zan_audio_set_err("IAudioRenderClient unavailable");
        goto thread_fail;
    }
    if (FAILED(IAudioClient_Start(zan_audio_client))) {
        zan_audio_set_err("IAudioClient Start failed");
        goto thread_fail;
    }
    CoTaskMemFree(wf);
    wf = NULL;

    SetEvent(zan_audio_init_evt);
    for (;;) {
        DWORD w = WaitForMultipleObjects(2, evs, FALSE, 2000);
        UINT32 padding = 0, avail;
        BYTE *dst = NULL;
        if (w == WAIT_OBJECT_0) break;
        if (w != WAIT_OBJECT_0 + 1 && w != WAIT_TIMEOUT) break;
        if (!zan_audio_ready) break;
        if (FAILED(IAudioClient_GetCurrentPadding(zan_audio_client, &padding)))
            continue;
        if (padding >= zan_audio_buf_frames) continue;
        avail = zan_audio_buf_frames - padding;
        while (avail > 0) {
            UINT32 chunk = avail > ZAN_AUDIO_MAX_FILL ? ZAN_AUDIO_MAX_FILL : avail;
            if (FAILED(IAudioRenderClient_GetBuffer(zan_audio_render, chunk, &dst)))
                break;
            zan_audio_mix(dst, chunk);
            IAudioRenderClient_ReleaseBuffer(zan_audio_render, chunk, 0);
            avail -= chunk;
        }
    }
    IAudioClient_Stop(zan_audio_client);
    if (wf) CoTaskMemFree(wf);
    if (zan_audio_render) { IAudioRenderClient_Release(zan_audio_render); zan_audio_render = NULL; }
    if (zan_audio_client) { IAudioClient_Release(zan_audio_client); zan_audio_client = NULL; }
    IMMDevice_Release(dev);
    IMMDeviceEnumerator_Release(enumr);
    CoUninitialize();
    return 0;

thread_fail:
    if (wf) CoTaskMemFree(wf);
    zan_audio_client = NULL;
    zan_audio_render = NULL;
    IMMDevice_Release(dev);
    IMMDeviceEnumerator_Release(enumr);
    CoUninitialize();
    SetEvent(zan_audio_init_evt);
    return 0;
}

#endif /* _WIN32 */
