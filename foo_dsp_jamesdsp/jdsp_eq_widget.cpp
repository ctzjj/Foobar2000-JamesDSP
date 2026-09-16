#include "stdafx.h"
#include "jdsp_eq_widget.h"
#include "resource.h"
#include <cmath>

JdspEqWidget::JdspEqWidget() {
    // The library's default 15 point axis (multimodalEQ.c).
    static const float freqs[JDSP_EQ_BANDS] = { 25, 40, 63, 100, 160, 250, 400, 630,
                                                1000, 1600, 2500, 4000, 6300, 10000, 16000 };
    for (int i = 0; i < JDSP_EQ_BANDS; i++) {
        m_bands[i].frequency = freqs[i];
    }
}

bool JdspEqWidget::RegisterClass(HMODULE hModule) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hModule;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = WC_EQCURVE;
    return RegisterClassExW(&wc) != 0;
}

void JdspEqWidget::Create(HWND parent, int x, int y, int w, int h) {
    HMODULE hMod = NULL;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&JdspEqWidget::RegisterClass, &hMod);
    m_hwnd = CreateWindowExW(0, WC_EQCURVE, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x, y, w, h, parent, (HMENU)(INT_PTR)IDC_EQ_CURVE,
        hMod, this);
}

void JdspEqWidget::SetBands(const EqBand bands[JDSP_EQ_BANDS]) {
    memcpy(m_bands, bands, sizeof(m_bands));
    Refresh();
}

void JdspEqWidget::GetBands(EqBand bands[JDSP_EQ_BANDS]) const {
    memcpy(bands, m_bands, sizeof(m_bands));
}

void JdspEqWidget::SetSelectedBand(int band) {
    if (band >= 0 && band < JDSP_EQ_BANDS) {
        m_selected_band = band;
        Refresh();
    }
}

int JdspEqWidget::GetSelectedBand() const { return m_selected_band; }
const EqBand& JdspEqWidget::GetSelectedBandData() const { return m_bands[m_selected_band]; }

void JdspEqWidget::Refresh() {
    if (m_hwnd) InvalidateRect(m_hwnd, NULL, TRUE);
}

