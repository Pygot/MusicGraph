#define INITGUID
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <gl/GL.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define N 512
#define MAX_BARS 128
#define MAX_DEVICES 32
#define MAX_DEVICE_NAME 128
#define MAX_DEVICE_ID 512

#define IDI_APP_ICON 101
#define APP_TRAY_ICON 1
#define WM_TRAYICON (WM_APP + 1)
#define IDM_TRAY_SHOW 1001
#define IDM_TRAY_HIDE 1002
#define IDM_TRAY_EXIT 1003
#define IDM_SOURCE_OUTPUT 1101
#define IDM_SOURCE_INPUT 1102
#define IDM_COLOR_CUSTOM_TOP 1104
#define IDM_COLOR_CUSTOM_BOT 1105
#define IDM_TOGGLE_LOCK 1106
#define IDM_LAYOUT_BASE 1200
#define IDM_SIZE_BASE 1300
#define IDM_BARS_BASE 1400
#define IDM_GAIN_BASE 1500
#define IDM_SMOOTH_BASE 1600
#define IDM_STYLE_BASE 1700
#define IDM_VISUAL_BASE 1800
#define IDM_ROTATE_BASE 1900
#define IDM_TOGGLE_MIRROR 1910
#define IDM_TOGGLE_MINHEIGHT 1920
#define IDM_TOGGLE_CENTERED 1930
#define IDM_DEVICE_DEFAULT 1999
#define IDM_DEVICE_BASE 2000

#define IDM_COLOR_MODE_STATIC 2100
#define IDM_COLOR_MODE_RAINBOW 2101
#define IDM_COLOR_PRESET_RED 2102
#define IDM_COLOR_PRESET_GREEN 2103
#define IDM_COLOR_PRESET_BLUE 2104
#define IDM_COLOR_PRESET_GRAY 2105
#define IDM_SPACING_BASE 2200

#define SOURCE_OUTPUT 0
#define SOURCE_INPUT 1

#define LAYOUT_BOTTOM_LEFT 0
#define LAYOUT_BOTTOM_CENTER 1
#define LAYOUT_BOTTOM_RIGHT 2
#define LAYOUT_TOP_LEFT 3
#define LAYOUT_TOP_RIGHT 4
#define LAYOUT_CUSTOM 5

#define SIZE_TINY 0
#define SIZE_SMALL 1
#define SIZE_MEDIUM 2
#define SIZE_CUSTOM 3

#define STYLE_BARS 0
#define STYLE_MIRROR 1
#define STYLE_LINE 2
#define STYLE_AREA 3
#define STYLE_DOTS 4

typedef struct {
    char name[MAX_DEVICE_NAME];
    WCHAR id[MAX_DEVICE_ID];
} AUDIO_DEVICE;

typedef struct {
    int sourceMode;
    int layoutPreset;
    int sizePreset;
    int customWidth;
    int customHeight;
    int customX;
    int customY;
    int isUnlocked;
    int barCount;
    float gain;
    float smoothing;
    int graphStyle;
    float visibilityBoost;
    int renderDeviceIndex;
    int captureDeviceIndex;
    int visible;
    float colorTop[4];
    float colorBot[4];
    int colorMode;
    int rotation;
    int isMirrored;
    int keepVisible;
    int isCentered;
    int spacingPreset;
} SETTINGS;

static const char *g_layoutNames[] = {
    "Bottom left", "Bottom center", "Bottom right",
    "Top left", "Top right", "Custom (Drag to move/resize)"
};

static const int g_sizeWidths[] = {220, 280, 360, 500};
static const int g_sizeHeights[] = {100, 120, 150, 200};

static const int g_barValues[] = {16, 24, 32, 48, 64, 96, 128};
static const float g_gainValues[] = {0.015f, 0.025f, 0.04f, 0.06f, 0.09f, 0.15f, 0.25f};
static const float g_smoothingValues[] = {0.0f, 0.55f, 0.7f, 0.8f, 0.9f, 0.95f};
static const char *g_styleNames[] = {"Bars", "Mirror bars", "Line", "Filled area", "Dots"};
static const float g_visibilityValues[] = {0.5f, 0.85f, 1.0f, 1.2f, 2.0f};
static const char *g_visibilityNames[] = {"Very Soft", "Soft", "Normal", "Strong", "Max"};
static const int g_rotateValues[] = {0, 90, 180, 270};
static const int g_spacingValues[] = {0, 1, 2, 3, 4, 5, 6, 8, 10, 15, 20};

static double real[N];
static double imag[N];
static double smooth[MAX_BARS];

static SETTINGS settings = {
    SOURCE_OUTPUT, LAYOUT_BOTTOM_CENTER, SIZE_MEDIUM, 400, 150, 100, 100, 0,
    64, 0.04f, 0.8f, STYLE_BARS, 1.2f, -1, -1, 1,
    {0.3f, 0.6f, 1.0f, 0.95f}, {0.1f, 0.2f, 0.8f, 0.6f}, 0, 0, 0, 1, 0, 2
};

static AUDIO_DEVICE renderDevices[MAX_DEVICES];
static AUDIO_DEVICE captureDevices[MAX_DEVICES];
static int renderDeviceCount = 0;
static int captureDeviceCount = 0;
static const PROPERTYKEY kDeviceFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
static const GUID kIeeeFloatSubtype = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

static IAudioClient *audioClient = NULL;
static IAudioCaptureClient *captureClient = NULL;
static WAVEFORMATEX *mixFormat = NULL;
static int audioChannels = 1;
static int audioBytesPerSample = 2;
static int audioIsFloat = 0;
static WCHAR lastDefaultDeviceId[MAX_DEVICE_ID] = {0};
static int pollCounter = 0;

static HINSTANCE g_hInstance = NULL;
static HWND g_hwnd = NULL;
static HDC hdc = NULL;
static HGLRC hglrc = NULL;
static NOTIFYICONDATAA g_nid;
static UINT g_taskbarCreatedMessage = 0;
static char iniPath[MAX_PATH];
static float hueAnim = 0.0f;

static void release_audio(void);
static int start_audio(void);
static void refresh_devices(void);
static void show_tray_menu(HWND hwnd);

static void hsv_to_rgb(float h, float s, float v, float *r, float *g, float *b) {
    int i = (int)(h * 6);
    float f = h * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);
    switch (i % 6) {
        case 0: *r = v, *g = t, *b = p; break;
        case 1: *r = q, *g = v, *b = p; break;
        case 2: *r = p, *g = v, *b = t; break;
        case 3: *r = p, *g = q, *b = v; break;
        case 4: *r = t, *g = p, *b = v; break;
        case 5: *r = v, *g = p, *b = q; break;
    }
}

