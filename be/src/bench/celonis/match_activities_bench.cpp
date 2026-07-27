#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

#include <random>

#include "column/column_helper.h"
#include "column/datum_tuple.h"
#include "exprs/celonis/match_activities.h"
#include "exprs/function_context.h"
#include "runtime/types.h"

namespace starrocks {

/*
2026-07-24T23:26:47+00:00
Running ./be/build_Release/src/bench/celonis/output/match_activities_bench
Run on (32 X 2500 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x16)
  L1 Instruction 32 KiB (x16)
  L2 Unified 1024 KiB (x16)
  L3 Unified 33792 KiB (x1)
Load Average: 0.00, 0.31, 1.44
-----------------------------------------------------------------------------------------------------------------------------------------
Benchmark                                                                               Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------------------------------------------
BM_MatchActivitiesNonConstantConfig/1000/20/20/5/0/0/0/0/0                        1093900 ns      1093795 ns          638 RowInvRate=1093.79ns
BM_MatchActivitiesNonConstantConfig/10000/20/20/5/0/0/0/0/0                      11015700 ns     11015515 ns           63 RowInvRate=1.10155us
BM_MatchActivitiesNonConstantConfig/100000/20/20/5/0/0/0/0/0                    131161960 ns    131160968 ns            5 RowInvRate=1.31161us
BM_MatchActivitiesNonConstantConfig/1000/20/40/5/0/0/0/0/0                        1007062 ns      1006802 ns          698 RowInvRate=1006.8ns
BM_MatchActivitiesNonConstantConfig/10000/20/40/5/0/0/0/0/0                      10160276 ns     10160067 ns           69 RowInvRate=1016.01ns
BM_MatchActivitiesNonConstantConfig/100000/20/40/5/0/0/0/0/0                    123704387 ns    123703282 ns            6 RowInvRate=1.23703us
BM_MatchActivitiesNonConstantConfig/1000/20/60/5/0/0/0/0/0                         973835 ns       973565 ns          719 RowInvRate=973.565ns
BM_MatchActivitiesNonConstantConfig/10000/20/60/5/0/0/0/0/0                       9773882 ns      9771752 ns           72 RowInvRate=977.175ns
BM_MatchActivitiesNonConstantConfig/100000/20/60/5/0/0/0/0/0                    121480735 ns    121479597 ns            6 RowInvRate=1.2148us
BM_MatchActivitiesNonConstantConfig/1000/20/20/10/0/0/0/0/0                       1630885 ns      1630797 ns          430 RowInvRate=1.6308us
BM_MatchActivitiesNonConstantConfig/10000/20/20/10/0/0/0/0/0                     16384729 ns     16384087 ns           43 RowInvRate=1.63841us
BM_MatchActivitiesNonConstantConfig/100000/20/20/10/0/0/0/0/0                   184624711 ns    184616233 ns            4 RowInvRate=1.84616us
BM_MatchActivitiesNonConstantConfig/1000/20/40/10/0/0/0/0/0                       1491535 ns      1491455 ns          470 RowInvRate=1.49145us
BM_MatchActivitiesNonConstantConfig/10000/20/40/10/0/0/0/0/0                     14997433 ns     14996019 ns           47 RowInvRate=1.4996us
BM_MatchActivitiesNonConstantConfig/100000/20/40/10/0/0/0/0/0                   171672830 ns    171671385 ns            4 RowInvRate=1.71671us
BM_MatchActivitiesNonConstantConfig/1000/20/60/10/0/0/0/0/0                       1425233 ns      1425096 ns          492 RowInvRate=1.4251us
BM_MatchActivitiesNonConstantConfig/10000/20/60/10/0/0/0/0/0                     14353207 ns     14351261 ns           49 RowInvRate=1.43513us
BM_MatchActivitiesNonConstantConfig/100000/20/60/10/0/0/0/0/0                   166144888 ns    166141013 ns            4 RowInvRate=1.66141us
BM_MatchActivitiesConstantConfig/1000/20/20/5/0/0/0/0/0                            231297 ns       231197 ns         3046 RowInvRate=231.197ns
BM_MatchActivitiesConstantConfig/10000/20/20/5/0/0/0/0/0                          2406313 ns      2406208 ns          283 RowInvRate=240.621ns
BM_MatchActivitiesConstantConfig/100000/20/20/5/0/0/0/0/0                        45661704 ns     45658331 ns           15 RowInvRate=456.583ns
BM_MatchActivitiesConstantConfig/1000/20/40/5/0/0/0/0/0                            152033 ns       151928 ns         4620 RowInvRate=151.928ns
BM_MatchActivitiesConstantConfig/10000/20/40/5/0/0/0/0/0                          1599107 ns      1598970 ns          439 RowInvRate=159.897ns
BM_MatchActivitiesConstantConfig/100000/20/40/5/0/0/0/0/0                        37808010 ns     37806386 ns           18 RowInvRate=378.064ns
BM_MatchActivitiesConstantConfig/1000/20/60/5/0/0/0/0/0                            124267 ns       124157 ns         5657 RowInvRate=124.157ns
BM_MatchActivitiesConstantConfig/10000/20/60/5/0/0/0/0/0                          1318510 ns      1318365 ns          526 RowInvRate=131.836ns
BM_MatchActivitiesConstantConfig/100000/20/60/5/0/0/0/0/0                        34664284 ns     34663450 ns           20 RowInvRate=346.634ns
BM_MatchActivitiesConstantConfig/1000/20/20/10/0/0/0/0/0                           351121 ns       350805 ns         2001 RowInvRate=350.805ns
BM_MatchActivitiesConstantConfig/10000/20/20/10/0/0/0/0/0                         3633718 ns      3633537 ns          190 RowInvRate=363.354ns
BM_MatchActivitiesConstantConfig/100000/20/20/10/0/0/0/0/0                       55139812 ns     55138014 ns           12 RowInvRate=551.38ns
BM_MatchActivitiesConstantConfig/1000/20/40/10/0/0/0/0/0                           221406 ns       221295 ns         3193 RowInvRate=221.295ns
BM_MatchActivitiesConstantConfig/10000/20/40/10/0/0/0/0/0                         2305872 ns      2305784 ns          302 RowInvRate=230.578ns
BM_MatchActivitiesConstantConfig/100000/20/40/10/0/0/0/0/0                       44366625 ns     44365203 ns           16 RowInvRate=443.652ns
BM_MatchActivitiesConstantConfig/1000/20/60/10/0/0/0/0/0                           172745 ns       172571 ns         4063 RowInvRate=172.571ns
BM_MatchActivitiesConstantConfig/10000/20/60/10/0/0/0/0/0                         1773259 ns      1773182 ns          380 RowInvRate=177.318ns
BM_MatchActivitiesConstantConfig/100000/20/60/10/0/0/0/0/0                       39365672 ns     39361528 ns           18 RowInvRate=393.615ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000/20/5000/3000/0/0/0/0/0           490516 ns       490369 ns         1440 RowInvRate=490.369ns
BM_MatchActivitiesConstantLargeMatchesConfig/10000/20/5000/3000/0/0/0/0/0         2920799 ns      2920659 ns          236 RowInvRate=292.066ns
BM_MatchActivitiesConstantLargeMatchesConfig/100000/20/5000/3000/0/0/0/0/0       47918669 ns     47915770 ns           15 RowInvRate=479.158ns
BM_MatchActivitiesConstantLargeMatchesConfig/1000000/20/5000/3000/0/0/0/0/0     498112513 ns    498105182 ns            2 RowInvRate=498.105ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/1/0/0/0/0                            525898 ns       525736 ns         1315 RowInvRate=525.736ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/1/0/0/0/0                          5324803 ns      5323508 ns          133 RowInvRate=532.351ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/1/0/0/0/0                        73782530 ns     73780023 ns            9 RowInvRate=737.8ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/1/0/0/0/0                            590468 ns       590291 ns         1188 RowInvRate=590.291ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/1/0/0/0/0                          6046353 ns      6045881 ns          115 RowInvRate=604.588ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/1/0/0/0/0                        80568882 ns     80563258 ns            9 RowInvRate=805.633ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/1/0/0/0/0                            610512 ns       610300 ns         1151 RowInvRate=610.3ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/1/0/0/0/0                          6120294 ns      6118597 ns          114 RowInvRate=611.86ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/1/0/0/0/0                        82822659 ns     82818700 ns            8 RowInvRate=828.187ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/5/0/0/0/0                            899077 ns       898742 ns          778 RowInvRate=898.742ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/5/0/0/0/0                          8988811 ns      8986838 ns           80 RowInvRate=898.684ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/5/0/0/0/0                       109976033 ns    109974135 ns            6 RowInvRate=1099.74ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/5/0/0/0/0                            753178 ns       753108 ns          922 RowInvRate=753.108ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/5/0/0/0/0                          7535485 ns      7533305 ns           93 RowInvRate=753.331ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/5/0/0/0/0                        96600224 ns     96598252 ns            7 RowInvRate=965.983ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/5/0/0/0/0                            682515 ns       682441 ns         1026 RowInvRate=682.441ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/5/0/0/0/0                          6898158 ns      6898000 ns          102 RowInvRate=689.8ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/5/0/0/0/0                        89222053 ns     89220311 ns            8 RowInvRate=892.203ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/10/0/0/0/0                           988799 ns       988439 ns          709 RowInvRate=988.439ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/10/0/0/0/0                         9913019 ns      9912766 ns           69 RowInvRate=991.277ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/10/0/0/0/0                      118587480 ns    118584948 ns            6 RowInvRate=1.18585us
BM_MatchActivitiesConstantConfig/1000/20/40/0/10/0/0/0/0                           721850 ns       721750 ns          965 RowInvRate=721.75ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/10/0/0/0/0                         7270424 ns      7267985 ns           96 RowInvRate=726.798ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/10/0/0/0/0                       95606814 ns     95603949 ns            7 RowInvRate=956.039ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/10/0/0/0/0                           609227 ns       608963 ns         1145 RowInvRate=608.963ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/10/0/0/0/0                         6134504 ns      6134474 ns          113 RowInvRate=613.447ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/10/0/0/0/0                       81502017 ns     81498877 ns            8 RowInvRate=814.989ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/5/0/0/0                            232601 ns       232449 ns         3020 RowInvRate=232.449ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/5/0/0/0                          2438349 ns      2438296 ns          288 RowInvRate=243.83ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/5/0/0/0                        45822629 ns     45820968 ns           15 RowInvRate=458.21ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/5/0/0/0                            155160 ns       155008 ns         4615 RowInvRate=155.008ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/5/0/0/0                          1719938 ns      1719782 ns          428 RowInvRate=171.978ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/5/0/0/0                        40978473 ns     40976870 ns           18 RowInvRate=409.769ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/5/0/0/0                            128019 ns       127869 ns         5473 RowInvRate=127.869ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/5/0/0/0                          1567746 ns      1567352 ns          478 RowInvRate=156.735ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/5/0/0/0                        38254400 ns     38251841 ns           18 RowInvRate=382.518ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/10/0/0/0                           358032 ns       357856 ns         1952 RowInvRate=357.856ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/10/0/0/0                         3947604 ns      3947000 ns          180 RowInvRate=394.7ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/10/0/0/0                       60574028 ns     60569543 ns           11 RowInvRate=605.695ns
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/10/0/0/0                           225643 ns       225547 ns         3117 RowInvRate=225.547ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/10/0/0/0                         2661172 ns      2660447 ns          270 RowInvRate=266.045ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/10/0/0/0                       47452374 ns     47451136 ns           15 RowInvRate=474.511ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/10/0/0/0                           175732 ns       175609 ns         3978 RowInvRate=175.609ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/10/0/0/0                         1842040 ns      1841898 ns          381 RowInvRate=184.19ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/10/0/0/0                       40709073 ns     40706842 ns           17 RowInvRate=407.068ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/5/0                           1009928 ns      1009615 ns          697 RowInvRate=1009.61ns
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/5/0                         10154965 ns     10153515 ns           68 RowInvRate=1015.35ns
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/5/0                       124703409 ns    124697241 ns            5 RowInvRate=1.24697us
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/5/0                            875622 ns       875417 ns          798 RowInvRate=875.417ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/5/0                          8786554 ns      8784309 ns           80 RowInvRate=878.431ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/5/0                       106967719 ns    106964283 ns            6 RowInvRate=1069.64ns
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/5/0                            814279 ns       814212 ns          859 RowInvRate=814.212ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/5/0                          8268333 ns      8268099 ns           83 RowInvRate=826.81ns
BM_MatchActivitiesConstantConfig/100000/20/60/0/0/0/0/5/0                       102818293 ns    102814839 ns            7 RowInvRate=1028.15ns
BM_MatchActivitiesConstantConfig/1000/20/20/0/0/0/0/10/0                          1171918 ns      1171809 ns          597 RowInvRate=1.17181us
BM_MatchActivitiesConstantConfig/10000/20/20/0/0/0/0/10/0                        11997491 ns     11997058 ns           59 RowInvRate=1.19971us
BM_MatchActivitiesConstantConfig/100000/20/20/0/0/0/0/10/0                      136769419 ns    136759584 ns            5 RowInvRate=1.3676us
BM_MatchActivitiesConstantConfig/1000/20/40/0/0/0/0/10/0                           989289 ns       989221 ns          710 RowInvRate=989.221ns
BM_MatchActivitiesConstantConfig/10000/20/40/0/0/0/0/10/0                         9932846 ns      9930370 ns           71 RowInvRate=993.037ns
BM_MatchActivitiesConstantConfig/100000/20/40/0/0/0/0/10/0                      120982417 ns    120981169 ns            6 RowInvRate=1.20981us
BM_MatchActivitiesConstantConfig/1000/20/60/0/0/0/0/10/0                           893540 ns       893475 ns          785 RowInvRate=893.475ns
BM_MatchActivitiesConstantConfig/10000/20/60/0/0/0/0/10/0                         9135443 ns      9135205 ns           73 RowInvRate=913.52ns
BM_MatchActivitiesConstantConfigInt/10000/20/40/5/0/0/0/0/0                        472870 ns       472620 ns         1484 RowInvRate=47.262ns
BM_MatchActivitiesConstantConfigInt/100000/20/40/5/0/0/0/0/0                      5167926 ns      5166872 ns          137 RowInvRate=51.6687ns
BM_MatchActivitiesConstantConfigInt/1000/20/60/5/0/0/0/0/0                          41591 ns        41531 ns        16867 RowInvRate=41.5311ns
BM_MatchActivitiesConstantConfigInt/10000/20/60/5/0/0/0/0/0                        387963 ns       387678 ns         1810 RowInvRate=38.7678ns
BM_MatchActivitiesConstantConfigInt/100000/20/60/5/0/0/0/0/0                      4220762 ns      4220287 ns          163 RowInvRate=42.2029ns
BM_MatchActivitiesConstantConfigInt/1000/20/20/10/0/0/0/0/0                        112146 ns       112086 ns         6236 RowInvRate=112.086ns
BM_MatchActivitiesConstantConfigInt/10000/20/20/10/0/0/0/0/0                      1092973 ns      1092754 ns          639 RowInvRate=109.275ns
BM_MatchActivitiesConstantConfigInt/100000/20/20/10/0/0/0/0/0                    11134818 ns     11134399 ns           62 RowInvRate=111.344ns
BM_MatchActivitiesConstantConfigInt/1000/20/40/10/0/0/0/0/0                         73078 ns        73020 ns         9576 RowInvRate=73.0198ns
BM_MatchActivitiesConstantConfigInt/10000/20/40/10/0/0/0/0/0                       694771 ns       694486 ns          995 RowInvRate=69.4486ns
BM_MatchActivitiesConstantConfigInt/100000/20/40/10/0/0/0/0/0                     7216789 ns      7216170 ns           95 RowInvRate=72.1617ns
BM_MatchActivitiesConstantConfigInt/1000/20/60/10/0/0/0/0/0                         57333 ns        57266 ns        12206 RowInvRate=57.2663ns
BM_MatchActivitiesConstantConfigInt/10000/20/60/10/0/0/0/0/0                       540532 ns       540286 ns         1299 RowInvRate=54.0286ns
BM_MatchActivitiesConstantConfigInt/100000/20/60/10/0/0/0/0/0                     5687332 ns      5686986 ns          121 RowInvRate=56.8699ns
BM_MatchActivitiesConstantLargeMatchesConfigInt/1000/20/5000/3000/0/0/0/0/0        790213 ns       790044 ns          886 RowInvRate=790.044ns
BM_MatchActivitiesConstantLargeMatchesConfigInt/10000/20/5000/3000/0/0/0/0/0      3105069 ns      3104760 ns          226 RowInvRate=310.476ns
BM_MatchActivitiesConstantLargeMatchesConfigInt/100000/20/5000/3000/0/0/0/0/0    26387156 ns     26386293 ns           27 RowInvRate=263.863ns
BM_MatchActivitiesConstantLargeMatchesConfigInt/1000000/20/5000/3000/0/0/0/0/0  266257546 ns    266248251 ns            3 RowInvRate=266.248ns
BM_MatchActivitiesConstantConfigInt/1000/20/20/0/1/0/0/0/0                         188677 ns       188612 ns         3712 RowInvRate=188.612ns
BM_MatchActivitiesConstantConfigInt/10000/20/20/0/1/0/0/0/0                       1863254 ns      1863051 ns          375 RowInvRate=186.305ns
BM_MatchActivitiesConstantConfigInt/100000/20/20/0/1/0/0/0/0                     18905467 ns     18904696 ns           37 RowInvRate=189.047ns
BM_MatchActivitiesConstantConfigInt/1000/20/40/0/1/0/0/0/0                         217909 ns       217827 ns         3220 RowInvRate=217.827ns
BM_MatchActivitiesConstantConfigInt/10000/20/40/0/1/0/0/0/0                       2155085 ns      2154809 ns          325 RowInvRate=215.481ns
BM_MatchActivitiesConstantConfigInt/100000/20/40/0/1/0/0/0/0                     22056484 ns     22055690 ns           32 RowInvRate=220.557ns
BM_MatchActivitiesConstantConfigInt/1000/20/60/0/1/0/0/0/0                         228667 ns       228536 ns         3065 RowInvRate=228.536ns
BM_MatchActivitiesConstantConfigInt/10000/20/60/0/1/0/0/0/0                       2263955 ns      2263517 ns          309 RowInvRate=226.352ns
BM_MatchActivitiesConstantConfigInt/100000/20/60/0/1/0/0/0/0                     23070469 ns     23069214 ns           30 RowInvRate=230.692ns
BM_MatchActivitiesConstantConfigInt/1000/20/20/0/5/0/0/0/0                         342019 ns       341923 ns         2053 RowInvRate=341.923ns
BM_MatchActivitiesConstantConfigInt/10000/20/20/0/0/5/0/0/0                        707271 ns       707060 ns          984 RowInvRate=70.706ns
BM_MatchActivitiesConstantConfigInt/100000/20/20/0/0/5/0/0/0                      7423940 ns      7423482 ns          101 RowInvRate=74.2348ns
BM_MatchActivitiesConstantConfigInt/1000/20/40/0/0/5/0/0/0                          50252 ns        50191 ns        13839 RowInvRate=50.1907ns
BM_MatchActivitiesConstantConfigInt/10000/20/40/0/0/5/0/0/0                        475424 ns       475213 ns         1479 RowInvRate=47.5213ns
BM_MatchActivitiesConstantConfigInt/100000/20/40/0/0/5/0/0/0                      5013750 ns      5012834 ns          137 RowInvRate=50.1283ns
BM_MatchActivitiesConstantConfigInt/1000/20/60/0/0/5/0/0/0                          42017 ns        41967 ns        16699 RowInvRate=41.9674ns
BM_MatchActivitiesConstantConfigInt/10000/20/60/0/0/5/0/0/0                        390596 ns       390357 ns         1788 RowInvRate=39.0357ns
BM_MatchActivitiesConstantConfigInt/100000/20/60/0/0/5/0/0/0                      4451310 ns      4450484 ns          166 RowInvRate=44.5048ns
BM_MatchActivitiesConstantConfigInt/1000/20/20/0/0/10/0/0/0                        113619 ns       113541 ns         6160 RowInvRate=113.541ns
BM_MatchActivitiesConstantConfigInt/10000/20/20/0/0/10/0/0/0                      1107236 ns      1106990 ns          628 RowInvRate=110.699ns
BM_MatchActivitiesConstantConfigInt/100000/20/20/0/0/10/0/0/0                    11827825 ns     11826685 ns           60 RowInvRate=118.267ns
BM_MatchActivitiesConstantConfigInt/1000/20/40/0/0/10/0/0/0                         74564 ns        74502 ns         9380 RowInvRate=74.5023ns
BM_MatchActivitiesConstantConfigInt/10000/20/40/0/0/10/0/0/0                       711982 ns       711748 ns          975 RowInvRate=71.1748ns
BM_MatchActivitiesConstantConfigInt/100000/20/40/0/0/10/0/0/0                     7634737 ns      7633997 ns           95 RowInvRate=76.34ns
BM_MatchActivitiesConstantConfigInt/1000/20/60/0/0/10/0/0/0                         59247 ns        59179 ns        11783 RowInvRate=59.1795ns
BM_MatchActivitiesConstantConfigInt/10000/20/60/0/0/10/0/0/0                       560077 ns       559825 ns         1255 RowInvRate=55.9825ns
BM_MatchActivitiesConstantConfigInt/100000/20/60/0/0/10/0/0/0                     6080010 ns      6079108 ns          115 RowInvRate=60.7911ns
BM_MatchActivitiesConstantConfigInt/1000/20/20/0/0/0/0/5/0                         363734 ns       363649 ns         1919 RowInvRate=363.649ns
BM_MatchActivitiesConstantConfigInt/10000/20/20/0/0/0/0/5/0                       3590205 ns      3589930 ns          195 RowInvRate=358.993ns
BM_MatchActivitiesConstantConfigInt/100000/20/20/0/0/0/0/5/0                     35769928 ns     35766358 ns           19 RowInvRate=357.664ns
BM_MatchActivitiesConstantConfigInt/1000/20/40/0/0/0/0/5/0                         312314 ns       312237 ns         2242 RowInvRate=312.237ns
BM_MatchActivitiesConstantConfigInt/10000/20/40/0/0/0/0/5/0                       3089263 ns      3089046 ns          227 RowInvRate=308.905ns
BM_MatchActivitiesConstantConfigInt/100000/20/40/0/0/0/0/5/0                     31308396 ns     31307418 ns           22 RowInvRate=313.074ns
BM_MatchActivitiesConstantConfigInt/1000/20/60/0/0/0/0/5/0                         295131 ns       295028 ns         2371 RowInvRate=295.028ns
BM_MatchActivitiesConstantConfigInt/10000/20/60/0/0/0/0/5/0                       2914953 ns      2914560 ns          240 RowInvRate=291.456ns
BM_MatchActivitiesConstantConfigInt/100000/20/60/0/0/0/0/5/0                     29455752 ns     29455426 ns           24 RowInvRate=294.554ns
BM_MatchActivitiesConstantConfigInt/1000/20/20/0/0/0/0/10/0                        447187 ns       447142 ns         1566 RowInvRate=447.142ns
BM_MatchActivitiesConstantConfigInt/10000/20/20/0/0/0/0/10/0                      4442631 ns      4442472 ns          158 RowInvRate=444.247ns
BM_MatchActivitiesConstantConfigInt/100000/20/20/0/0/0/0/10/0                    46147216 ns     46144876 ns           16 RowInvRate=461.449ns
BM_MatchActivitiesConstantConfigInt/1000/20/40/0/0/0/0/10/0                        358586 ns       358509 ns         1948 RowInvRate=358.509ns
BM_MatchActivitiesConstantConfigInt/10000/20/40/0/0/0/0/10/0                      3540650 ns      3540417 ns          197 RowInvRate=354.042ns
BM_MatchActivitiesConstantConfigInt/100000/20/40/0/0/0/0/10/0                    35375106 ns     35374198 ns           19 RowInvRate=353.742ns
BM_MatchActivitiesConstantConfigInt/1000/20/60/0/0/0/0/10/0                        326768 ns       326718 ns         2142 RowInvRate=326.718ns
BM_MatchActivitiesConstantConfigInt/10000/20/60/0/0/0/0/10/0                      3226419 ns      3226234 ns          217 RowInvRate=322.623ns
BM_MatchActivitiesConstantConfigInt/100000/20/60/0/0/0/0/10/0                    32567415 ns     32566042 ns           21 RowInvRate=325.66ns
BM_MatchActivitiesConstantConfigInt/1000/20/20/0/0/0/0/0/5                          77370 ns        77317 ns         9042 RowInvRate=77.3174ns
BM_MatchActivitiesConstantConfigInt/10000/20/20/0/0/0/0/0/5                        743123 ns       742898 ns          940 RowInvRate=74.2898ns
BM_MatchActivitiesConstantConfigInt/100000/20/20/0/0/0/0/0/5                      7812228 ns      7811758 ns           88 RowInvRate=78.1176ns
BM_MatchActivitiesConstantConfigInt/1000/20/40/0/0/0/0/0/5                         107962 ns       107902 ns         6464 RowInvRate=107.902ns
BM_MatchActivitiesConstantConfigInt/10000/20/40/0/0/0/0/0/5                       1047620 ns      1047396 ns          662 RowInvRate=104.74ns
BM_MatchActivitiesConstantConfigInt/100000/20/40/0/0/0/0/0/5                     10750170 ns     10749431 ns           64 RowInvRate=107.494ns
BM_MatchActivitiesConstantConfigInt/1000/20/60/0/0/0/0/0/5                         129737 ns       129690 ns         5389 RowInvRate=129.69ns
BM_MatchActivitiesConstantConfigInt/10000/20/60/0/0/0/0/0/5                       1270255 ns      1270016 ns          552 RowInvRate=127.002ns
BM_MatchActivitiesConstantConfigInt/100000/20/60/0/0/0/0/0/5                     12975721 ns     12975341 ns           54 RowInvRate=129.753ns
BM_MatchActivitiesConstantConfigInt/1000/20/20/0/0/0/0/0/10                         60369 ns        60315 ns        11478 RowInvRate=60.3147ns
BM_MatchActivitiesConstantConfigInt/10000/20/20/0/0/0/0/0/10                       574136 ns       573752 ns         1230 RowInvRate=57.3752ns
BM_MatchActivitiesConstantConfigInt/100000/20/20/0/0/0/0/0/10                     6192005 ns      6190882 ns          109 RowInvRate=61.9088ns
BM_MatchActivitiesConstantConfigInt/1000/20/40/0/0/0/0/0/10                         77734 ns        77655 ns         8932 RowInvRate=77.6554ns
BM_MatchActivitiesConstantConfigInt/10000/20/40/0/0/0/0/0/10                       743254 ns       743037 ns          947 RowInvRate=74.3037ns
BM_MatchActivitiesConstantConfigInt/100000/20/40/0/0/0/0/0/10                     7871195 ns      7870289 ns           89 RowInvRate=78.7029ns
BM_MatchActivitiesConstantConfigInt/1000/20/60/0/0/0/0/0/10                         94023 ns        93966 ns         7412 RowInvRate=93.9662ns
BM_MatchActivitiesConstantConfigInt/10000/20/60/0/0/0/0/0/10                       907088 ns       906838 ns          771 RowInvRate=90.6838ns
BM_MatchActivitiesConstantConfigInt/100000/20/60/0/0/0/0/0/10                     9448734 ns      9448063 ns           75 RowInvRate=94.4806ns
*/

enum MatchType {
    CONSTANT,
    NON_CONSTANT,
};

template <LogicalType element_type>
static void do_bench(benchmark::State& state, MatchType match_type) {
    int num_rows = state.range(0);
    int variant_length = state.range(1);
    int num_values = state.range(2);
    // STARTING, NODE, ENDING, EXCLUDING, EXCLUDING_ALL, NODES_ANY
    int starting_nodes_length = state.range(3);
    int nodes_length = state.range(4);
    int ending_nodes_length = state.range(5);
    int excluding_nodes_length = state.range(6);
    int excluding_all_nodes_length = state.range(7);
    int nodes_any_length = state.range(8);

    using UniformInt = std::uniform_int_distribution<std::mt19937::result_type>;
    std::random_device dev;
    std::mt19937 rng(dev());
    UniformInt uniform_value(0, num_values - 1);

    // Array element TypeDescriptor for the columns we build below.
    const TypeDescriptor element_type_desc = [] {
        if constexpr (element_type == TYPE_VARCHAR) {
            return TypeDescriptor::create_varchar_type(20);
        } else {
            return TypeDescriptor::from_logical_type(element_type);
        }
    }();

    // For VARCHAR we need a backing pool of strings that the generated Slices
    // reference; for INT the value is generated inline.
    [[maybe_unused]] std::vector<std::string> string_values;
    if constexpr (element_type == TYPE_VARCHAR) {
        string_values.reserve(num_values);
        for (int i = 0; i < num_values; i++) {
            string_values.push_back("value" + std::to_string(i));
        }
    }

    auto gen_rand_element = [&]() {
        if constexpr (element_type == TYPE_VARCHAR) {
            return Slice(string_values[uniform_value(rng)]);
        } else {
            return static_cast<int32_t>(uniform_value(rng));
        }
    };

    auto gen_rand_array = [&](int num_elements) {
        DatumArray array;
        for (int i = 0; i < num_elements; i++) {
            array.emplace_back(gen_rand_element());
        }
        return array;
    };

    std::vector<FunctionContext::TypeDesc> arg_types = {
            TypeDescriptor::from_logical_type(TYPE_ARRAY), TypeDescriptor::from_logical_type(TYPE_ARRAY),
            TypeDescriptor::from_logical_type(TYPE_ARRAY), TypeDescriptor::from_logical_type(TYPE_ARRAY),
            TypeDescriptor::from_logical_type(TYPE_ARRAY), TypeDescriptor::from_logical_type(TYPE_ARRAY),
            TypeDescriptor::from_logical_type(TYPE_ARRAY)};
    auto return_type = TypeDescriptor::from_logical_type(TYPE_BIGINT);
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), return_type));

    int total_rows = 0;
    for (auto _ : state) {
        state.PauseTiming();
        total_rows += num_rows;
        auto variant_column = ColumnHelper::create_column(TypeDescriptor::create_array_type(element_type_desc), false);
        for (int i = 0; i < num_rows; i++) {
            variant_column->append_datum(gen_rand_array(variant_length));
        }
        auto starting_nodes_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(element_type_desc), false);
        auto nodes_column = ColumnHelper::create_column(TypeDescriptor::create_array_type(element_type_desc), false);
        auto ending_nodes_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(element_type_desc), false);
        auto excluding_nodes_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(element_type_desc), false);
        auto excluding_all_nodes_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(element_type_desc), false);
        auto any_nodes_column =
                ColumnHelper::create_column(TypeDescriptor::create_array_type(element_type_desc), false);
        switch (match_type) {
        case CONSTANT:
            starting_nodes_column->append_datum(gen_rand_array(starting_nodes_length));
            nodes_column->append_datum(gen_rand_array(nodes_length));
            ending_nodes_column->append_datum(gen_rand_array(ending_nodes_length));
            excluding_nodes_column->append_datum(gen_rand_array(excluding_nodes_length));
            excluding_all_nodes_column->append_datum(gen_rand_array(excluding_all_nodes_length));
            any_nodes_column->append_datum(gen_rand_array(nodes_any_length));
            ctx->set_constant_columns({nullptr, starting_nodes_column, nodes_column, ending_nodes_column,
                                       excluding_nodes_column, excluding_all_nodes_column, any_nodes_column});
            break;
        case NON_CONSTANT:
            for (int i = 0; i < num_rows; i++) {
                starting_nodes_column->append_datum(gen_rand_array(starting_nodes_length));
                nodes_column->append_datum(gen_rand_array(nodes_length));
                ending_nodes_column->append_datum(gen_rand_array(ending_nodes_length));
                excluding_nodes_column->append_datum(gen_rand_array(excluding_nodes_length));
                excluding_all_nodes_column->append_datum(gen_rand_array(excluding_all_nodes_length));
                any_nodes_column->append_datum(gen_rand_array(nodes_any_length));
            }
            ctx->set_constant_columns({nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr});
            break;
        }

        state.ResumeTiming();
        ASSERT_TRUE(CelonisMatchActivitiesFunctions<element_type>::prepare(ctx.get(), FunctionContext::FRAGMENT_LOCAL)
                            .ok());
        ASSERT_TRUE(
                CelonisMatchActivitiesFunctions<element_type>::prepare(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisMatchActivitiesFunctions<element_type>::celonis_match_activities(
                            ctx.get(), {variant_column, starting_nodes_column, nodes_column, ending_nodes_column,
                                        excluding_nodes_column, excluding_all_nodes_column, any_nodes_column})
                            .ok());
        ASSERT_TRUE(
                CelonisMatchActivitiesFunctions<element_type>::close(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(
                CelonisMatchActivitiesFunctions<element_type>::close(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
    }
    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_MatchActivitiesNonConstantConfig(benchmark::State& state) {
    do_bench<TYPE_VARCHAR>(state, NON_CONSTANT);
}

static void BM_MatchActivitiesConstantConfig(benchmark::State& state) {
    do_bench<TYPE_VARCHAR>(state, CONSTANT);
}

static void BM_MatchActivitiesConstantLargeMatchesConfig(benchmark::State& state) {
    do_bench<TYPE_VARCHAR>(state, CONSTANT);
}

static void BM_MatchActivitiesConstantConfigInt(benchmark::State& state) {
    do_bench<TYPE_INT>(state, CONSTANT);
}

static void BM_MatchActivitiesConstantLargeMatchesConfigInt(benchmark::State& state) {
    do_bench<TYPE_INT>(state, CONSTANT);
}

static void BM_MatchActivitiesNonConstantConfigInt(benchmark::State& state) {
    do_bench<TYPE_INT>(state, NON_CONSTANT);
}

// Args: Number of rows/ Length of each variant / Number of possible values / Starting nodes length / Nodes length / Ending nodes length / Excluding nodes length / Excluding all nodes length / Any nodes length
// STARTING nodes
BENCHMARK(BM_MatchActivitiesNonConstantConfig)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {5, 10}, {0}, {0}, {0}, {0}, {0}});
BENCHMARK(BM_MatchActivitiesConstantConfig)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {5, 10}, {0}, {0}, {0}, {0}, {0}});
BENCHMARK(BM_MatchActivitiesConstantLargeMatchesConfig)
        ->ArgsProduct({{1000, 10000, 100000, 1000000}, {20}, {5000}, {3000}, {0}, {0}, {0}, {0}, {0}});
