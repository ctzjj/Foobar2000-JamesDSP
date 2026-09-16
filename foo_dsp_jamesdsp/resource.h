#pragma once

// Main dialog
#define IDD_JDSP_CONFIG         1000

// Top toolbar controls
#define IDC_COMBO_LANGUAGE      1001
#define IDC_BTN_SAVE_CONFIG     1002
#define IDC_BTN_LOAD_CONFIG     1003
#define IDC_BTN_RESET_ALL       1004

// Tab control
#define IDC_TAB_MAIN            1100

// Tab: Modules
#define IDD_TAB_MODULES         1200
#define IDC_CHK_ANALOG          1201
#define IDC_CHK_BS2B            1202
#define IDC_CHK_DDC             1203
#define IDC_CHK_LIMITER         1204
#define IDC_CHK_COMPRESSOR      1205
#define IDC_CHK_CONVOLVER       1206
#define IDC_CHK_REVERB          1207
#define IDC_CHK_BASS_BOOST      1208
#define IDC_CHK_STEREO          1209
#define IDC_CHK_IIR             1210
#define IDC_CHK_SPECTRUM        1211
#define IDC_CHK_DYNAMIC_SYS     1212
#define IDC_CHK_EEL2            1213

// Tab: EQ
#define IDD_TAB_EQ              1300
#define IDC_EQ_CURVE            1301
#define IDC_COMBO_EQ_BAND       1302
#define IDC_EDIT_EQ_FREQ        1303
#define IDC_EDIT_EQ_GAIN        1304
#define IDC_EDIT_EQ_Q           1305
#define IDC_BTN_EQ_RESET        1306
// 10 vertical band gain sliders (IDC_SLIDER_EQ_BAND0 + 0..9)
#define IDC_SLIDER_EQ_BAND0     1310
#define IDC_SLIDER_EQ_BAND_LAST 1319
// per-band gain value labels (IDC_STATIC_EQ_BANDVAL0 + 0..9)
#define IDC_STATIC_EQ_BANDVAL0  1320
// per-band frequency labels (IDC_STATIC_EQ_BANDFREQ0 + 0..9)
#define IDC_STATIC_EQ_BANDFREQ0 1330

// Tab: Dynamics
#define IDD_TAB_DYNAMICS        1400
#define IDC_CHK_COMP            1401
#define IDC_SLIDER_COMP_THRESH  1402
#define IDC_SLIDER_COMP_RATIO   1403
#define IDC_SLIDER_COMP_ATTACK  1404
#define IDC_SLIDER_COMP_RELEASE 1405
#define IDC_STATIC_COMP_THRESH  1410
#define IDC_STATIC_COMP_RATIO   1411
#define IDC_STATIC_COMP_ATTACK  1412
#define IDC_STATIC_COMP_RELEASE 1413
#define IDC_CHK_LIM             1416
#define IDC_SLIDER_LIM_THRESH   1417
#define IDC_SLIDER_LIM_RELEASE  1418
#define IDC_STATIC_LIM_THRESH   1419
#define IDC_STATIC_LIM_RELEASE  1420
#define IDC_CHK_DDC_ENABLE      1421
#define IDC_SLIDER_DDC_STRENGTH 1422
#define IDC_STATIC_DDC_STRENGTH 1423
#define IDC_EDIT_DDC_PROFILE    1424
#define IDC_BTN_DDC_BROWSE      1425

// Tab: Effects
#define IDD_TAB_EFFECTS         1500
#define IDC_CHK_BASS_EFFECT     1501
#define IDC_SLIDER_BASS_BOOST   1502
#define IDC_STATIC_BASS_BOOST   1503
#define IDC_SLIDER_BASS_FREQ    1504
#define IDC_STATIC_BASS_FREQ    1505
#define IDC_CHK_STEREO_EFFECT   1506
#define IDC_SLIDER_STEREO_WIDTH 1507
#define IDC_STATIC_STEREO_WIDTH 1508
#define IDC_CHK_REVERB_EFFECT   1509
#define IDC_SLIDER_REVERB_ROOM  1510
#define IDC_STATIC_REVERB_ROOM  1511
#define IDC_SLIDER_REVERB_DAMP  1512
#define IDC_STATIC_REVERB_DAMP  1513
#define IDC_SLIDER_REVERB_WET   1514
#define IDC_STATIC_REVERB_WET   1515
#define IDC_CHK_TUBE_EFFECT     1516
#define IDC_SLIDER_TUBE_DRIVE   1517
#define IDC_STATIC_TUBE_DRIVE   1518
#define IDC_CHK_BS2B_EFFECT     1519
#define IDC_SLIDER_BS2B_FEED    1520
#define IDC_STATIC_BS2B_FEED    1521
#define IDC_SLIDER_BS2B_FREQ    1522
#define IDC_STATIC_BS2B_FREQ    1523

// Tab: Convolver
#define IDD_TAB_CONVOLVER       1600
#define IDC_CHK_CONV_ENABLE     1601
#define IDC_EDIT_CONV_IR        1602
#define IDC_BTN_CONV_BROWSE     1603
#define IDC_STATIC_CONV_INFO    1604
#define IDC_SLIDER_CONV_GAIN    1605
#define IDC_STATIC_CONV_GAIN    1606

// Tab: Script
#define IDD_TAB_SCRIPT          1700
#define IDC_CHK_SCRIPT_ENABLE   1701
#define IDC_EDIT_SCRIPT         1702
#define IDC_BTN_SCRIPT_LOAD     1703
#define IDC_BTN_SCRIPT_SAVE     1704
#define IDC_STATIC_SCRIPT_STATUS 1705

// Localizable static labels (IDL_*)
#define IDL_EQ_BAND             1801
#define IDL_EQ_FREQ             1802
#define IDL_EQ_FREQ_UNIT        1803
#define IDL_EQ_Q                1804
#define IDL_EQ_Q_UNIT           1805

#define IDL_DYN_COMP_GRP        1810
#define IDL_DYN_COMP_THRESH     1811
#define IDL_DYN_COMP_RATIO      1812
#define IDL_DYN_COMP_ATTACK     1813
#define IDL_DYN_COMP_RELEASE    1814
#define IDL_DYN_LIM_GRP         1815
#define IDL_DYN_LIM_THRESH      1816
#define IDL_DYN_LIM_RELEASE     1817
#define IDL_DYN_DDC_GRP         1818
#define IDL_DYN_DDC_STRENGTH    1819
#define IDL_DYN_DDC_PROFILE     1820

#define IDL_FX_BASS_GRP         1830
#define IDL_FX_BASS_BOOST       1831
#define IDL_FX_BASS_FREQ        1832
#define IDL_FX_STEREO_GRP       1833
#define IDL_FX_STEREO_WIDTH     1834
#define IDL_FX_REVERB_GRP       1835
#define IDL_FX_REVERB_ROOM      1836
#define IDL_FX_REVERB_DAMP      1837
#define IDL_FX_REVERB_WET       1838
#define IDL_FX_TUBE_GRP         1839
#define IDL_FX_TUBE_DRIVE       1840
#define IDL_FX_BS2B_GRP         1841
#define IDL_FX_BS2B_FEED        1842
#define IDL_FX_BS2B_FREQ        1843

#define IDL_CV_IR_GRP           1850
#define IDL_CV_FILE             1851
#define IDL_CV_GAIN_GRP         1852
#define IDL_CV_GAIN             1853

#define IDL_SC_SCRIPT           1860
#define IDL_SC_STATUS           1861

// Buttons
#define IDOK                    1
#define IDCANCEL                2