static void SaveSettings(void) {
    char buf[64];
    WritePrivateProfileStringA("Settings", "SourceMode", itoa(settings.sourceMode, buf, 10), iniPath);
    WritePrivateProfileStringA("Settings", "LayoutPreset", itoa(settings.layoutPreset, buf, 10), iniPath);
    WritePrivateProfileStringA("Settings", "SizePreset", itoa(settings.sizePreset, buf, 10), iniPath);
    WritePrivateProfileStringA("Settings", "CustomWidth", itoa(settings.customWidth, buf, 10), iniPath);
    WritePrivateProfileStringA("Settings", "CustomHeight", itoa(settings.customHeight, buf, 10), iniPath);
    WritePrivateProfileStringA("Settings", "CustomX", itoa(settings.customX, buf, 10), iniPath);
    WritePrivateProfileStringA("Settings", "CustomY", itoa(settings.customY, buf, 10), iniPath);
    WritePrivateProfileStringA("Settings", "BarCount", itoa(settings.barCount, buf, 10), iniPath);
    snprintf(buf, sizeof(buf), "%f", settings.gain); WritePrivateProfileStringA("Settings", "Gain", buf, iniPath);
    snprintf(buf, sizeof(buf), "%f", settings.smoothing); WritePrivateProfileStringA("Settings", "Smoothing", buf, iniPath);
    WritePrivateProfileStringA("Settings", "GraphStyle", itoa(settings.graphStyle, buf, 10), iniPath);
    snprintf(buf, sizeof(buf), "%f", settings.visibilityBoost); WritePrivateProfileStringA("Settings", "VisibilityBoost", buf, iniPath);
    WritePrivateProfileStringA("Settings", "RenderDeviceIndex", itoa(settings.renderDeviceIndex, buf, 10), iniPath);
    WritePrivateProfileStringA("Settings", "CaptureDeviceIndex", itoa(settings.captureDeviceIndex, buf, 10), iniPath);
    WritePrivateProfileStringA("Settings", "Visible", itoa(settings.visible, buf, 10), iniPath);
    WritePrivateProfileStringA("Settings", "Rotation", itoa(settings.rotation, buf, 10), iniPath);
    WritePrivateProfileStringA("Settings", "Mirrored", itoa(settings.isMirrored, buf, 10), iniPath);
    WritePrivateProfileStringA("Settings", "ColorMode", itoa(settings.colorMode, buf, 10), iniPath);
    WritePrivateProfileStringA("Settings", "KeepVisible", itoa(settings.keepVisible, buf, 10), iniPath);
    WritePrivateProfileStringA("Settings", "Centered", itoa(settings.isCentered, buf, 10), iniPath);
    WritePrivateProfileStringA("Settings", "SpacingPreset", itoa(settings.spacingPreset, buf, 10), iniPath);

    snprintf(buf, sizeof(buf), "%f,%f,%f,%f", settings.colorTop[0], settings.colorTop[1], settings.colorTop[2], settings.colorTop[3]);
    WritePrivateProfileStringA("Settings", "ColorTop", buf, iniPath);
    snprintf(buf, sizeof(buf), "%f,%f,%f,%f", settings.colorBot[0], settings.colorBot[1], settings.colorBot[2], settings.colorBot[3]);
    WritePrivateProfileStringA("Settings", "ColorBot", buf, iniPath);
}

static void LoadSettings(void) {
    char buf[64];
    settings.sourceMode = GetPrivateProfileIntA("Settings", "SourceMode", settings.sourceMode, iniPath);
    settings.layoutPreset = GetPrivateProfileIntA("Settings", "LayoutPreset", settings.layoutPreset, iniPath);
    settings.sizePreset = GetPrivateProfileIntA("Settings", "SizePreset", settings.sizePreset, iniPath);
    settings.customWidth = GetPrivateProfileIntA("Settings", "CustomWidth", settings.customWidth, iniPath);
    settings.customHeight = GetPrivateProfileIntA("Settings", "CustomHeight", settings.customHeight, iniPath);
    settings.customX = GetPrivateProfileIntA("Settings", "CustomX", settings.customX, iniPath);
    settings.customY = GetPrivateProfileIntA("Settings", "CustomY", settings.customY, iniPath);
    settings.barCount = GetPrivateProfileIntA("Settings", "BarCount", settings.barCount, iniPath);

    GetPrivateProfileStringA("Settings", "Gain", "", buf, sizeof(buf), iniPath);
    if (buf[0]) settings.gain = (float)atof(buf);

    GetPrivateProfileStringA("Settings", "Smoothing", "", buf, sizeof(buf), iniPath);
    if (buf[0]) settings.smoothing = (float)atof(buf);

    settings.graphStyle = GetPrivateProfileIntA("Settings", "GraphStyle", settings.graphStyle, iniPath);

    GetPrivateProfileStringA("Settings", "VisibilityBoost", "", buf, sizeof(buf), iniPath);
    if (buf[0]) settings.visibilityBoost = (float)atof(buf);

    settings.renderDeviceIndex = GetPrivateProfileIntA("Settings", "RenderDeviceIndex", settings.renderDeviceIndex, iniPath);
    settings.captureDeviceIndex = GetPrivateProfileIntA("Settings", "CaptureDeviceIndex", settings.captureDeviceIndex, iniPath);
    settings.visible = GetPrivateProfileIntA("Settings", "Visible", settings.visible, iniPath);
    settings.rotation = GetPrivateProfileIntA("Settings", "Rotation", settings.rotation, iniPath);
    settings.isMirrored = GetPrivateProfileIntA("Settings", "Mirrored", settings.isMirrored, iniPath);
    settings.colorMode = GetPrivateProfileIntA("Settings", "ColorMode", settings.colorMode, iniPath);
    settings.keepVisible = GetPrivateProfileIntA("Settings", "KeepVisible", settings.keepVisible, iniPath);
    settings.isCentered = GetPrivateProfileIntA("Settings", "Centered", settings.isCentered, iniPath);
    settings.spacingPreset = GetPrivateProfileIntA("Settings", "SpacingPreset", settings.spacingPreset, iniPath);
    if (settings.spacingPreset < 0 || settings.spacingPreset >= (int)(sizeof(g_spacingValues)/sizeof(g_spacingValues[0]))) settings.spacingPreset = 2;

    GetPrivateProfileStringA("Settings", "ColorTop", "", buf, sizeof(buf), iniPath);
    if (buf[0]) sscanf(buf, "%f,%f,%f,%f", &settings.colorTop[0], &settings.colorTop[1], &settings.colorTop[2], &settings.colorTop[3]);

    GetPrivateProfileStringA("Settings", "ColorBot", "", buf, sizeof(buf), iniPath);
    if (buf[0]) sscanf(buf, "%f,%f,%f,%f", &settings.colorBot[0], &settings.colorBot[1], &settings.colorBot[2], &settings.colorBot[3]);
}

