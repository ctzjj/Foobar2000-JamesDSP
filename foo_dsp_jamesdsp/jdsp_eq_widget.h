#pragma once
#include <windows.h>

struct EqBand {
    bool enabled = true;
    float frequency = 1000.0f;
    float gain = 0.0f;
    float q = 0.707f;
};

#define WC_EQCURVE L"JDSP_EQ_CURVE"

class JdspEqWidget {
public:
    JdspEqWidget();
    static bool RegisterClass(HMODULE hModule);

    void Create(HWND parent, int x, int y, int w, int h);
    HWND GetHWND() const { return m_hwnd; }

    void SetBands(const EqBand bands[10]);
    void GetBands(EqBand bands[10]) const;
    void SetSelectedBand(int band);
    int GetSelectedBand() const;
    const EqBand& GetSelectedBandData() const;

    void Refresh();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnPaint(HWND hwnd);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp();
    void OnMouseMove(int x, int y);
    void OnContextMenu(int x, int y);

    void DrawGrid(HDC hdc, const RECT& rc);
    void DrawCurve(HDC hdc, const RECT& rc);
    void DrawHandles(HDC hdc, const RECT& rc);
    POINT FreqGainToPixel(float freq, float gain, const RECT& rc);
    void PixelToFreqGain(int px, int py, float& freq, float& gain, const RECT& rc);
    int HitTest(int x, int y, const RECT& rc);

    HWND m_hwnd = NULL;
    EqBand m_bands[10];
    int m_selected_band = 0;
    bool m_dragging = false;
};