LRESULT CALLBACK JdspEqWidget::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    JdspEqWidget* self = NULL;
    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        self = (JdspEqWidget*)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        return 0;
    }
    self = (JdspEqWidget*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!self) return DefWindowProc(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_PAINT:
        self->OnPaint(hwnd);
        return 0;
    case WM_LBUTTONDOWN:
        self->OnLButtonDown(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_LBUTTONUP:
        self->OnLButtonUp();
        return 0;
    case WM_MOUSEMOVE:
        self->OnMouseMove(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_RBUTTONUP:
        self->OnContextMenu(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void JdspEqWidget::OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    FillRect(memDC, &rc, (HBRUSH)(COLOR_WINDOW + 1));
    DrawGrid(memDC, rc);
    DrawCurve(memDC, rc);
    DrawHandles(memDC, rc);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

void JdspEqWidget::DrawGrid(HDC hdc, const RECT& rc) {
    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
    HPEN oldPen = (HPEN)SelectObject(hdc, gridPen);

    for (int i = 0; i <= 10; i++) {
        int x = rc.left + MulDiv(i, rc.right - rc.left, 10);
        MoveToEx(hdc, x, rc.top, NULL);
        LineTo(hdc, x, rc.bottom);
    }
    for (int i = 0; i <= 8; i++) {
        int y = rc.top + MulDiv(i, rc.bottom - rc.top, 8);
        MoveToEx(hdc, rc.left, y, NULL);
        LineTo(hdc, rc.right, y);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(gridPen);

    HPEN zeroPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
    oldPen = (HPEN)SelectObject(hdc, zeroPen);
    int zeroY = rc.top + (rc.bottom - rc.top) / 2;
    MoveToEx(hdc, rc.left, zeroY, NULL);
    LineTo(hdc, rc.right, zeroY);
    SelectObject(hdc, oldPen);
    DeleteObject(zeroPen);

    SetBkMode(hdc, TRANSPARENT);
    // One label per band, in the library's 15 point axis order.
    static const wchar_t* freqLabels[JDSP_EQ_BANDS] = {
        L"25", L"40", L"63", L"100", L"160", L"250", L"400", L"630",
        L"1k", L"1.6k", L"2.5k", L"4k", L"6.3k", L"10k", L"16k"
    };
    for (int i = 0; i < JDSP_EQ_BANDS; i++) {
        POINT p = FreqGainToPixel(m_bands[i].frequency, 0, rc);
        TextOutW(hdc, p.x - 8, rc.bottom - 14, freqLabels[i], (int)wcslen(freqLabels[i]));
    }
    TextOutW(hdc, 2, zeroY - 7, L"0dB", 3);
    TextOutW(hdc, 2, rc.top + 2, L"+24", 3);
    TextOutW(hdc, 2, rc.bottom - 16, L"-24", 3);
}

void JdspEqWidget::DrawCurve(HDC hdc, const RECT& rc) {
    if (rc.right <= rc.left || rc.bottom <= rc.top) return;

    // The library interpolates through its 15 axis points, so the drawn curve goes
    // through the same points. A monotone cubic (Fritsch-Carlson) interpolation is
    // used in log-frequency: it is smooth but never overshoots, so a single band
    // moved up produces a smooth local bump instead of a sharp corner.
    const int n = JDSP_EQ_BANDS;
    double x[n], y[n];
    for (int i = 0; i < n; i++) {
        x[i] = log10((double)m_bands[i].frequency);
        y[i] = m_bands[i].enabled ? (double)m_bands[i].gain : 0.0;
    }

    double h[n - 1], delta[n - 1], m[n];
    for (int i = 0; i < n - 1; i++) {
        h[i] = x[i + 1] - x[i];
        delta[i] = (h[i] > 0.0) ? (y[i + 1] - y[i]) / h[i] : 0.0;
    }
    m[0] = delta[0];
    m[n - 1] = delta[n - 2];
    for (int i = 1; i < n - 1; i++) {
        if (delta[i - 1] * delta[i] <= 0.0) {
            m[i] = 0.0;
        } else {
            double w1 = 2.0 * h[i] + h[i - 1];
            double w2 = h[i] + 2.0 * h[i - 1];
            m[i] = (w1 + w2) / (w1 / delta[i - 1] + w2 / delta[i]);
        }
    }

    HPEN curvePen = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
    HPEN oldPen = (HPEN)SelectObject(hdc, curvePen);

    bool first = true;
    for (int px = rc.left; px <= rc.right; px++) {
        double t = (double)(px - rc.left) / (double)(rc.right - rc.left);
        double logF = log10(20.0) + t * (log10(20000.0) - log10(20.0));

        double gain;
        if (logF <= x[0]) {
            gain = y[0];
        } else if (logF >= x[n - 1]) {
            gain = y[n - 1];
        } else {
            int seg = 0;
            while (seg < n - 2 && x[seg + 1] < logF) seg++;
            double hh = h[seg];
            double u = (hh > 0.0) ? (logF - x[seg]) / hh : 0.0;
            double u2 = u * u, u3 = u2 * u;
            double h00 = 2.0 * u3 - 3.0 * u2 + 1.0;
            double h10 = u3 - 2.0 * u2 + u;
            double h01 = -2.0 * u3 + 3.0 * u2;
            double h11 = u3 - u2;
            gain = h00 * y[seg] + h10 * hh * m[seg] +
                   h01 * y[seg + 1] + h11 * hh * m[seg + 1];
        }
        if (gain > JDSP_EQ_GAIN_MAX) gain = JDSP_EQ_GAIN_MAX;
        if (gain < -JDSP_EQ_GAIN_MAX) gain = -JDSP_EQ_GAIN_MAX;

        int py = rc.top + (int)((JDSP_EQ_GAIN_MAX - gain) /
                                (2.0f * JDSP_EQ_GAIN_MAX) * (rc.bottom - rc.top));
        if (first) { MoveToEx(hdc, px, py, NULL); first = false; }
        else LineTo(hdc, px, py);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(curvePen);
}

void JdspEqWidget::DrawHandles(HDC hdc, const RECT& rc) {
    for (int i = 0; i < JDSP_EQ_BANDS; i++) {
        POINT p = FreqGainToPixel(m_bands[i].frequency, m_bands[i].gain, rc);
        HBRUSH br = CreateSolidBrush(i == m_selected_band ? RGB(255, 80, 80) : RGB(0, 120, 215));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, br);
        Ellipse(hdc, p.x - 5, p.y - 5, p.x + 5, p.y + 5);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBr);
        DeleteObject(pen);
        DeleteObject(br);
    }
}

POINT JdspEqWidget::FreqGainToPixel(float freq, float gain, const RECT& rc) {
    POINT p;
    if (rc.right <= rc.left || rc.bottom <= rc.top) { p.x = rc.left; p.y = rc.top; return p; }
    float logMin = log10f(20.0f);
    float logMax = log10f(20000.0f);
    float logFreq = log10f(max(20.0f, min(20000.0f, freq)));
    p.x = rc.left + (int)((logFreq - logMin) / (logMax - logMin) * (rc.right - rc.left));
    p.y = rc.top + (int)((JDSP_EQ_GAIN_MAX - max(-JDSP_EQ_GAIN_MAX, min(JDSP_EQ_GAIN_MAX, gain))) /
                         (2.0f * JDSP_EQ_GAIN_MAX) * (rc.bottom - rc.top));
    return p;
}

void JdspEqWidget::PixelToFreqGain(int px, int py, float& freq, float& gain, const RECT& rc) {
    if (rc.right <= rc.left || rc.bottom <= rc.top) { freq = 1000; gain = 0; return; }
    float logMin = log10f(20.0f);
    float logMax = log10f(20000.0f);
    float t = (float)(px - rc.left) / (float)(rc.right - rc.left);
    t = max(0.0f, min(1.0f, t));
    freq = powf(10.0f, logMin + t * (logMax - logMin));
    gain = JDSP_EQ_GAIN_MAX - (float)(py - rc.top) / (float)(rc.bottom - rc.top) * 2.0f * JDSP_EQ_GAIN_MAX;
    gain = max(-JDSP_EQ_GAIN_MAX, min(JDSP_EQ_GAIN_MAX, gain));
}

int JdspEqWidget::HitTest(int x, int y, const RECT& rc) {
    for (int i = 0; i < JDSP_EQ_BANDS; i++) {
        POINT p = FreqGainToPixel(m_bands[i].frequency, m_bands[i].gain, rc);
        int dx = x - p.x, dy = y - p.y;
        if (dx * dx + dy * dy < 100) return i;
    }
    return -1;
}

void JdspEqWidget::OnLButtonDown(int x, int y) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int hit = HitTest(x, y, rc);
    if (hit >= 0) {
        m_selected_band = hit;
        m_dragging = true;
        SetCapture(m_hwnd);
        Refresh();
        NMHDR nm = { m_hwnd, IDC_EQ_CURVE, NM_CLICK };
        SendMessage(GetParent(m_hwnd), WM_NOTIFY, IDC_EQ_CURVE, (LPARAM)&nm);
    }
}

void JdspEqWidget::OnLButtonUp() {
    if (m_dragging) {
        m_dragging = false;
        ReleaseCapture();
    }
}

void JdspEqWidget::OnMouseMove(int x, int y) {
    if (!m_dragging) return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    float freq, gain;
    PixelToFreqGain(x, y, freq, gain, rc);
    m_bands[m_selected_band].frequency = max(20.0f, min(20000.0f, freq));
    m_bands[m_selected_band].gain = gain;
    Refresh();
    NMHDR nm = { m_hwnd, IDC_EQ_CURVE, NM_CLICK };
    SendMessage(GetParent(m_hwnd), WM_NOTIFY, IDC_EQ_CURVE, (LPARAM)&nm);
}

void JdspEqWidget::OnContextMenu(int x, int y) {
    POINT pt = {x, y};
    ClientToScreen(m_hwnd, &pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Reset band to 0 dB");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (m_bands[m_selected_band].enabled ? MF_CHECKED : MF_UNCHECKED), 2, L"Enabled");
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, NULL);
    DestroyMenu(menu);

    if (cmd == 1) m_bands[m_selected_band].gain = 0.0f;
    else if (cmd == 2) m_bands[m_selected_band].enabled = !m_bands[m_selected_band].enabled;
    if (cmd) {
        Refresh();
        NMHDR nm = { m_hwnd, IDC_EQ_CURVE, NM_CLICK };
        SendMessage(GetParent(m_hwnd), WM_NOTIFY, IDC_EQ_CURVE, (LPARAM)&nm);
    }
}