static double read_sample(const BYTE *p) {
    if (audioIsFloat) {
        float v = 0.0f;
        memcpy(&v, p, sizeof(float));
        return v;
    }
    switch (audioBytesPerSample) {
        case 1: return ((int)p[0] - 128) / 128.0;
        case 2: { short v = 0; memcpy(&v, p, sizeof(short)); return v / 32768.0; }
        case 3: { int v = (int)p[0] | ((int)p[1] << 8) | ((int)p[2] << 16); if (v & 0x800000) v |= ~0xFFFFFF; return v / 8388608.0; }
        case 4: { long v = 0; memcpy(&v, p, sizeof(long)); return v / 2147483648.0; }
        default: return 0.0;
    }
}

static int is_float_format(const WAVEFORMATEX *fmt) {
    if (!fmt) return 0;
    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return 1;
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *ext = (const WAVEFORMATEXTENSIBLE *)fmt;
        if (IsEqualGUID(&ext->SubFormat, &kIeeeFloatSubtype)) return 1;
    }
    return 0;
}

static void decode_friendly_name(IPropertyStore *store, AUDIO_DEVICE *device) {
    PROPVARIANT value;
    PropVariantInit(&value);
    device->name[0] = '\0';
    if (store->lpVtbl->GetValue(store, &kDeviceFriendlyName, &value) == S_OK && value.vt == VT_LPWSTR && value.pwszVal) {
        WideCharToMultiByte(CP_ACP, 0, value.pwszVal, -1, device->name, MAX_DEVICE_NAME, NULL, NULL);
    }
    if (device->name[0] == '\0') lstrcpynA(device->name, "Unknown device", MAX_DEVICE_NAME);
    PropVariantClear(&value);
}

static int enumerate_devices(EDataFlow flow, AUDIO_DEVICE *outDevices) {
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDeviceCollection *collection = NULL;
    HRESULT hr;
    UINT count = 0;
    int written = 0;
    if (CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&enumerator) != S_OK) return 0;
    hr = enumerator->lpVtbl->EnumAudioEndpoints(enumerator, flow, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) { enumerator->lpVtbl->Release(enumerator); return 0; }
    collection->lpVtbl->GetCount(collection, &count);
    for (UINT i = 0; i < count && written < MAX_DEVICES; i++) {
        IMMDevice *device = NULL;
        IPropertyStore *store = NULL;
        LPWSTR id = NULL;
        if (collection->lpVtbl->Item(collection, i, &device) != S_OK) continue;
        if (device->lpVtbl->GetId(device, &id) == S_OK && id) lstrcpynW(outDevices[written].id, id, MAX_DEVICE_ID);
        else outDevices[written].id[0] = L'\0';
        if (device->lpVtbl->OpenPropertyStore(device, STGM_READ, &store) == S_OK && store) {
            decode_friendly_name(store, &outDevices[written]);
            store->lpVtbl->Release(store);
        } else lstrcpynA(outDevices[written].name, "Unknown device", MAX_DEVICE_NAME);
        if (id) CoTaskMemFree(id);
        device->lpVtbl->Release(device);
        written++;
    }
    collection->lpVtbl->Release(collection);
    enumerator->lpVtbl->Release(enumerator);
    return written;
}

static const AUDIO_DEVICE *current_device_list(void) { return settings.sourceMode == SOURCE_OUTPUT ? renderDevices : captureDevices; }
static int current_device_count(void) { return settings.sourceMode == SOURCE_OUTPUT ? renderDeviceCount : captureDeviceCount; }

static const WCHAR *current_device_id(void) {
    if (settings.sourceMode == SOURCE_OUTPUT) {
        if (settings.renderDeviceIndex == -1) return NULL;
        if (settings.renderDeviceIndex >= 0 && settings.renderDeviceIndex < renderDeviceCount)
            return renderDevices[settings.renderDeviceIndex].id;
    } else {
        if (settings.captureDeviceIndex == -1) return NULL;
        if (settings.captureDeviceIndex >= 0 && settings.captureDeviceIndex < captureDeviceCount)
            return captureDevices[settings.captureDeviceIndex].id;
    }
    return NULL;
}

static int current_device_index(void) { return settings.sourceMode == SOURCE_OUTPUT ? settings.renderDeviceIndex : settings.captureDeviceIndex; }

static void clamp_device_indices(void) {
    if (settings.renderDeviceIndex != -1) {
        if (renderDeviceCount <= 0 || settings.renderDeviceIndex >= renderDeviceCount) settings.renderDeviceIndex = -1;
    }
    if (settings.captureDeviceIndex != -1) {
        if (captureDeviceCount <= 0 || settings.captureDeviceIndex >= captureDeviceCount) settings.captureDeviceIndex = -1;
    }
}

static void refresh_devices(void) {
    renderDeviceCount = enumerate_devices(eRender, renderDevices);
    captureDeviceCount = enumerate_devices(eCapture, captureDevices);
    clamp_device_indices();
}

static void check_default_device_changed(void) {
    if (current_device_index() != -1) return;
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    LPWSTR id = NULL;
    EDataFlow flow = settings.sourceMode == SOURCE_OUTPUT ? eRender : eCapture;

    if (CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&enumerator) == S_OK) {
        if (enumerator->lpVtbl->GetDefaultAudioEndpoint(enumerator, flow, eConsole, &device) == S_OK) {
            if (device->lpVtbl->GetId(device, &id) == S_OK && id) {
                if (wcscmp(id, lastDefaultDeviceId) != 0) {
                    lstrcpynW(lastDefaultDeviceId, id, MAX_DEVICE_ID);
                    refresh_devices();
                    start_audio();
                }
                CoTaskMemFree(id);
            }
            device->lpVtbl->Release(device);
        }
        enumerator->lpVtbl->Release(enumerator);
    }
}

static void release_audio(void) {
    if (captureClient) { captureClient->lpVtbl->Release(captureClient); captureClient = NULL; }
    if (audioClient) { audioClient->lpVtbl->Stop(audioClient); audioClient->lpVtbl->Release(audioClient); audioClient = NULL; }
    if (mixFormat) { CoTaskMemFree(mixFormat); mixFormat = NULL; }
    audioChannels = 1; audioBytesPerSample = 2; audioIsFloat = 0;
}

