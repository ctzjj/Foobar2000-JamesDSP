#pragma once
#include "jdsp_config_dialog.h"

namespace JdspConfig {

bool SaveToFile(const wchar_t* path, const JdspConfigDialog& dlg);
bool LoadFromFile(const wchar_t* path, JdspConfigDialog& dlg);

}
