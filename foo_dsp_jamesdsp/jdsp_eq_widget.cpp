#include "stdafx.h"
#include "jdsp_eq_widget.h"

JdspEqWidget::JdspEqWidget() {
    float freqs[] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    for (int i = 0; i < 10; i++) {
        m_bands[i].frequency = freqs[i];
    }
}

void JdspEqWidget::SetBands(const EqBand bands[10]) {
    memcpy(m_bands, bands, sizeof(m_bands));
}

void JdspEqWidget::GetBands(EqBand bands[10]) const {
    memcpy(bands, m_bands, sizeof(m_bands));
}