static int initialize_audio_client(const WCHAR *deviceId) {
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    HRESULT hr;
    LPWSTR gotId = NULL;
    DWORD streamFlags = settings.sourceMode == SOURCE_OUTPUT ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
    EDataFlow flow = settings.sourceMode == SOURCE_OUTPUT ? eRender : eCapture;
    if (CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&enumerator) != S_OK) return 0;

    if (deviceId && deviceId[0] != L'\0') hr = enumerator->lpVtbl->GetDevice(enumerator, deviceId, &device);
    else hr = enumerator->lpVtbl->GetDefaultAudioEndpoint(enumerator, flow, eConsole, &device);

    if (FAILED(hr)) { enumerator->lpVtbl->Release(enumerator); return 0; }

    if (deviceId == NULL || deviceId[0] == L'\0') {
        if (device->lpVtbl->GetId(device, &gotId) == S_OK && gotId) {
            lstrcpynW(lastDefaultDeviceId, gotId, MAX_DEVICE_ID);
            CoTaskMemFree(gotId);
        }
    }

    hr = device->lpVtbl->Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&audioClient);
    if (FAILED(hr)) { device->lpVtbl->Release(device); enumerator->lpVtbl->Release(enumerator); return 0; }
    hr = audioClient->lpVtbl->GetMixFormat(audioClient, &mixFormat);
    if (FAILED(hr)) { device->lpVtbl->Release(device); enumerator->lpVtbl->Release(enumerator); release_audio(); return 0; }
    audioChannels = mixFormat->nChannels > 0 ? mixFormat->nChannels : 1;
    audioBytesPerSample = mixFormat->wBitsPerSample / 8;
    if (audioBytesPerSample <= 0) audioBytesPerSample = 2;
    audioIsFloat = is_float_format(mixFormat);
    hr = audioClient->lpVtbl->Initialize(audioClient, AUDCLNT_SHAREMODE_SHARED, streamFlags, 0, 0, mixFormat, NULL);
    if (FAILED(hr)) { device->lpVtbl->Release(device); enumerator->lpVtbl->Release(enumerator); release_audio(); return 0; }
    hr = audioClient->lpVtbl->GetService(audioClient, &IID_IAudioCaptureClient, (void **)&captureClient);
    if (FAILED(hr)) { device->lpVtbl->Release(device); enumerator->lpVtbl->Release(enumerator); release_audio(); return 0; }
    hr = audioClient->lpVtbl->Start(audioClient);
    device->lpVtbl->Release(device);
    enumerator->lpVtbl->Release(enumerator);
    if (FAILED(hr)) { release_audio(); return 0; }
    return 1;
}

static int start_audio(void) {
    const WCHAR *deviceId = current_device_id();
    release_audio();
    return initialize_audio_client(deviceId);
}

static void restart_audio(void) {
    if (!start_audio()) MessageBoxA(g_hwnd, "Unable to start audio capture.", "MusicGraph", MB_OK | MB_ICONERROR);
}

static RECT get_work_area(void) {
    RECT work;
    MONITORINFO info;
    HMONITOR monitor = MonitorFromWindow(g_hwnd ? g_hwnd : GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    info.cbSize = sizeof(info);
    if (GetMonitorInfoA(monitor, &info)) work = info.rcWork;
    else { work.left = 0; work.top = 0; work.right = GetSystemMetrics(SM_CXSCREEN); work.bottom = GetSystemMetrics(SM_CYSCREEN); }
    return work;
}

static void apply_window_size(void) {
    RECT work = get_work_area();
    int width = settings.sizePreset == SIZE_CUSTOM ? settings.customWidth : g_sizeWidths[settings.sizePreset];
    int height = settings.sizePreset == SIZE_CUSTOM ? settings.customHeight : g_sizeHeights[settings.sizePreset];

    int topMargin = 24;
    int bottomMargin = 0;
    int x = work.left + 24, y = work.top + 24;

    switch (settings.layoutPreset) {
        case LAYOUT_BOTTOM_LEFT: x = work.left + 24; y = work.bottom - height - bottomMargin; break;
        case LAYOUT_BOTTOM_CENTER: x = work.left + ((work.right - work.left) - width) / 2; y = work.bottom - height - bottomMargin; break;
        case LAYOUT_BOTTOM_RIGHT: x = work.right - width - 24; y = work.bottom - height - bottomMargin; break;
        case LAYOUT_TOP_LEFT: x = work.left + 24; y = work.top + topMargin; break;
        case LAYOUT_TOP_RIGHT: x = work.right - width - 24; y = work.top + topMargin; break;
        case LAYOUT_CUSTOM: x = settings.customX; y = settings.customY; break;
        default: break;
    }

    if (width < 50) width = 50;
    if (height < 20) height = 20;

    MoveWindow(g_hwnd, x, y, width, height, settings.visible);
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | (settings.visible ? SWP_SHOWWINDOW : 0));
    SaveSettings();
}

static void choose_color(float *c) {
    CHOOSECOLOR cc;
    static COLORREF acrCustClr[16];
    ZeroMemory(&cc, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = g_hwnd;
    cc.lpCustColors = (LPDWORD)acrCustClr;
    cc.rgbResult = RGB((BYTE)(c[0]*255), (BYTE)(c[1]*255), (BYTE)(c[2]*255));
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColor(&cc)) {
        c[0] = GetRValue(cc.rgbResult) / 255.0f;
        c[1] = GetGValue(cc.rgbResult) / 255.0f;
        c[2] = GetBValue(cc.rgbResult) / 255.0f;
        settings.colorMode = 0;
        SaveSettings();
    }
}

static void show_overlay(int show) {
    settings.visible = show ? 1 : 0;
    if (settings.visible) { ShowWindow(g_hwnd, SW_SHOWNOACTIVATE); apply_window_size(); }
    else ShowWindow(g_hwnd, SW_HIDE);
    SaveSettings();
}

