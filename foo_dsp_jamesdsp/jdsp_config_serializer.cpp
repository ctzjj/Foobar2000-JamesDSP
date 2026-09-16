#include "stdafx.h"
#include "jdsp_config_serializer.h"
#include <cstdio>
#include <string>

bool JdspConfig::SaveToFile(const wchar_t* path, const JdspConfigDialog& dlg) {
    std::string blob = dlg.SerializeSettings();

    FILE* f = NULL;
    _wfopen_s(&f, path, L"wb");
    if (!f) return false;

    bool ok = (fwrite(blob.data(), 1, blob.size(), f) == blob.size());
    fclose(f);
    return ok;
}

bool JdspConfig::LoadFromFile(const wchar_t* path, JdspConfigDialog& dlg) {
    FILE* f = NULL;
    _wfopen_s(&f, path, L"rb");
    if (!f) return false;

    std::string blob;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        blob.append(buf, n);
    }
    fclose(f);

    dlg.DeserializeSettings(blob);
    return true;
}
