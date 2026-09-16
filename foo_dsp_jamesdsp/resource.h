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

// Tab: Modules (order must match the JdspModule enum in jdsp_engine.h)
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
#define IDC_CHK_EEL2            1213

// Tab: Equalizer
#define IDD_TAB_EQ              1300
#define IDC_EQ_CURVE            1301
#define IDC_COMBO_EQ_BAND       1302
#define IDC_EDIT_EQ_FREQ        1303
#define IDC_EDIT_EQ_GAIN        1304
#define IDC_COMBO_EQ_FILTERTYPE 1305
#define IDC_COMBO_EQ_INTERP     1306
#define IDC_BTN_EQ_RESET        1307
// 15 vertical band gain sliders (IDC_SLIDER_EQ_BAND0 + 0..14)
#define IDC_SLIDER_EQ_BAND0     1310
#define IDC_SLIDER_EQ_BAND_LAST 1324
// per-band gain value labels (IDC_STATIC_EQ_BANDVAL0 + 0..14)
#define IDC_STATIC_EQ_BANDVAL0  1330
// per-band frequency labels (IDC_STATIC_EQ_BANDFREQ0 + 0..14)
#define IDC_STATIC_EQ_BANDFREQ0 1345

// Tab: Dynamics
#define IDD_TAB_DYNAMICS        1400
#define IDC_CHK_COMP            1401
#define IDC_SLIDER_COMP_TIME    1402
#define IDC_SLIDER_COMP_GRAN    1403
#define IDC_SLIDER_COMP_TFRES   1404
#define IDC_STATIC_COMP_TIME    1410
#define IDC_STATIC_COMP_GRAN    1411
#define IDC_STATIC_COMP_TFRES   1412
#define IDC_CHK_LIM             1416
#define IDC_SLIDER_LIM_THRESH   1417
#define IDC_SLIDER_LIM_RELEASE  1418
#define IDC_STATIC_LIM_THRESH   1419
#define IDC_STATIC_LIM_RELEASE  1420
#define IDC_CHK_DDC_ENABLE      1421
#define IDC_EDIT_DDC_PROFILE    1424
#define IDC_BTN_DDC_BROWSE      1425
// 7 compressor band gain sliders (IDC_SLIDER_COMP_BAND0 + 0..6)
#define IDC_SLIDER_COMP_BAND0   1430
#define IDC_STATIC_COMP_BAND0   1440
// 7 compressor band value labels (IDC_STATIC_COMP_BANDVAL0 + 0..6)
#define IDC_STATIC_COMP_BANDVAL0 1450

// Tab: Effects
#define IDD_TAB_EFFECTS         1500
#define IDC_CHK_BASS_EFFECT     1501
#define IDC_SLIDER_BASS_BOOST   1502
#define IDC_STATIC_BASS_BOOST   1503
#define IDC_CHK_STEREO_EFFECT   1506
#define IDC_SLIDER_STEREO_WIDTH 1507
#define IDC_STATIC_STEREO_WIDTH 1508
#define IDC_CHK_REVERB_EFFECT   1509
#define IDC_COMBO_REVERB_PRESET 1510
#define IDC_CHK_TUBE_EFFECT     1516
#define IDC_SLIDER_TUBE_DRIVE   1517
#define IDC_STATIC_TUBE_DRIVE   1518
#define IDC_CHK_BS2B_EFFECT     1519
#define IDC_COMBO_BS2B_MODE     1520
#define IDC_SLIDER_OUTPUT_GAIN  1522
#define IDC_STATIC_OUTPUT_GAIN  1523
// Reverb detail parameters
#define IDC_SLIDER_REVERB_WET   1530
#define IDC_STATIC_REVERB_WET   1531
#define IDC_SLIDER_REVERB_DRY   1532
#define IDC_STATIC_REVERB_DRY   1533
#define IDC_SLIDER_REVERB_WIDTH 1534
#define IDC_STATIC_REVERB_WIDTH 1535
#define IDC_SLIDER_REVERB_RT60  1536
#define IDC_STATIC_REVERB_RT60  1537
#define IDC_SLIDER_REVERB_DAMP  1538
#define IDC_STATIC_REVERB_DAMP  1539
#define IDC_SLIDER_REVERB_BASS  1540
#define IDC_STATIC_REVERB_BASS  1541
#define IDC_SLIDER_REVERB_PREDELAY 1542
#define IDC_STATIC_REVERB_PREDELAY 1543
#define IDC_SLIDER_REVERB_ER    1544
#define IDC_STATIC_REVERB_ER    1545