// NODES
BENCHMARK(BM_MatchActivitiesConstantConfig)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {1, 5, 10}, {0}, {0}, {0}, {0}});
// ENDING nodes
BENCHMARK(BM_MatchActivitiesConstantConfig)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {5, 10}, {0}, {0}, {0}});
// EXCLUDING_ALL nodes
BENCHMARK(BM_MatchActivitiesConstantConfig)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {0}, {0}, {5, 10}, {0}});
// ANY_NODES
BENCHMARK(BM_MatchActivitiesConstantConfig)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {0}, {0}, {0}, {5, 10}});

// ---------------------------------------------------------------------------
// INT-typed variants of the benchmarks (element type passed as the template
// argument to CelonisMatchActivitiesFunctions).
// ---------------------------------------------------------------------------
// STARTING nodes
BENCHMARK(BM_MatchActivitiesNonConstantConfigInt)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {5, 10}, {0}, {0}, {0}, {0}, {0}});
BENCHMARK(BM_MatchActivitiesConstantConfigInt)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {5, 10}, {0}, {0}, {0}, {0}, {0}});
BENCHMARK(BM_MatchActivitiesConstantLargeMatchesConfigInt)
        ->ArgsProduct({{1000, 10000, 100000, 1000000}, {20}, {5000}, {3000}, {0}, {0}, {0}, {0}, {0}});
// NODES
BENCHMARK(BM_MatchActivitiesConstantConfigInt)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {1, 5, 10}, {0}, {0}, {0}, {0}});
// ENDING nodes
BENCHMARK(BM_MatchActivitiesConstantConfigInt)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {5, 10}, {0}, {0}, {0}});
// EXCLUDING_ALL nodes
BENCHMARK(BM_MatchActivitiesConstantConfigInt)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {0}, {0}, {5, 10}, {0}});
// ANY_NODES
BENCHMARK(BM_MatchActivitiesConstantConfigInt)
        ->ArgsProduct({{1000, 10000, 100000}, {20}, {20, 40, 60}, {0}, {0}, {0}, {0}, {0}, {5, 10}});

} // namespace starrocks

BENCHMARK_MAIN();
