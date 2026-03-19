#include "viewer.h"

AppContext g_ctx;

void CenterImage(bool resetZoom) {
    if (resetZoom) {
        g_ctx.zoomFactor = 1.0f;
    }
    g_ctx.rotationAngle = 0;
    g_ctx.offsetX = 0.0f;
    g_ctx.offsetY = 0.0f;
    g_ctx.isFlippedHorizontal = false;
    g_ctx.isGrayscale = false;
    g_ctx.isCropActive = false;
    g_ctx.isCropMode = false;
    g_ctx.isSelectingCropRect = false;
    g_ctx.isCropPending = false;
    g_ctx.brightness = 0.0f;
    g_ctx.contrast = 1.0f;
    g_ctx.saturation = 1.0f;
    {
        CriticalSectionLock lock(g_ctx.wicMutex);
        g_ctx.wicConverter = g_ctx.wicConverterOriginal;
        g_ctx.d2dBitmap = nullptr;
        g_ctx.animationD2DBitmaps.clear();
    }
    FitImageToWindow();
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

void SetActualSize() {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) return;
    g_ctx.zoomFactor = 1.0f;
    g_ctx.rotationAngle = 0;
    g_ctx.offsetX = 0.0f;
    g_ctx.offsetY = 0.0f;
    g_ctx.isFlippedHorizontal = false;
    g_ctx.isGrayscale = false;
    g_ctx.isCropActive = false;
    g_ctx.isCropMode = false;
    g_ctx.isSelectingCropRect = false;
    g_ctx.isCropPending = false;
    g_ctx.brightness = 0.0f;
    g_ctx.contrast = 1.0f;
    g_ctx.saturation = 1.0f;
    {
        CriticalSectionLock lock(g_ctx.wicMutex);
        g_ctx.wicConverter = g_ctx.wicConverterOriginal;
        g_ctx.d2dBitmap = nullptr;
        g_ctx.animationD2DBitmaps.clear();
    }
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

void CleanupCurrentImage() {
    // A. Stop background threads and timers
    CleanupLoadingThread();
    KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
    KillTimer(g_ctx.hWnd, OCR_MESSAGE_TIMER_ID);
    KillTimer(g_ctx.hWnd, AUTO_REFRESH_TIMER_ID);

    {
        CriticalSectionLock lock(g_ctx.wicMutex);

        // B. Securely zero raw pixel staging buffers before releasing
        for (auto& frame : g_ctx.stagedFrames) {
            SecureZeroByteVector(frame);
        }
        g_ctx.stagedFrames.clear();
        g_ctx.stagedFrames.shrink_to_fit();
        g_ctx.stagedDelays.clear();
        g_ctx.stagedDelays.shrink_to_fit();
        g_ctx.stagedWidth = 0;
        g_ctx.stagedHeight = 0;

        // C. Release WIC COM objects holding decoded pixel data
        g_ctx.wicConverter = nullptr;
        g_ctx.wicConverterOriginal = nullptr;
        g_ctx.animationFrameConverters.clear();
        g_ctx.animationFrameConverters.shrink_to_fit();
        g_ctx.preloadedNextConverter = nullptr;
        g_ctx.preloadedPrevConverter = nullptr;

        // D. Release Direct2D GPU bitmaps
        g_ctx.d2dBitmap = nullptr;
        g_ctx.animationD2DBitmaps.clear();
        g_ctx.animationD2DBitmaps.shrink_to_fit();
    }

    // E. Animation metadata
    g_ctx.animationFrameDelays.clear();
    g_ctx.animationFrameDelays.shrink_to_fit();
    g_ctx.currentAnimationFrame = 0;
    g_ctx.isAnimated = false;

    // F. Securely zero the loading file path
    SecureZeroWString(g_ctx.loadingFilePath);

    // G. Container format
    g_ctx.originalContainerFormat = GUID_NULL;

    // H. Loading state
    g_ctx.isLoading = false;
    g_ctx.lastWriteTime = { 0 };
}

void CleanupImageData() {
    // Clean up all image memory (pixels, bitmaps, converters)
    CleanupCurrentImage();

    // Securely zero all file path strings
    for (auto& path : g_ctx.imageFiles) {
        SecureZeroWString(path);
    }
    g_ctx.imageFiles.clear();
    g_ctx.imageFiles.shrink_to_fit();
    for (auto& path : g_ctx.stagedImageFiles) {
        SecureZeroWString(path);
    }
    g_ctx.stagedImageFiles.clear();
    g_ctx.stagedImageFiles.shrink_to_fit();
    SecureZeroWString(g_ctx.currentFilePathOverride);
    SecureZeroWString(g_ctx.currentDirectory);
    g_ctx.currentImageIndex = -1;
    g_ctx.stagedFoundIndex = -1;

    // Eyedropper / color picker derived data
    g_ctx.hoveredColor = 0;
    SecureZeroWString(g_ctx.colorStringRgb);
    SecureZeroWString(g_ctx.colorStringHex);
    g_ctx.didCopyColor = false;
    g_ctx.isEyedropperActive = false;

    // OCR message
    SecureZeroWString(g_ctx.ocrMessage);
    g_ctx.isOcrMessageVisible = false;

    // Edit state
    g_ctx.preloadedNextFormat = GUID_NULL;
    g_ctx.preloadedPrevFormat = GUID_NULL;
    g_ctx.brightness = 0.0f;
    g_ctx.contrast = 1.0f;
    g_ctx.saturation = 1.0f;
    g_ctx.savedBrightness = 0.0f;
    g_ctx.savedContrast = 1.0f;
    g_ctx.savedSaturation = 1.0f;
    g_ctx.rotationAngle = 0;
    g_ctx.isFlippedHorizontal = false;
    g_ctx.isGrayscale = false;
    g_ctx.isCropActive = false;
    g_ctx.isCropMode = false;
    g_ctx.isSelectingCropRect = false;
    g_ctx.isCropPending = false;
    g_ctx.cropRectWindow = { 0 };
    g_ctx.cropRectLocal = { 0 };
    g_ctx.cropStartPoint = { 0 };
    g_ctx.isSelectingOcrRect = false;
    g_ctx.isDraggingOcrRect = false;
    g_ctx.ocrStartPoint = { 0 };
    g_ctx.ocrRectWindow = { 0 };

    // View state
    g_ctx.zoomFactor = 1.0f;
    g_ctx.offsetX = 0.0f;
    g_ctx.offsetY = 0.0f;
    g_ctx.isOsdVisible = false;

    // Window title — remove file path
    if (g_ctx.hWnd) {
        SetWindowTextW(g_ctx.hWnd, L"Minimal Image Viewer");
        InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    g_ctx.hInst = hInstance;

    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    PathAppendW(exePath, L"minimal_image_viewer_settings.ini");
    g_ctx.settingsPath = exePath;

    RECT startupRect;
    ReadSettings(g_ctx.settingsPath, startupRect, g_ctx.startFullScreen, g_ctx.enforceSingleInstance, g_ctx.alwaysOnTop);
    if (IsRectEmpty(&startupRect)) {
        startupRect = { CW_USEDEFAULT, CW_USEDEFAULT, 800, 600 };
    }

    if (g_ctx.enforceSingleInstance) {
        HWND existingWnd = FindWindowW(L"MinimalImageViewer", nullptr);
        if (existingWnd) {
            SetForegroundWindow(existingWnd);
            if (IsIconic(existingWnd)) {
                ShowWindow(existingWnd, SW_RESTORE);
            }
            if (lpCmdLine && *lpCmdLine) {
                COPYDATASTRUCT cds{};
                cds.dwData = 1;
                cds.cbData = (static_cast<DWORD>(wcslen(lpCmdLine)) + 1) * sizeof(wchar_t);
                cds.lpData = lpCmdLine;
                SendMessage(existingWnd, WM_COPYDATA, reinterpret_cast<WPARAM>(hInstance), reinterpret_cast<LPARAM>(&cds));
            }
            return 0;
        }
    }

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    InitializeCriticalSection(&g_ctx.wicMutex);
    InitializeCriticalSection(&g_ctx.preloadMutex);

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_ctx.wicFactory)))) {
        MessageBoxW(nullptr, L"Failed to create WIC Imaging Factory.", L"Error", MB_OK | MB_ICONERROR);
        DeleteCriticalSection(&g_ctx.wicMutex);
        DeleteCriticalSection(&g_ctx.preloadMutex);
        winrt::uninit_apartment();
        return 1;
    }

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_ctx.d2dFactory))) {
        MessageBoxW(nullptr, L"Failed to create Direct2D Factory.", L"Error", MB_OK | MB_ICONERROR);
        DeleteCriticalSection(&g_ctx.wicMutex);
        DeleteCriticalSection(&g_ctx.preloadMutex);
        winrt::uninit_apartment();
        return 1;
    }

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&g_ctx.writeFactory)))) {
        MessageBoxW(nullptr, L"Failed to create DirectWrite Factory.", L"Error", MB_OK | MB_ICONERROR);
        DeleteCriticalSection(&g_ctx.wicMutex);
        DeleteCriticalSection(&g_ctx.preloadMutex);
        winrt::uninit_apartment();
        return 1;
    }

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = NULL;
    wcex.lpszClassName = L"MinimalImageViewer";
    RegisterClassExW(&wcex);

    DWORD exStyle = (g_ctx.alwaysOnTop) ? WS_EX_TOPMOST : 0;

    g_ctx.hWnd = CreateWindowExW(
        exStyle,
        wcex.lpszClassName,
        L"Minimal Image Viewer",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        startupRect.left, startupRect.top,
        (startupRect.left == CW_USEDEFAULT) ? 800 : (startupRect.right - startupRect.left),
        (startupRect.top == CW_USEDEFAULT) ? 600 : (startupRect.bottom - startupRect.top),
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_ctx.hWnd) {
        MessageBoxW(nullptr, L"Failed to create window.", L"Error", MB_OK | MB_ICONERROR);
        DeleteCriticalSection(&g_ctx.wicMutex);
        DeleteCriticalSection(&g_ctx.preloadMutex);
        winrt::uninit_apartment();
        return 1;
    }

    SetWindowLongPtr(g_ctx.hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&g_ctx));
    DragAcceptFiles(g_ctx.hWnd, TRUE);

    if (g_ctx.startFullScreen) {
        ToggleFullScreen();
    }

    g_ctx.isInitialized = true;

    ShowWindow(g_ctx.hWnd, nCmdShow);
    UpdateWindow(g_ctx.hWnd);

    if (lpCmdLine && *lpCmdLine) {
        wchar_t filePath[MAX_PATH];
        wcscpy_s(filePath, MAX_PATH, lpCmdLine);
        PathUnquoteSpacesW(filePath);
        LoadImageFromFile(filePath);
    }

    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CleanupImageData();
    g_ctx.textBrush = nullptr;
    g_ctx.textFormat = nullptr;
    g_ctx.renderTarget = nullptr;
    g_ctx.writeFactory = nullptr;
    g_ctx.d2dFactory = nullptr;
    g_ctx.wicFactory = nullptr;

    DeleteCriticalSection(&g_ctx.wicMutex);
    DeleteCriticalSection(&g_ctx.preloadMutex);
    winrt::uninit_apartment();
    return static_cast<int>(msg.wParam);
}