// Tab: Convolver
#define IDD_TAB_CONVOLVER       1600
#define IDC_CHK_CONV_ENABLE     1601
#define IDC_EDIT_CONV_IR        1602
#define IDC_BTN_CONV_BROWSE     1603
#define IDC_STATIC_CONV_INFO    1604

// Tab: Script
#define IDD_TAB_SCRIPT          1700
#define IDC_CHK_SCRIPT_ENABLE   1701
#define IDC_EDIT_SCRIPT         1702
#define IDC_BTN_SCRIPT_LOAD     1703
#define IDC_BTN_SCRIPT_SAVE     1704
#define IDC_STATIC_SCRIPT_STATUS 1705

// Tab: Spectrum Extender
#define IDD_TAB_SPECTRUM        1750
#define IDC_CHK_SPECTRUM_ENABLE 1751
#define IDC_EDIT_SPECTRUM_FILE  1752
#define IDC_BTN_SPECTRUM_BROWSE 1753
#define IDC_STATIC_SPECTRUM_INFO 1754

// Localizable static labels (IDL_*)
#define IDL_EQ_GRP              1801
#define IDL_EQ_BAND             1802
#define IDL_EQ_FREQ             1803
#define IDL_EQ_GAIN             1804
#define IDL_EQ_FILTERTYPE       1805
#define IDL_EQ_INTERP           1806

#define IDL_DYN_COMP_GRP        1810
#define IDL_DYN_COMP_TIME       1811
#define IDL_DYN_COMP_GRAN       1812
#define IDL_DYN_COMP_TFRES      1813
#define IDL_DYN_COMP_BANDS      1814
#define IDL_DYN_LIM_GRP         1815
#define IDL_DYN_LIM_THRESH      1816
#define IDL_DYN_LIM_RELEASE     1817
#define IDL_DYN_COMP_BANDGRP    1819
#define IDL_DYN_DDC_GRP         1818
#define IDL_DYN_DDC_PROFILE     1820

#define IDL_FX_BASS_GRP         1830
#define IDL_FX_BASS_BOOST       1831
#define IDL_FX_STEREO_GRP       1833
#define IDL_FX_STEREO_WIDTH     1834
#define IDL_FX_REVERB_GRP       1835
#define IDL_FX_REVERB_PRESET    1836
#define IDL_FX_TUBE_GRP         1839
#define IDL_FX_TUBE_DRIVE       1840
#define IDL_FX_BS2B_GRP         1841
#define IDL_FX_BS2B_MODE        1842
#define IDL_FX_OUTPUT_GRP       1843
#define IDL_FX_OUTPUT_GAIN      1844

#define IDL_CV_IR_GRP           1850
#define IDL_CV_FILE             1851
#define IDL_CV_HINT             1852

#define IDL_SC_SCRIPT           1860
#define IDL_SC_STATUS           1861

#define IDL_SP_GRP              1870
#define IDL_SP_FILE             1871
#define IDL_SP_FORMAT           1872

#define IDL_FX_REVERB_WET       1880
#define IDL_FX_REVERB_DRY       1881
#define IDL_FX_REVERB_WIDTH     1882
#define IDL_FX_REVERB_RT60      1883
#define IDL_FX_REVERB_DAMP      1884
#define IDL_FX_REVERB_BASS      1885
#define IDL_FX_REVERB_PREDELAY  1886
#define IDL_FX_REVERB_ER        1887
#define IDL_FX_REVERB_HINT      1888

// Buttons
#define IDOK                    1
#define IDCANCEL                2
