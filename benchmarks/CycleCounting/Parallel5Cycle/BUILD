cc_library(
  name = "FiveCycle",
  hdrs = ["FiveCycle.h"],
  deps = [
  "//gbbs:gbbs",
  "//pbbslib:random_shuffle",
  "//pbbslib:sample_sort",
  "//benchmarks/DegeneracyOrder/BarenboimElkin08:DegeneracyOrder",
  "//benchmarks/DegeneracyOrder/GoodrichPszona11:DegeneracyOrder",
  "//benchmarks/KCore/JulienneDBS17:KCore"
  ]
)

cc_binary(
  name = "FiveCycle_main",
  srcs = ["FiveCycle.cc"],
  deps = [":FiveCycle"]
)

package(
  default_visibility = ["//visibility:public"],
)