static HMENU build_tray_menu(void) {
    HMENU root = CreatePopupMenu();

    HMENU audioMenu = CreatePopupMenu();
    HMENU posMenu = CreatePopupMenu();
    HMENU colorMenu = CreatePopupMenu();
    HMENU styleMenu = CreatePopupMenu();
    HMENU adjustMenu = CreatePopupMenu();
    HMENU spacingMenu = CreatePopupMenu();

    AppendMenuA(root, MF_STRING | (settings.visible ? MF_GRAYED : 0), IDM_TRAY_SHOW, "Show overlay");
    AppendMenuA(root, MF_STRING | (!settings.visible ? MF_GRAYED : 0), IDM_TRAY_HIDE, "Hide overlay");
    AppendMenuA(root, MF_SEPARATOR, 0, NULL);

    AppendMenuA(audioMenu, MF_STRING | (settings.sourceMode == SOURCE_OUTPUT ? MF_CHECKED : 0), IDM_SOURCE_OUTPUT, "System output");
    AppendMenuA(audioMenu, MF_STRING | (settings.sourceMode == SOURCE_INPUT ? MF_CHECKED : 0), IDM_SOURCE_INPUT, "Microphone input");
    AppendMenuA(audioMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(audioMenu, MF_STRING | (current_device_index() == -1 ? MF_CHECKED : 0), IDM_DEVICE_DEFAULT, "[Default System Device]");
    int deviceCount = current_device_count();
    const AUDIO_DEVICE *devices = current_device_list();
    if (deviceCount > 0) {
        for (int i = 0; i < deviceCount; i++)
            AppendMenuA(audioMenu, MF_STRING | (current_device_index() == i ? MF_CHECKED : 0), IDM_DEVICE_BASE + i, devices[i].name);
    } else {
        AppendMenuA(audioMenu, MF_STRING | MF_GRAYED, IDM_DEVICE_BASE + MAX_DEVICES, "No active devices");
    }
    AppendMenuA(root, MF_POPUP, (UINT_PTR)audioMenu, "Source & Device");

    for (int i = 0; i < (int)(sizeof(g_layoutNames) / sizeof(g_layoutNames[0])); i++)
        AppendMenuA(posMenu, MF_STRING | (settings.layoutPreset == i ? MF_CHECKED : 0), IDM_LAYOUT_BASE + i, g_layoutNames[i]);
    AppendMenuA(posMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(posMenu, MF_STRING | (settings.sizePreset == SIZE_TINY ? MF_CHECKED : 0), IDM_SIZE_BASE + SIZE_TINY, "Tiny");
    AppendMenuA(posMenu, MF_STRING | (settings.sizePreset == SIZE_SMALL ? MF_CHECKED : 0), IDM_SIZE_BASE + SIZE_SMALL, "Small");
    AppendMenuA(posMenu, MF_STRING | (settings.sizePreset == SIZE_MEDIUM ? MF_CHECKED : 0), IDM_SIZE_BASE + SIZE_MEDIUM, "Medium");
    AppendMenuA(posMenu, MF_STRING | (settings.sizePreset == SIZE_CUSTOM ? MF_CHECKED : 0), IDM_SIZE_BASE + SIZE_CUSTOM, "Custom Size");
    AppendMenuA(posMenu, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < (int)(sizeof(g_rotateValues) / sizeof(g_rotateValues[0])); i++) {
        char label[32]; snprintf(label, sizeof(label), "%d degrees", g_rotateValues[i]);
        AppendMenuA(posMenu, MF_STRING | (settings.rotation == g_rotateValues[i] ? MF_CHECKED : 0), IDM_ROTATE_BASE + i, label);
    }
    AppendMenuA(posMenu, MF_STRING | (settings.isMirrored ? MF_CHECKED : 0), IDM_TOGGLE_MIRROR, "Mirror Horizontally");
    AppendMenuA(posMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(posMenu, MF_STRING | (settings.isUnlocked ? MF_CHECKED : 0), IDM_TOGGLE_LOCK, "Unlock Window (Drag / Resize)");
    AppendMenuA(root, MF_POPUP, (UINT_PTR)posMenu, "Position & Size");

    AppendMenuA(colorMenu, MF_STRING | (settings.colorMode == 1 ? MF_CHECKED : 0), IDM_COLOR_MODE_RAINBOW, "Animated Rainbow");
    AppendMenuA(colorMenu, MF_STRING | (settings.colorMode == 0 ? MF_CHECKED : 0), IDM_COLOR_MODE_STATIC, "Static Mode");
    AppendMenuA(colorMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(colorMenu, MF_STRING, IDM_COLOR_PRESET_RED, "Preset: Red");
    AppendMenuA(colorMenu, MF_STRING, IDM_COLOR_PRESET_GREEN, "Preset: Green");
    AppendMenuA(colorMenu, MF_STRING, IDM_COLOR_PRESET_BLUE, "Preset: Blue");
    AppendMenuA(colorMenu, MF_STRING, IDM_COLOR_PRESET_GRAY, "Preset: Gray");
    AppendMenuA(colorMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(colorMenu, MF_STRING, IDM_COLOR_CUSTOM_TOP, "Custom Top Color...");
    AppendMenuA(colorMenu, MF_STRING, IDM_COLOR_CUSTOM_BOT, "Custom Bottom Color...");
    AppendMenuA(root, MF_POPUP, (UINT_PTR)colorMenu, "Colors");

    for (int i = 0; i < (int)(sizeof(g_styleNames) / sizeof(g_styleNames[0])); i++)
        AppendMenuA(styleMenu, MF_STRING | (settings.graphStyle == i ? MF_CHECKED : 0), IDM_STYLE_BASE + i, g_styleNames[i]);
    AppendMenuA(styleMenu, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < (int)(sizeof(g_visibilityValues) / sizeof(g_visibilityValues[0])); i++)
        AppendMenuA(styleMenu, MF_STRING | (fabsf(settings.visibilityBoost - g_visibilityValues[i]) < 0.0001f ? MF_CHECKED : 0), IDM_VISUAL_BASE + i, g_visibilityNames[i]);
    AppendMenuA(root, MF_POPUP, (UINT_PTR)styleMenu, "Graph Style");

    for (int i = 0; i < (int)(sizeof(g_barValues) / sizeof(g_barValues[0])); i++) {
        char label[32]; snprintf(label, sizeof(label), "%d bars", g_barValues[i]);
        AppendMenuA(adjustMenu, MF_STRING | (settings.barCount == g_barValues[i] ? MF_CHECKED : 0), IDM_BARS_BASE + i, label);
    }
    AppendMenuA(adjustMenu, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < (int)(sizeof(g_spacingValues) / sizeof(g_spacingValues[0])); i++) {
        char label[32];
        if (g_spacingValues[i] == 0) snprintf(label, sizeof(label), "None (0px)");
        else snprintf(label, sizeof(label), "%d pixels", g_spacingValues[i]);
        AppendMenuA(spacingMenu, MF_STRING | (settings.spacingPreset == i ? MF_CHECKED : 0), IDM_SPACING_BASE + i, label);
    }
    AppendMenuA(adjustMenu, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < (int)(sizeof(g_gainValues) / sizeof(g_gainValues[0])); i++) {
        char label[32]; snprintf(label, sizeof(label), "%.3fx gain", g_gainValues[i]);
        AppendMenuA(adjustMenu, MF_STRING | (fabsf(settings.gain - g_gainValues[i]) < 0.0001f ? MF_CHECKED : 0), IDM_GAIN_BASE + i, label);
    }
    AppendMenuA(adjustMenu, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < (int)(sizeof(g_smoothingValues) / sizeof(g_smoothingValues[0])); i++) {
        char label[32]; snprintf(label, sizeof(label), "%.0f%% smoothing", g_smoothingValues[i] * 100.0f);
        AppendMenuA(adjustMenu, MF_STRING | (fabsf(settings.smoothing - g_smoothingValues[i]) < 0.0001f ? MF_CHECKED : 0), IDM_SMOOTH_BASE + i, label);
    }
    AppendMenuA(adjustMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(adjustMenu, MF_STRING | (settings.isCentered ? MF_CHECKED : 0), IDM_TOGGLE_CENTERED, "Center-out Frequencies");
    AppendMenuA(adjustMenu, MF_STRING | (settings.keepVisible ? MF_CHECKED : 0), IDM_TOGGLE_MINHEIGHT, "Always Keep Slightly Visible");
    AppendMenuA(adjustMenu, MF_POPUP, (UINT_PTR)spacingMenu, "Bar Spacing");
    AppendMenuA(root, MF_POPUP, (UINT_PTR)adjustMenu, "Graph Adjustments");

    AppendMenuA(root, MF_SEPARATOR, 0, NULL);
    AppendMenuA(root, MF_STRING, IDM_TRAY_EXIT, "Exit");

    return root;
}

static void show_tray_menu(HWND hwnd) {
    POINT pt; GetCursorPos(&pt); SetForegroundWindow(hwnd);
    HMENU menu = build_tray_menu();
    UINT command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu);
    if (command != 0) SendMessage(hwnd, WM_COMMAND, command, 0);
    PostMessage(hwnd, WM_NULL, 0, 0);
}

static void add_tray_icon(HWND hwnd) {
    HICON hIcon = LoadIconA(g_hInstance, MAKEINTRESOURCEA(IDI_APP_ICON));
    if (!hIcon) hIcon = LoadIconA(NULL, IDI_APPLICATION);

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = APP_TRAY_ICON;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = hIcon;
    lstrcpynA(g_nid.szTip, "MusicGraph", sizeof(g_nid.szTip));
    Shell_NotifyIconA(NIM_ADD, &g_nid);
}

static void remove_tray_icon(void) { Shell_NotifyIconA(NIM_DELETE, &g_nid); }

static void fft(void) {
    int n = N, j = 0;
    for (int i = 1; i < n; i++) {
        int bit = n >> 1;
        while (j & bit) { j ^= bit; bit >>= 1; }
        j |= bit;
        if (i < j) { double tr = real[i]; real[i] = real[j]; real[j] = tr; double ti = imag[i]; imag[i] = imag[j]; imag[j] = ti; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * 3.14159265358979323846 / len, wlen_cos = cos(ang), wlen_sin = sin(ang);
        for (int i = 0; i < n; i += len) {
            double w_cos = 1.0, w_sin = 0.0;
            for (int k = 0; k < len / 2; k++) {
                int u = i + k, v = i + k + len / 2;
                double vr = real[v] * w_cos - imag[v] * w_sin, vi = real[v] * w_sin + imag[v] * w_cos;
                double ur = real[u], ui = imag[u];
                real[u] = ur + vr; imag[u] = ui + vi; real[v] = ur - vr; imag[v] = ui - vi;
                double next_cos = w_cos * wlen_cos - w_sin * wlen_sin, next_sin = w_cos * wlen_sin + w_sin * wlen_cos;
                w_cos = next_cos; w_sin = next_sin;
            }
        }
    }
}

static void capture_audio(void) {
    if (!captureClient) return;
    BYTE *data = NULL; UINT32 frames = 0; DWORD flags = 0;
    ZeroMemory(real, sizeof(real)); ZeroMemory(imag, sizeof(imag));
    if (captureClient->lpVtbl->GetBuffer(captureClient, &data, &frames, &flags, NULL, NULL) != S_OK) return;
    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == NULL) { captureClient->lpVtbl->ReleaseBuffer(captureClient, frames); return; }
    UINT32 framesToCopy = frames; if (framesToCopy > N) framesToCopy = N;
    int frameBytes = mixFormat ? mixFormat->nBlockAlign : (audioBytesPerSample * audioChannels);
    if (frameBytes <= 0) frameBytes = audioBytesPerSample * audioChannels;
    if (frameBytes <= 0) frameBytes = 4;
    for (UINT32 i = 0; i < framesToCopy; i++) {
        const BYTE *frame = data + (i * frameBytes); double sum = 0.0;
        for (int ch = 0; ch < audioChannels; ch++) sum += read_sample(frame + ch * audioBytesPerSample);
        real[i] = sum / audioChannels;
    }
    captureClient->lpVtbl->ReleaseBuffer(captureClient, frames);
}

static void render(int width, int height) {
    int barCount = settings.barCount;
    float levels[MAX_BARS];
    float renderLevels[MAX_BARS];
    float boost = settings.visibilityBoost;
    if (barCount < 1) barCount = 1;
    if (barCount > MAX_BARS) barCount = MAX_BARS;

    if (settings.colorMode == 1) {
        hueAnim += 0.002f;
        if (hueAnim > 1.0f) hueAnim -= 1.0f;
        hsv_to_rgb(hueAnim, 1.0f, 1.0f, &settings.colorBot[0], &settings.colorBot[1], &settings.colorBot[2]);
        float topHue = hueAnim + 0.15f;
        if (topHue > 1.0f) topHue -= 1.0f;
        hsv_to_rgb(topHue, 1.0f, 1.0f, &settings.colorTop[0], &settings.colorTop[1], &settings.colorTop[2]);
    }

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, 0, height, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if (settings.rotation != 0 || settings.isMirrored) {
        glTranslatef(width / 2.0f, height / 2.0f, 0);
        if (settings.rotation != 0) glRotatef(settings.rotation, 0, 0, 1);
        if (settings.isMirrored) glScalef(-1.0f, 1.0f, 1.0f);
        glTranslatef(-width / 2.0f, -height / 2.0f, 0);
    }

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (settings.isUnlocked) {
        glBegin(GL_QUADS);
        glColor4f(0.2f, 0.2f, 0.2f, 0.8f);
        glVertex2f(0, 0); glVertex2f(width, 0); glVertex2f(width, height); glVertex2f(0, height);
        glEnd();
    }

    float marginX = 10.0f, marginY = 10.0f;
    float usableWidth = (float)width - marginX * 2.0f, usableHeight = (float)height - marginY * 2.0f;
    if (usableWidth < 1.0f) usableWidth = 1.0f;
    if (usableHeight < 1.0f) usableHeight = 1.0f;

    for (int i = 0; i < barCount; i++) {
        int bin = 1 + (i * ((N / 2) - 1)) / barCount;
        if (bin >= N) bin = N - 1;
        double mag = sqrt(real[bin] * real[bin] + imag[bin] * imag[bin]);
        double target = mag * settings.gain;
        target = target / (1.0 + target);
        smooth[i] = smooth[i] * settings.smoothing + target * (1.0 - settings.smoothing);
        float level = (float)smooth[i] * boost;
        if (settings.keepVisible && level < 0.02f) level = 0.02f;
        if (level > 1.0f) level = 1.0f;
        if (level < 0.0f) level = 0.0f;
        levels[i] = level;
    }

    if (settings.isCentered) {
        int half = barCount / 2;
        for (int i = 0; i < barCount; i++) {
            if (i < half) {
                renderLevels[i] = levels[half - 1 - i];
            } else {
                renderLevels[i] = levels[i - half];
            }
        }
    } else {
        for (int i = 0; i < barCount; i++) {
            renderLevels[i] = levels[i];
        }
    }

    int iUsableWidth = (int)usableWidth;
    int totalGaps = barCount - 1;

    int gap = g_spacingValues[settings.spacingPreset];
    int barW = (iUsableWidth - (totalGaps * gap)) / barCount;

    // Safety check: if gap is too big for the window width, shrink gap so barW is at least 1
    if (barW < 1) {
        barW = 1;
        if (totalGaps > 0) {
            gap = (iUsableWidth - barCount) / totalGaps;
            if (gap < 0) gap = 0;
        }
    }

    int blockWidth = barCount * barW + totalGaps * gap;
    int startX = (int)marginX + (iUsableWidth - blockWidth) / 2;

    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    if (settings.graphStyle == STYLE_BARS) {
        glBegin(GL_QUADS);
        for (int i = 0; i < barCount; i++) {
            int x = startX + i * (barW + gap);
            float h = usableHeight * renderLevels[i], y = marginY;
            glColor4fv(settings.colorBot); glVertex2f((float)x, y); glVertex2f((float)(x + barW), y);
            glColor4fv(settings.colorTop); glVertex2f((float)(x + barW), y + h); glVertex2f((float)x, y + h);
        }
        glEnd();
    } else if (settings.graphStyle == STYLE_MIRROR) {
        float centerY = marginY + usableHeight * 0.5f;
        glBegin(GL_QUADS);
        for (int i = 0; i < barCount; i++) {
            int x = startX + i * (barW + gap);
            float h = usableHeight * renderLevels[i] * 0.5f;
            glColor4fv(settings.colorTop); glVertex2f((float)x, centerY - h); glVertex2f((float)(x + barW), centerY - h);
            glColor4fv(settings.colorBot); glVertex2f((float)(x + barW), centerY + h); glVertex2f((float)x, centerY + h);
        }
        glEnd();
    } else if (settings.graphStyle == STYLE_LINE) {
        glLineWidth(2.2f);
        glBegin(GL_LINE_STRIP);
        for (int i = 0; i < barCount; i++) {
            int x = startX + i * (barW + gap) + barW / 2;
            float y = marginY + usableHeight * renderLevels[i];
            glColor4fv(settings.colorTop); glVertex2f((float)x, y);
        }
        glEnd();
    } else if (settings.graphStyle == STYLE_AREA) {
        glBegin(GL_TRIANGLE_STRIP);
        for (int i = 0; i < barCount; i++) {
            int x = startX + i * (barW + gap) + barW / 2;
            float y = marginY + usableHeight * renderLevels[i];
            glColor4fv(settings.colorBot); glVertex2f((float)x, marginY);
            glColor4fv(settings.colorTop); glVertex2f((float)x, y);
        }
        glEnd();
    } else {
        glPointSize(3.4f);
        glBegin(GL_POINTS);
        for (int i = 0; i < barCount; i++) {
            int x = startX + i * (barW + gap) + barW / 2;
            float y = marginY + usableHeight * renderLevels[i];
            glColor4fv(settings.colorTop); glVertex2f((float)x, y);
        }
        glEnd();
    }
    SwapBuffers(hdc);
}

static void set_preset_colors(float r, float g, float b, float r2, float g2, float b2) {
    settings.colorMode = 0;
    settings.colorTop[0] = r; settings.colorTop[1] = g; settings.colorTop[2] = b;
    settings.colorBot[0] = r2; settings.colorBot[1] = g2; settings.colorBot[2] = b2;
    SaveSettings();
}

static void handle_command(HWND hwnd, WPARAM wParam) {
    int cmd = LOWORD(wParam);
    if (cmd == IDM_TRAY_SHOW) show_overlay(1);
    else if (cmd == IDM_TRAY_HIDE) show_overlay(0);
    else if (cmd == IDM_TRAY_EXIT) DestroyWindow(hwnd);
    else if (cmd == IDM_SOURCE_OUTPUT) { settings.sourceMode = SOURCE_OUTPUT; SaveSettings(); refresh_devices(); restart_audio(); }
    else if (cmd == IDM_SOURCE_INPUT) { settings.sourceMode = SOURCE_INPUT; SaveSettings(); refresh_devices(); restart_audio(); }
    else if (cmd == IDM_COLOR_CUSTOM_TOP) choose_color(settings.colorTop);
    else if (cmd == IDM_COLOR_CUSTOM_BOT) choose_color(settings.colorBot);
    else if (cmd == IDM_COLOR_MODE_RAINBOW) { settings.colorMode = 1; SaveSettings(); }
    else if (cmd == IDM_COLOR_MODE_STATIC) { settings.colorMode = 0; SaveSettings(); }
    else if (cmd == IDM_COLOR_PRESET_RED) set_preset_colors(1.0f, 0.2f, 0.2f, 0.6f, 0.0f, 0.0f);
    else if (cmd == IDM_COLOR_PRESET_GREEN) set_preset_colors(0.2f, 1.0f, 0.2f, 0.0f, 0.6f, 0.0f);
    else if (cmd == IDM_COLOR_PRESET_BLUE) set_preset_colors(0.3f, 0.6f, 1.0f, 0.1f, 0.2f, 0.8f);
    else if (cmd == IDM_COLOR_PRESET_GRAY) set_preset_colors(0.8f, 0.8f, 0.8f, 0.3f, 0.3f, 0.3f);
    else if (cmd == IDM_TOGGLE_CENTERED) { settings.isCentered = !settings.isCentered; SaveSettings(); }
    else if (cmd == IDM_TOGGLE_MINHEIGHT) { settings.keepVisible = !settings.keepVisible; SaveSettings(); }
    else if (cmd == IDM_TOGGLE_LOCK) {
        settings.isUnlocked = !settings.isUnlocked;
        LONG style = GetWindowLongA(hwnd, GWL_STYLE);
        if (settings.isUnlocked) {
            SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLongA(hwnd, GWL_EXSTYLE) & ~WS_EX_TRANSPARENT);
        } else {
            SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLongA(hwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
        }
        SetWindowLongA(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        apply_window_size();
        SaveSettings();
    }
    else if (cmd >= IDM_LAYOUT_BASE && cmd < IDM_LAYOUT_BASE + (int)(sizeof(g_layoutNames) / sizeof(g_layoutNames[0]))) { settings.layoutPreset = cmd - IDM_LAYOUT_BASE; apply_window_size(); }
    else if (cmd >= IDM_SIZE_BASE && cmd < IDM_SIZE_BASE + 4) { settings.sizePreset = cmd - IDM_SIZE_BASE; apply_window_size(); }
    else if (cmd >= IDM_BARS_BASE && cmd < IDM_BARS_BASE + (int)(sizeof(g_barValues) / sizeof(g_barValues[0]))) { settings.barCount = g_barValues[cmd - IDM_BARS_BASE]; SaveSettings(); }
    else if (cmd >= IDM_SPACING_BASE && cmd < IDM_SPACING_BASE + (int)(sizeof(g_spacingValues) / sizeof(g_spacingValues[0]))) { settings.spacingPreset = cmd - IDM_SPACING_BASE; SaveSettings(); }
    else if (cmd >= IDM_GAIN_BASE && cmd < IDM_GAIN_BASE + (int)(sizeof(g_gainValues) / sizeof(g_gainValues[0]))) { settings.gain = g_gainValues[cmd - IDM_GAIN_BASE]; SaveSettings(); }
    else if (cmd >= IDM_SMOOTH_BASE && cmd < IDM_SMOOTH_BASE + (int)(sizeof(g_smoothingValues) / sizeof(g_smoothingValues[0]))) { settings.smoothing = g_smoothingValues[cmd - IDM_SMOOTH_BASE]; SaveSettings(); }
    else if (cmd >= IDM_STYLE_BASE && cmd < IDM_STYLE_BASE + (int)(sizeof(g_styleNames) / sizeof(g_styleNames[0]))) { settings.graphStyle = cmd - IDM_STYLE_BASE; SaveSettings(); }
    else if (cmd >= IDM_VISUAL_BASE && cmd < IDM_VISUAL_BASE + (int)(sizeof(g_visibilityValues) / sizeof(g_visibilityValues[0]))) { settings.visibilityBoost = g_visibilityValues[cmd - IDM_VISUAL_BASE]; SaveSettings(); }
    else if (cmd >= IDM_ROTATE_BASE && cmd < IDM_ROTATE_BASE + (int)(sizeof(g_rotateValues) / sizeof(g_rotateValues[0]))) { settings.rotation = g_rotateValues[cmd - IDM_ROTATE_BASE]; SaveSettings(); }
    else if (cmd == IDM_TOGGLE_MIRROR) { settings.isMirrored = !settings.isMirrored; SaveSettings(); }
    else if (cmd == IDM_DEVICE_DEFAULT) {
        if (settings.sourceMode == SOURCE_OUTPUT) settings.renderDeviceIndex = -1;
        else settings.captureDeviceIndex = -1;
        restart_audio();
        SaveSettings();
    }
    else if (cmd >= IDM_DEVICE_BASE && cmd < IDM_DEVICE_BASE + MAX_DEVICES) {
        int idx = cmd - IDM_DEVICE_BASE;
        if (settings.sourceMode == SOURCE_OUTPUT) { settings.renderDeviceIndex = idx; restart_audio(); }
        else { settings.captureDeviceIndex = idx; restart_audio(); }
        SaveSettings();
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_taskbarCreatedMessage) { add_tray_icon(hwnd); return 0; }
    if (msg == WM_TRAYICON) { if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK) { show_tray_menu(hwnd); return 0; } }
    switch (msg) {
        case WM_NCHITTEST:
            if (settings.isUnlocked) {
                RECT r;
                GetClientRect(hwnd, &r);

                POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd, &p);

                int border = 8;

                if (p.x < border && p.y < border) return HTTOPLEFT;
                if (p.x > r.right - border && p.y < border) return HTTOPRIGHT;
                if (p.x < border && p.y > r.bottom - border) return HTBOTTOMLEFT;
                if (p.x > r.right - border && p.y > r.bottom - border) return HTBOTTOMRIGHT;

                if (p.x < border) return HTLEFT;
                if (p.x > r.right - border) return HTRIGHT;
                if (p.y < border) return HTTOP;
                if (p.y > r.bottom - border) return HTBOTTOM;

                return HTCAPTION;
            }
            break;
        case WM_EXITSIZEMOVE: {
            RECT r;
            GetWindowRect(hwnd, &r);
            settings.customX = r.left;
            settings.customY = r.top;
            settings.customWidth = r.right - r.left;
            settings.customHeight = r.bottom - r.top;
            settings.layoutPreset = LAYOUT_CUSTOM;
            settings.sizePreset = SIZE_CUSTOM;
            SaveSettings();
            return 0;
        }
        case WM_COMMAND: handle_command(hwnd, wParam); return 0;
        case WM_SYSCOMMAND: if ((wParam & 0xFFF0) == SC_CLOSE) { show_overlay(0); return 0; } break;
        case WM_DESTROY: remove_tray_icon(); PostQuitMessage(0); return 0;
        default: break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int main(void) {
    MSG msg; WNDCLASSA wc; PIXELFORMATDESCRIPTOR pfd; int pixelFormat;
    GetModuleFileNameA(NULL, iniPath, MAX_PATH);
    char *lastSlash = strrchr(iniPath, '\\');
    if (lastSlash) strcpy(lastSlash + 1, "musicgraph.ini");
    LoadSettings();

    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) return 1;
    g_hInstance = GetModuleHandleA(NULL);
    g_taskbarCreatedMessage = RegisterWindowMessageA("TaskbarCreated");

    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInstance;
    wc.lpszClassName = "MusicGraphOverlay";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hIcon = LoadIconA(g_hInstance, MAKEINTRESOURCEA(IDI_APP_ICON));
    if (!wc.hIcon) wc.hIcon = LoadIconA(NULL, IDI_APPLICATION);

    if (!RegisterClassA(&wc)) { CoUninitialize(); return 1; }

    int initW = settings.sizePreset == SIZE_CUSTOM ? settings.customWidth : g_sizeWidths[settings.sizePreset];
    int initH = settings.sizePreset == SIZE_CUSTOM ? settings.customHeight : g_sizeHeights[settings.sizePreset];

    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    if (!settings.isUnlocked) exStyle |= WS_EX_TRANSPARENT;

    DWORD style = WS_POPUP;

    g_hwnd = CreateWindowExA(exStyle, "MusicGraphOverlay", "MusicGraph", style, 100, 100, initW, initH, NULL, NULL, g_hInstance, NULL);
    if (!g_hwnd) { CoUninitialize(); return 1; }

    SetLayeredWindowAttributes(g_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    if (settings.visible) ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

    hdc = GetDC(g_hwnd);
    if (!hdc) { DestroyWindow(g_hwnd); CoUninitialize(); return 1; }
    ZeroMemory(&pfd, sizeof(pfd)); pfd.nSize = sizeof(pfd); pfd.nVersion = 1; pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER; pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32;
    pixelFormat = ChoosePixelFormat(hdc, &pfd);
    if (pixelFormat == 0 || !SetPixelFormat(hdc, pixelFormat, &pfd)) { ReleaseDC(g_hwnd, hdc); DestroyWindow(g_hwnd); CoUninitialize(); return 1; }
    hglrc = wglCreateContext(hdc);
    if (!hglrc || !wglMakeCurrent(hdc, hglrc)) { if (hglrc) wglDeleteContext(hglrc); ReleaseDC(g_hwnd, hdc); DestroyWindow(g_hwnd); CoUninitialize(); return 1; }

    refresh_devices();
    start_audio();

    add_tray_icon(g_hwnd);
    apply_window_size();

    while (1) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { release_audio(); wglMakeCurrent(NULL, NULL); wglDeleteContext(hglrc); ReleaseDC(g_hwnd, hdc); CoUninitialize(); return 0; }
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }

        if (++pollCounter >= 100) {
            check_default_device_changed();
            pollCounter = 0;
        }

        capture_audio();
        fft();

        RECT client;
        GetClientRect(g_hwnd, &client);
        render(client.right - client.left, client.bottom - client.top);

        Sleep(16);
    }
}