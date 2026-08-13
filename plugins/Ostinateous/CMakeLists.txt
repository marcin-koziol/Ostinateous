dpf_add_plugin(ostinateous
  TARGETS lv2 vst3 clap jack
  UI_TYPE cairo
  FILES_DSP
      OstinateousPlugin.cpp
  FILES_UI
      GridUI.cpp)

target_include_directories(ostinateous PUBLIC ".")
