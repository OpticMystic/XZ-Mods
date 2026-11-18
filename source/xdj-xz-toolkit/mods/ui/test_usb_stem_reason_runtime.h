static void usb_stem_reason_runtime_check(void){
    struct xz_ui_model saved=model;int saved_available=audio_available;
    struct xz_audio_status s={.state=XZ_AUDIO_EXPERIMENTAL_READY,.prepared=1};
    audio_available=0;assert(usb_stem_reason(&s)==XZ_UI_USB_WHY_ENGINE);
    audio_available=1;model.enabled=0;assert(usb_stem_reason(&s)==XZ_UI_USB_WHY_OFF);
    model.enabled=XZ_UI_STEM;assert(usb_stem_reason(&s)==XZ_UI_USB_WHY_NONE);
    s.state=XZ_AUDIO_CACHE_MISSING;assert(usb_stem_reason(&s)==XZ_UI_USB_WHY_NOT_PREPARED);
    s.state=XZ_AUDIO_PREPARED_ALIGNING;assert(usb_stem_reason(&s)==XZ_UI_USB_WHY_ALIGNING);
    s.state=XZ_AUDIO_PREPARED_BUFFERING;assert(usb_stem_reason(&s)==XZ_UI_USB_WHY_LOADING);
    s.state=XZ_AUDIO_PREPARED_ERROR;assert(usb_stem_reason(&s)==XZ_UI_USB_WHY_ERROR);
    s.state=XZ_AUDIO_WAITING_TRACK;assert(usb_stem_reason(&s)==XZ_UI_USB_WHY_NO_TRACK);
    char text[16384],quoted[64];
    assert(stem_json_string(quoted,sizeof(quoted),"track\"\\\n"));
    assert(!strcmp(quoted,"\"track\\\"\\\\\\u000a\""));
    assert(xz_mods_stem_status_json_v1(text,sizeof(text))&&strstr(text,"\"ovpgz004\":true"));
    pthread_mutex_lock(&ui_mutex);assert(!xz_mods_stem_status_json_v1(text,sizeof(text)));pthread_mutex_unlock(&ui_mutex);
    assert(!xz_mods_stem_status_json_v1(text,8));
    model=saved;audio_available=saved_available;
    puts("PASS explicit USB stem reasons and bounded read-only diagnostic snapshot");
}
