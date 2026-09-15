#pragma once
#include <windows.h>

struct EqBand {
    bool enabled = true;
    float frequency = 1000.0f;
    float gain = 0.0f;
    float q = 0.707f;
};

class JdspEqWidget {
public:
    JdspEqWidget();
    void SetBands(const EqBand bands[10]);
    void GetBands(EqBand bands[10]) const;
private:
    EqBand m_bands[10];
};
