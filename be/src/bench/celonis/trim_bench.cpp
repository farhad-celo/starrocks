#include <benchmark/benchmark.h>
#include <gtest/gtest.h>
#include <random>

#include "column/column_helper.h"
#include "exprs/anyval_util.h"
#include "exprs/celonis/trim.h"
#include "exprs/function_context.h"

namespace starrocks {

/*
2025-03-06T10:47:58+01:00
Running ./be/build_Release/src/bench/celonis/output/trim_bench
Run on (22 X 4458.59 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x11)
  L1 Instruction 64 KiB (x11)
  L2 Unified 2048 KiB (x11)
  L3 Unified 24576 KiB (x1)
Load Average: 3.00, 3.03, 2.47
// Args: Number of rows / Avg length of words / Length of chars to trim / % of input rows that start with trim_chars
-----------------------------------------------------------------------------------------------
Benchmark                                     Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------
BM_TrimConstant/1000/25/1/25              31262 ns        31254 ns        21304 RowInvRate=31.2541ns
BM_TrimConstant/10000/25/1/25            331184 ns       331134 ns         2093 RowInvRate=33.1134ns
BM_TrimConstant/100000/25/1/25          3220851 ns      3219799 ns          205 RowInvRate=32.198ns
BM_TrimConstant/1000/50/1/25              32703 ns        32695 ns        20805 RowInvRate=32.6946ns
BM_TrimConstant/10000/50/1/25            343146 ns       343077 ns         2059 RowInvRate=34.3077ns
BM_TrimConstant/100000/50/1/25          4352788 ns      4352446 ns          161 RowInvRate=43.5245ns
BM_TrimConstant/1000/100/1/25             35309 ns        35296 ns        19835 RowInvRate=35.2956ns
BM_TrimConstant/10000/100/1/25           363125 ns       363060 ns         1922 RowInvRate=36.306ns
BM_TrimConstant/100000/100/1/25         8931978 ns      8927445 ns          112 RowInvRate=89.2744ns
BM_TrimConstant/1000/25/5/25              35559 ns        35551 ns        19907 RowInvRate=35.5508ns
BM_TrimConstant/10000/25/5/25            348190 ns       348143 ns         1947 RowInvRate=34.8143ns
BM_TrimConstant/100000/25/5/25          3672737 ns      3671889 ns          196 RowInvRate=36.7189ns
BM_TrimConstant/1000/50/5/25              35369 ns        35365 ns        18752 RowInvRate=35.3648ns
BM_TrimConstant/10000/50/5/25            343446 ns       343401 ns         2028 RowInvRate=34.3401ns
BM_TrimConstant/100000/50/5/25          3592226 ns      3591627 ns          196 RowInvRate=35.9163ns
BM_TrimConstant/1000/100/5/25             39849 ns        39573 ns        17093 RowInvRate=39.5725ns
BM_TrimConstant/10000/100/5/25           394305 ns       394235 ns         1870 RowInvRate=39.4235ns
BM_TrimConstant/100000/100/5/25         8896070 ns      8891031 ns          111 RowInvRate=88.9103ns
BM_TrimConstant/1000/25/10/25             33389 ns        33385 ns        20165 RowInvRate=33.3848ns
BM_TrimConstant/10000/25/10/25           335027 ns       335017 ns         2084 RowInvRate=33.5017ns
BM_TrimConstant/100000/25/10/25         3377753 ns      3377537 ns          205 RowInvRate=33.7754ns
BM_TrimConstant/1000/50/10/25             35041 ns        35036 ns        20407 RowInvRate=35.0364ns
BM_TrimConstant/10000/50/10/25           377226 ns       377183 ns         1861 RowInvRate=37.7183ns
BM_TrimConstant/100000/50/10/25         4572903 ns      4572251 ns          183 RowInvRate=45.7225ns
BM_TrimConstant/1000/100/10/25            37475 ns        37470 ns        19113 RowInvRate=37.4705ns
BM_TrimConstant/10000/100/10/25          384542 ns       384461 ns         1661 RowInvRate=38.4461ns
BM_TrimConstant/100000/100/10/25        8787370 ns      8785332 ns          109 RowInvRate=87.8533ns
BM_TrimConstant/1000/25/25/25             36148 ns        36139 ns        20818 RowInvRate=36.1392ns
BM_TrimConstant/10000/25/25/25           365488 ns       365415 ns         1946 RowInvRate=36.5415ns
BM_TrimConstant/100000/25/25/25         3417579 ns      3416631 ns          191 RowInvRate=34.1663ns
BM_TrimConstant/1000/50/25/25             33748 ns        33746 ns        20179 RowInvRate=33.7462ns
BM_TrimConstant/10000/50/25/25           344659 ns       344640 ns         1873 RowInvRate=34.464ns
BM_TrimConstant/100000/50/25/25         3676144 ns      3675892 ns          192 RowInvRate=36.7589ns
BM_TrimConstant/1000/100/25/25            39791 ns        39784 ns        18508 RowInvRate=39.7835ns
BM_TrimConstant/10000/100/25/25          380336 ns       380167 ns         1772 RowInvRate=38.0167ns
BM_TrimConstant/100000/100/25/25        6340533 ns      6339199 ns           84 RowInvRate=63.392ns
BM_TrimConstant/1000/25/1/50              31908 ns        31905 ns        21199 RowInvRate=31.905ns
BM_TrimConstant/10000/25/1/50            334166 ns       334125 ns         2236 RowInvRate=33.4125ns
BM_TrimConstant/100000/25/1/50          3389526 ns      3389048 ns          203 RowInvRate=33.8905ns
BM_TrimConstant/1000/50/1/50              34085 ns        34083 ns        21818 RowInvRate=34.0831ns
BM_TrimConstant/10000/50/1/50            328340 ns       328246 ns         2133 RowInvRate=32.8246ns
BM_TrimConstant/100000/50/1/50          3378396 ns      3378317 ns          158 RowInvRate=33.7832ns
BM_TrimConstant/1000/100/1/50             36583 ns        36439 ns        18502 RowInvRate=36.4391ns
BM_TrimConstant/10000/100/1/50           363625 ns       363526 ns         1916 RowInvRate=36.3526ns
BM_TrimConstant/100000/100/1/50         6083301 ns      6081282 ns          111 RowInvRate=60.8128ns
BM_TrimConstant/1000/25/5/50              35249 ns        35245 ns        21186 RowInvRate=35.2451ns
BM_TrimConstant/10000/25/5/50            345600 ns       345529 ns         1935 RowInvRate=34.5529ns
BM_TrimConstant/100000/25/5/50          3372559 ns      3371556 ns          208 RowInvRate=33.7156ns
BM_TrimConstant/1000/50/5/50              36460 ns        36443 ns        19517 RowInvRate=36.4434ns
BM_TrimConstant/10000/50/5/50            366446 ns       366296 ns         2005 RowInvRate=36.6296ns
BM_TrimConstant/100000/50/5/50          3550299 ns      3549305 ns          198 RowInvRate=35.4931ns
BM_TrimConstant/1000/100/5/50             39533 ns        39521 ns        17294 RowInvRate=39.5207ns
BM_TrimConstant/10000/100/5/50           369136 ns       369061 ns         1804 RowInvRate=36.9061ns
BM_TrimConstant/100000/100/5/50         9232686 ns      9230642 ns          104 RowInvRate=92.3064ns
BM_TrimConstant/1000/25/10/50             35545 ns        35542 ns        19739 RowInvRate=35.5419ns
BM_TrimConstant/10000/25/10/50           343649 ns       343599 ns         1945 RowInvRate=34.3599ns
BM_TrimConstant/100000/25/10/50         3511268 ns      3510843 ns          205 RowInvRate=35.1084ns
BM_TrimConstant/1000/50/10/50             35875 ns        35864 ns        19298 RowInvRate=35.864ns
BM_TrimConstant/10000/50/10/50           365162 ns       365125 ns         1963 RowInvRate=36.5125ns
BM_TrimConstant/100000/50/10/50         4511083 ns      4510254 ns          198 RowInvRate=45.1025ns
BM_TrimConstant/1000/100/10/50            40560 ns        40551 ns        18024 RowInvRate=40.5514ns
BM_TrimConstant/10000/100/10/50          379639 ns       379614 ns         1868 RowInvRate=37.9614ns
BM_TrimConstant/100000/100/10/50        6353720 ns      6351982 ns           79 RowInvRate=63.5198ns
BM_TrimConstant/1000/25/25/50             34944 ns        34934 ns        20792 RowInvRate=34.9337ns
BM_TrimConstant/10000/25/25/50           334235 ns       334201 ns         2027 RowInvRate=33.4201ns
BM_TrimConstant/100000/25/25/50         3625483 ns      3625031 ns          192 RowInvRate=36.2503ns
BM_TrimConstant/1000/50/25/50             34886 ns        34883 ns        19737 RowInvRate=34.8834ns
BM_TrimConstant/10000/50/25/50           353138 ns       353067 ns         1977 RowInvRate=35.3067ns
BM_TrimConstant/100000/50/25/50         3561539 ns      3560099 ns          149 RowInvRate=35.601ns
BM_TrimConstant/1000/100/25/50            40671 ns        40662 ns        19201 RowInvRate=40.6621ns
BM_TrimConstant/10000/100/25/50          395952 ns       395852 ns         1708 RowInvRate=39.5852ns
BM_TrimConstant/100000/100/25/50        6522052 ns      6518549 ns          103 RowInvRate=65.1855ns
BM_TrimConstant/1000/25/1/75              33297 ns        33290 ns        20775 RowInvRate=33.2903ns
BM_TrimConstant/10000/25/1/75            320862 ns       320724 ns         2005 RowInvRate=32.0724ns
BM_TrimConstant/100000/25/1/75          3438007 ns      3437161 ns          218 RowInvRate=34.3716ns
BM_TrimConstant/1000/50/1/75              33415 ns        33409 ns        19763 RowInvRate=33.4087ns
BM_TrimConstant/10000/50/1/75            327542 ns       327505 ns         1994 RowInvRate=32.7505ns
BM_TrimConstant/100000/50/1/75          4340593 ns      4339969 ns          151 RowInvRate=43.3997ns
BM_TrimConstant/1000/100/1/75             35260 ns        35253 ns        18995 RowInvRate=35.2533ns
BM_TrimConstant/10000/100/1/75           365144 ns       365051 ns         1889 RowInvRate=36.5051ns
BM_TrimConstant/100000/100/1/75         8235438 ns      8234407 ns           78 RowInvRate=82.3441ns
BM_TrimConstant/1000/25/5/75              34295 ns        34286 ns        21134 RowInvRate=34.286ns
BM_TrimConstant/10000/25/5/75            343050 ns       343010 ns         1912 RowInvRate=34.301ns
BM_TrimConstant/100000/25/5/75          3407745 ns      3407258 ns          197 RowInvRate=34.0726ns
BM_TrimConstant/1000/50/5/75              34576 ns        34567 ns        19041 RowInvRate=34.5674ns
BM_TrimConstant/10000/50/5/75            376855 ns       376812 ns         1955 RowInvRate=37.6812ns
BM_TrimConstant/100000/50/5/75          3591259 ns      3590621 ns          140 RowInvRate=35.9062ns
BM_TrimConstant/1000/100/5/75             37670 ns        37664 ns        18382 RowInvRate=37.6641ns
BM_TrimConstant/10000/100/5/75           379110 ns       379049 ns         1882 RowInvRate=37.9049ns
BM_TrimConstant/100000/100/5/75         7903162 ns      7900827 ns          111 RowInvRate=79.0083ns
BM_TrimConstant/1000/25/10/75             32985 ns        32981 ns        21363 RowInvRate=32.9813ns
BM_TrimConstant/10000/25/10/75           349436 ns       349314 ns         1971 RowInvRate=34.9314ns
BM_TrimConstant/100000/25/10/75         3492108 ns      3491581 ns          204 RowInvRate=34.9158ns
BM_TrimConstant/1000/50/10/75             34410 ns        34404 ns        20181 RowInvRate=34.4041ns
BM_TrimConstant/10000/50/10/75           367335 ns       365918 ns         2016 RowInvRate=36.5918ns
BM_TrimConstant/100000/50/10/75         3692370 ns      3690797 ns          185 RowInvRate=36.908ns
BM_TrimConstant/1000/100/10/75            37022 ns        37014 ns        17390 RowInvRate=37.0136ns
BM_TrimConstant/10000/100/10/75          392714 ns       392605 ns         1828 RowInvRate=39.2605ns
BM_TrimConstant/100000/100/10/75        6434146 ns      6430598 ns           82 RowInvRate=64.306ns
BM_TrimConstant/1000/25/25/75             33428 ns        33420 ns        19521 RowInvRate=33.4196ns
BM_TrimConstant/10000/25/25/75           336964 ns       336915 ns         2093 RowInvRate=33.6915ns
BM_TrimConstant/100000/25/25/75         3574580 ns      3574097 ns          204 RowInvRate=35.741ns
BM_TrimConstant/1000/50/25/75             34416 ns        34406 ns        18347 RowInvRate=34.4064ns
BM_TrimConstant/10000/50/25/75           349095 ns       348973 ns         2006 RowInvRate=34.8973ns
BM_TrimConstant/100000/50/25/75         4652333 ns      4650790 ns          186 RowInvRate=46.5079ns
BM_TrimConstant/1000/100/25/75            38753 ns        38751 ns        18547 RowInvRate=38.7508ns
BM_TrimConstant/10000/100/25/75          398015 ns       396896 ns         1863 RowInvRate=39.6896ns
BM_TrimConstant/100000/100/25/75        8454186 ns      8451570 ns          108 RowInvRate=84.5157ns
BM_TrimConstant/1000/25/1/100             31788 ns        31784 ns        22355 RowInvRate=31.784ns
BM_TrimConstant/10000/25/1/100           315277 ns       315256 ns         2171 RowInvRate=31.5256ns
BM_TrimConstant/100000/25/1/100         3292454 ns      3291312 ns          213 RowInvRate=32.9131ns
BM_TrimConstant/1000/50/1/100             32790 ns        32788 ns        20313 RowInvRate=32.7876ns
BM_TrimConstant/10000/50/1/100           335060 ns       334905 ns         2053 RowInvRate=33.4905ns
BM_TrimConstant/100000/50/1/100         3521946 ns      3521137 ns          197 RowInvRate=35.2114ns
BM_TrimConstant/1000/100/1/100            37892 ns        37874 ns        18014 RowInvRate=37.8739ns
BM_TrimConstant/10000/100/1/100          369927 ns       369798 ns         1792 RowInvRate=36.9798ns
BM_TrimConstant/100000/100/1/100        5984628 ns      5984059 ns          113 RowInvRate=59.8406ns
BM_TrimConstant/1000/25/5/100             33106 ns        33105 ns        20606 RowInvRate=33.1048ns
BM_TrimConstant/10000/25/5/100           342399 ns       342325 ns         1960 RowInvRate=34.2325ns
BM_TrimConstant/100000/25/5/100         3469793 ns      3469109 ns          203 RowInvRate=34.6911ns
BM_TrimConstant/1000/50/5/100             35261 ns        35255 ns        19747 RowInvRate=35.2551ns
BM_TrimConstant/10000/50/5/100           375518 ns       375430 ns         1857 RowInvRate=37.543ns
BM_TrimConstant/100000/50/5/100         3598500 ns      3596941 ns          197 RowInvRate=35.9694ns
BM_TrimConstant/1000/100/5/100            39868 ns        39857 ns        18575 RowInvRate=39.8571ns
BM_TrimConstant/10000/100/5/100          403781 ns       403746 ns         1836 RowInvRate=40.3746ns
BM_TrimConstant/100000/100/5/100        6500867 ns      6499473 ns           88 RowInvRate=64.9947ns
BM_TrimConstant/1000/25/10/100            33404 ns        33395 ns        20192 RowInvRate=33.395ns
BM_TrimConstant/10000/25/10/100          361862 ns       361839 ns         2090 RowInvRate=36.1839ns
BM_TrimConstant/100000/25/10/100        3621149 ns      3620234 ns          189 RowInvRate=36.2023ns
BM_TrimConstant/1000/50/10/100            34862 ns        34856 ns        19046 RowInvRate=34.8563ns
BM_TrimConstant/10000/50/10/100          380557 ns       380419 ns         1904 RowInvRate=38.0419ns
BM_TrimConstant/100000/50/10/100        3607006 ns      3606486 ns          151 RowInvRate=36.0649ns
BM_TrimConstant/1000/100/10/100           37416 ns        37408 ns        17366 RowInvRate=37.4079ns
BM_TrimConstant/10000/100/10/100         407438 ns       407347 ns         1658 RowInvRate=40.7347ns
BM_TrimConstant/100000/100/10/100       6422153 ns      6421261 ns          106 RowInvRate=64.2126ns
BM_TrimConstant/1000/25/25/100            34762 ns        34756 ns        19102 RowInvRate=34.7561ns
BM_TrimConstant/10000/25/25/100          357111 ns       356956 ns         2038 RowInvRate=35.6956ns
BM_TrimConstant/100000/25/25/100        3641161 ns      3640694 ns          192 RowInvRate=36.4069ns
BM_TrimConstant/1000/50/25/100            34973 ns        34961 ns        20306 RowInvRate=34.9605ns
BM_TrimConstant/10000/50/25/100          370814 ns       370732 ns         1844 RowInvRate=37.0732ns
BM_TrimConstant/100000/50/25/100        3587445 ns      3586963 ns          181 RowInvRate=35.8696ns
BM_TrimConstant/1000/100/25/100           38184 ns        38183 ns        18749 RowInvRate=38.1831ns
BM_TrimConstant/10000/100/25/100         381448 ns       381426 ns         1794 RowInvRate=38.1426ns
BM_TrimConstant/100000/100/25/100       6259811 ns      6259441 ns          107 RowInvRate=62.5944ns
BM_TrimNonConstant/1000/25/1/25           50110 ns        50096 ns        14297 RowInvRate=50.0963ns
BM_TrimNonConstant/10000/25/1/25         494897 ns       494763 ns         1347 RowInvRate=49.4763ns
BM_TrimNonConstant/100000/25/1/25       5038741 ns      5037321 ns          130 RowInvRate=50.3732ns
BM_TrimNonConstant/1000/50/1/25           50205 ns        50200 ns        14488 RowInvRate=50.2002ns
BM_TrimNonConstant/10000/50/1/25         517966 ns       517864 ns         1339 RowInvRate=51.7864ns
BM_TrimNonConstant/100000/50/1/25       6104975 ns      6102341 ns          143 RowInvRate=61.0234ns
BM_TrimNonConstant/1000/100/1/25          51434 ns        51422 ns        13363 RowInvRate=51.4222ns
BM_TrimNonConstant/10000/100/1/25        555793 ns       555720 ns         1386 RowInvRate=55.572ns
BM_TrimNonConstant/100000/100/1/25      8124166 ns      8120642 ns           86 RowInvRate=81.2064ns
BM_TrimNonConstant/1000/25/5/25           76564 ns        76552 ns         8565 RowInvRate=76.5515ns
BM_TrimNonConstant/10000/25/5/25         769112 ns       769059 ns          930 RowInvRate=76.9059ns
BM_TrimNonConstant/100000/25/5/25       7789654 ns      7786903 ns           90 RowInvRate=77.869ns
BM_TrimNonConstant/1000/50/5/25           80672 ns        80650 ns         8522 RowInvRate=80.6496ns
BM_TrimNonConstant/10000/50/5/25         781704 ns       781430 ns          878 RowInvRate=78.143ns
BM_TrimNonConstant/100000/50/5/25       9415568 ns      9413276 ns           85 RowInvRate=94.1328ns
BM_TrimNonConstant/1000/100/5/25          85235 ns        85206 ns         8757 RowInvRate=85.2064ns
BM_TrimNonConstant/10000/100/5/25        811362 ns       811103 ns          864 RowInvRate=81.1103ns
BM_TrimNonConstant/100000/100/5/25     13260208 ns     13217735 ns           65 RowInvRate=132.177ns
BM_TrimNonConstant/1000/25/10/25          95780 ns        95762 ns         6746 RowInvRate=95.7618ns
BM_TrimNonConstant/10000/25/10/25       1017279 ns      1017223 ns          689 RowInvRate=101.722ns
BM_TrimNonConstant/100000/25/10/25      9830306 ns      9826765 ns           72 RowInvRate=98.2676ns
BM_TrimNonConstant/1000/50/10/25         103441 ns       103438 ns         6849 RowInvRate=103.438ns
BM_TrimNonConstant/10000/50/10/25       1040043 ns      1039727 ns          682 RowInvRate=103.973ns
BM_TrimNonConstant/100000/50/10/25     11082865 ns     11078419 ns           65 RowInvRate=110.784ns
BM_TrimNonConstant/1000/100/10/25         99476 ns        99473 ns         7030 RowInvRate=99.4728ns
BM_TrimNonConstant/10000/100/10/25       986759 ns       986755 ns          707 RowInvRate=98.6755ns
BM_TrimNonConstant/100000/100/10/25    12894085 ns     12888006 ns           55 RowInvRate=128.88ns
BM_TrimNonConstant/1000/25/25/25         123194 ns       123169 ns         5603 RowInvRate=123.169ns
BM_TrimNonConstant/10000/25/25/25       1350476 ns      1350330 ns          603 RowInvRate=135.033ns
BM_TrimNonConstant/100000/25/25/25     11783915 ns     11780869 ns           57 RowInvRate=117.809ns
BM_TrimNonConstant/1000/50/25/25         115108 ns       115092 ns         5035 RowInvRate=115.092ns
BM_TrimNonConstant/10000/50/25/25       1360436 ns      1360254 ns          608 RowInvRate=136.025ns
BM_TrimNonConstant/100000/50/25/25     12142829 ns     12138759 ns           48 RowInvRate=121.388ns
BM_TrimNonConstant/1000/100/25/25        141299 ns       141268 ns         5262 RowInvRate=141.268ns
BM_TrimNonConstant/10000/100/25/25      1257038 ns      1256963 ns          602 RowInvRate=125.696ns
BM_TrimNonConstant/100000/100/25/25    19289585 ns     19286676 ns           47 RowInvRate=192.867ns
BM_TrimNonConstant/1000/25/1/50           51326 ns        51318 ns        14252 RowInvRate=51.3176ns
BM_TrimNonConstant/10000/25/1/50         507233 ns       507112 ns         1443 RowInvRate=50.7112ns
BM_TrimNonConstant/100000/25/1/50       4954438 ns      4953936 ns          146 RowInvRate=49.5394ns
BM_TrimNonConstant/1000/50/1/50           52673 ns        52667 ns        13090 RowInvRate=52.6667ns
BM_TrimNonConstant/10000/50/1/50         528923 ns       528888 ns         1325 RowInvRate=52.8888ns
BM_TrimNonConstant/100000/50/1/50       6267215 ns      6265762 ns          129 RowInvRate=62.6576ns
BM_TrimNonConstant/1000/100/1/50          53991 ns        53949 ns        13779 RowInvRate=53.9488ns
BM_TrimNonConstant/10000/100/1/50        517195 ns       517115 ns         1345 RowInvRate=51.7115ns
BM_TrimNonConstant/100000/100/1/50      7874661 ns      7871015 ns           66 RowInvRate=78.7101ns
BM_TrimNonConstant/1000/25/5/50           76192 ns        76185 ns         9153 RowInvRate=76.1847ns
BM_TrimNonConstant/10000/25/5/50         812539 ns       812508 ns          862 RowInvRate=81.2508ns
BM_TrimNonConstant/100000/25/5/50       7596376 ns      7595284 ns           94 RowInvRate=75.9528ns
BM_TrimNonConstant/1000/50/5/50           77137 ns        77132 ns         9152 RowInvRate=77.1321ns
BM_TrimNonConstant/10000/50/5/50         761437 ns       761381 ns          909 RowInvRate=76.1381ns
BM_TrimNonConstant/100000/50/5/50       8969809 ns      8968753 ns           89 RowInvRate=89.6875ns
BM_TrimNonConstant/1000/100/5/50          83978 ns        83954 ns         8837 RowInvRate=83.954ns
BM_TrimNonConstant/10000/100/5/50        805542 ns       805391 ns          872 RowInvRate=80.5391ns
BM_TrimNonConstant/100000/100/5/50     10774818 ns     10771995 ns           66 RowInvRate=107.72ns
BM_TrimNonConstant/1000/25/10/50         104047 ns       104038 ns         7245 RowInvRate=104.038ns
BM_TrimNonConstant/10000/25/10/50       1008786 ns      1008541 ns          678 RowInvRate=100.854ns
BM_TrimNonConstant/100000/25/10/50      9816391 ns      9814324 ns           66 RowInvRate=98.1432ns
BM_TrimNonConstant/1000/50/10/50          98814 ns        98800 ns         7161 RowInvRate=98.8ns
BM_TrimNonConstant/10000/50/10/50        997609 ns       997439 ns          682 RowInvRate=99.7439ns
BM_TrimNonConstant/100000/50/10/50     11216564 ns     11199811 ns           71 RowInvRate=111.998ns
BM_TrimNonConstant/1000/100/10/50        104884 ns       104845 ns         6723 RowInvRate=104.845ns
BM_TrimNonConstant/10000/100/10/50      1111357 ns      1110627 ns          621 RowInvRate=111.063ns
BM_TrimNonConstant/100000/100/10/50    14515417 ns     14513559 ns           42 RowInvRate=145.136ns
BM_TrimNonConstant/1000/25/25/50         126435 ns       126384 ns         5008 RowInvRate=126.384ns
BM_TrimNonConstant/10000/25/25/50       1426780 ns      1426199 ns          583 RowInvRate=142.62ns
BM_TrimNonConstant/100000/25/25/50     12903757 ns     12899844 ns           61 RowInvRate=128.998ns
BM_TrimNonConstant/1000/50/25/50         131587 ns       131555 ns         4860 RowInvRate=131.555ns
BM_TrimNonConstant/10000/50/25/50       1576709 ns      1575996 ns          535 RowInvRate=157.6ns
BM_TrimNonConstant/100000/50/25/50     15565458 ns     15548139 ns           49 RowInvRate=155.481ns
BM_TrimNonConstant/1000/100/25/50        126422 ns       126377 ns         4475 RowInvRate=126.377ns
BM_TrimNonConstant/10000/100/25/50      1399695 ns      1399552 ns          514 RowInvRate=139.955ns
BM_TrimNonConstant/100000/100/25/50    17418711 ns     17412677 ns           34 RowInvRate=174.127ns
BM_TrimNonConstant/1000/25/1/75           53382 ns        53369 ns        13591 RowInvRate=53.3689ns
BM_TrimNonConstant/10000/25/1/75         518358 ns       518306 ns         1000 RowInvRate=51.8306ns
BM_TrimNonConstant/100000/25/1/75       5448297 ns      5446349 ns          129 RowInvRate=54.4635ns
BM_TrimNonConstant/1000/50/1/75           53514 ns        53499 ns        13881 RowInvRate=53.4992ns
BM_TrimNonConstant/10000/50/1/75         516501 ns       516407 ns         1392 RowInvRate=51.6407ns
BM_TrimNonConstant/100000/50/1/75       6107000 ns      6100736 ns           93 RowInvRate=61.0074ns
BM_TrimNonConstant/1000/100/1/75          57941 ns        57926 ns        12266 RowInvRate=57.9259ns
BM_TrimNonConstant/10000/100/1/75        539168 ns       538719 ns         1047 RowInvRate=53.8719ns
BM_TrimNonConstant/100000/100/1/75      8298717 ns      8296825 ns           67 RowInvRate=82.9683ns
BM_TrimNonConstant/1000/25/5/75           78902 ns        78877 ns         8768 RowInvRate=78.8766ns
BM_TrimNonConstant/10000/25/5/75         859683 ns       859553 ns          858 RowInvRate=85.9553ns
BM_TrimNonConstant/100000/25/5/75       8681606 ns      8678690 ns           81 RowInvRate=86.7869ns
BM_TrimNonConstant/1000/50/5/75           81625 ns        81606 ns         8547 RowInvRate=81.6059ns
BM_TrimNonConstant/10000/50/5/75         789724 ns       789649 ns          881 RowInvRate=78.9649ns
BM_TrimNonConstant/100000/50/5/75       7971712 ns      7970724 ns           77 RowInvRate=79.7072ns
BM_TrimNonConstant/1000/100/5/75          85089 ns        85072 ns         8174 RowInvRate=85.0723ns
BM_TrimNonConstant/10000/100/5/75        896703 ns       896642 ns          772 RowInvRate=89.6642ns
BM_TrimNonConstant/100000/100/5/75     13478480 ns     13472983 ns           61 RowInvRate=134.73ns
BM_TrimNonConstant/1000/25/10/75         102404 ns       102388 ns         7156 RowInvRate=102.388ns
BM_TrimNonConstant/10000/25/10/75        977117 ns       977097 ns          717 RowInvRate=97.7097ns
BM_TrimNonConstant/100000/25/10/75      9868236 ns      9867714 ns           71 RowInvRate=98.6771ns
BM_TrimNonConstant/1000/50/10/75         103491 ns       103475 ns         7059 RowInvRate=103.475ns
BM_TrimNonConstant/10000/50/10/75       1033139 ns      1032769 ns          656 RowInvRate=103.277ns
BM_TrimNonConstant/100000/50/10/75      9942056 ns      9941030 ns           64 RowInvRate=99.4103ns
BM_TrimNonConstant/1000/100/10/75        106928 ns       106918 ns         6840 RowInvRate=106.918ns
BM_TrimNonConstant/10000/100/10/75      1028617 ns      1028548 ns          700 RowInvRate=102.855ns
BM_TrimNonConstant/100000/100/10/75    12760907 ns     12760175 ns           52 RowInvRate=127.602ns
BM_TrimNonConstant/1000/25/25/75         119634 ns       119625 ns         5793 RowInvRate=119.625ns
BM_TrimNonConstant/10000/25/25/75       1422724 ns      1422466 ns          604 RowInvRate=142.247ns
BM_TrimNonConstant/100000/25/25/75     15317931 ns     15295467 ns           54 RowInvRate=152.955ns
BM_TrimNonConstant/1000/50/25/75         123873 ns       123833 ns         5033 RowInvRate=123.833ns
BM_TrimNonConstant/10000/50/25/75       1357671 ns      1357514 ns          553 RowInvRate=135.751ns
BM_TrimNonConstant/100000/50/25/75     12757593 ns     12756566 ns           47 RowInvRate=127.566ns
BM_TrimNonConstant/1000/100/25/75        149447 ns       149420 ns         5155 RowInvRate=149.42ns
BM_TrimNonConstant/10000/100/25/75      1280026 ns      1279684 ns          563 RowInvRate=127.968ns
BM_TrimNonConstant/100000/100/25/75    17830429 ns     17823981 ns           40 RowInvRate=178.24ns
BM_TrimNonConstant/1000/25/1/100          51539 ns        51533 ns        13009 RowInvRate=51.5326ns
BM_TrimNonConstant/10000/25/1/100        536135 ns       536053 ns         1310 RowInvRate=53.6053ns
BM_TrimNonConstant/100000/25/1/100      5056916 ns      5056316 ns          138 RowInvRate=50.5632ns
BM_TrimNonConstant/1000/50/1/100          54549 ns        54537 ns        12672 RowInvRate=54.5368ns
BM_TrimNonConstant/10000/50/1/100        526901 ns       526831 ns         1300 RowInvRate=52.6831ns
BM_TrimNonConstant/100000/50/1/100      5136416 ns      5135730 ns          138 RowInvRate=51.3573ns
BM_TrimNonConstant/1000/100/1/100         53151 ns        53135 ns        13655 RowInvRate=53.1347ns
BM_TrimNonConstant/10000/100/1/100       526651 ns       526566 ns         1235 RowInvRate=52.6566ns
BM_TrimNonConstant/100000/100/1/100    10298498 ns     10277383 ns           89 RowInvRate=102.774ns
BM_TrimNonConstant/1000/25/5/100          77984 ns        77980 ns         8999 RowInvRate=77.9801ns
BM_TrimNonConstant/10000/25/5/100        801228 ns       800951 ns          839 RowInvRate=80.0951ns
BM_TrimNonConstant/100000/25/5/100      8565434 ns      8563152 ns           87 RowInvRate=85.6315ns
BM_TrimNonConstant/1000/50/5/100          82842 ns        82812 ns         8070 RowInvRate=82.812ns
BM_TrimNonConstant/10000/50/5/100        775849 ns       775672 ns          909 RowInvRate=77.5672ns
BM_TrimNonConstant/100000/50/5/100      7859526 ns      7857665 ns           87 RowInvRate=78.5767ns
BM_TrimNonConstant/1000/100/5/100         87587 ns        87574 ns         8411 RowInvRate=87.5736ns
BM_TrimNonConstant/10000/100/5/100       888932 ns       888511 ns          773 RowInvRate=88.8511ns
BM_TrimNonConstant/100000/100/5/100    12236032 ns     12235418 ns           52 RowInvRate=122.354ns
BM_TrimNonConstant/1000/25/10/100         98701 ns        98692 ns         7225 RowInvRate=98.6919ns
BM_TrimNonConstant/10000/25/10/100      1002512 ns       999172 ns          715 RowInvRate=99.9172ns
BM_TrimNonConstant/100000/25/10/100    10325312 ns     10321557 ns           66 RowInvRate=103.216ns
BM_TrimNonConstant/1000/50/10/100         98221 ns        98210 ns         7082 RowInvRate=98.2095ns
BM_TrimNonConstant/10000/50/10/100       985182 ns       985155 ns          715 RowInvRate=98.5155ns
BM_TrimNonConstant/100000/50/10/100    11005510 ns     11002646 ns           60 RowInvRate=110.026ns
BM_TrimNonConstant/1000/100/10/100       102102 ns       102090 ns         7015 RowInvRate=102.09ns
BM_TrimNonConstant/10000/100/10/100     1023776 ns      1023389 ns          707 RowInvRate=102.339ns
BM_TrimNonConstant/100000/100/10/100   15475501 ns     15472268 ns           54 RowInvRate=154.723ns
BM_TrimNonConstant/1000/25/25/100        121292 ns       121245 ns         5366 RowInvRate=121.245ns
BM_TrimNonConstant/10000/25/25/100      1360373 ns      1360153 ns          561 RowInvRate=136.015ns
BM_TrimNonConstant/100000/25/25/100    12016366 ns     12015652 ns           61 RowInvRate=120.157ns
BM_TrimNonConstant/1000/50/25/100        115570 ns       115567 ns         5199 RowInvRate=115.567ns
BM_TrimNonConstant/10000/50/25/100      1193858 ns      1193557 ns          587 RowInvRate=119.356ns
BM_TrimNonConstant/100000/50/25/100    12178734 ns     12176212 ns           47 RowInvRate=121.762ns
BM_TrimNonConstant/1000/100/25/100       138016 ns       137987 ns         5733 RowInvRate=137.987ns
BM_TrimNonConstant/10000/100/25/100     1282818 ns      1279853 ns          555 RowInvRate=127.985ns
BM_TrimNonConstant/100000/100/25/100   19980671 ns     19973974 ns           42 RowInvRate=199.74ns
 */

enum TrimCharsType {
    CONSTANT,
    NON_CONSTANT,
};

static std::string generate_random_word(std::mt19937& random_generator, const std::string& trim_chars, const bool add_trim_chars, std::normal_distribution<double>& length_dist, std::uniform_int_distribution<int>& trim_char_count_dist) {
    const std::string chars{"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"};

    int word_length{static_cast<int>(std::round(length_dist(random_generator)))};
    word_length = std::max(1, word_length);
    std::string word;

    // Add trim_chars to the word first
    const int trim_char_count{add_trim_chars ? trim_char_count_dist(random_generator) : 0};
    for (int i{0}; i < trim_char_count; ++i) {
        word += trim_chars[random_generator() % trim_chars.size()];
    }

    // Subtract number of trim_chars from the word length
    word_length -= trim_char_count;
    // Ensure the word has at least one character
    if (word_length < 1) word_length = 1;

    // Generate random word
    for (int i{0}; i < word_length; ++i) {
        word += chars[random_generator() % chars.size()];
    }

    return word;
}

static ColumnPtr generate_input_column(const int num_rows, const int avg_word_length, const std::string& trim_chars, const int percentage_trimmed_words) {
    // Control the spread of the word length of all words
    const double variance{2.0};
    const auto input_column{ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true)};
    std::random_device random_device;
    std::mt19937 random_generator(random_device());
    std::normal_distribution<double> word_length_dist(avg_word_length, std::sqrt(variance));
    std::uniform_int_distribution<int> trim_char_count_dist(1, trim_chars.size());

    const int trim_cut{static_cast<int>(std::round(percentage_trimmed_words / num_rows))};
    for (size_t i{0}; i < num_rows; i++) {
        const auto add_trim_chars{i < trim_cut ? true : false};
        const auto word{generate_random_word(random_generator, trim_chars, add_trim_chars, word_length_dist, trim_char_count_dist)};
        input_column->append_datum(Slice(word));
    }

    return input_column;
}

static void do_bench(benchmark::State& state, TrimCharsType trim_chars_type) {
	const std::string unique_trim_chars{"%_^&!"};
    const int num_rows = state.range(0);
    const int avg_word_length = state.range(1);
    const int trim_char_length = state.range(2);
    const int percentage_trimmed_words = state.range(3);

    auto arg_types = {AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    auto return_type{AnyValUtil::column_type_to_type_desc(TypeDescriptor::from_logical_type(TYPE_VARCHAR))};
    std::unique_ptr<FunctionContext> ctx(FunctionContext::create_test_context(std::move(arg_types), std::move(return_type)));

    std::string trim_chars{};
    for (size_t i{0}; i < trim_char_length; ++i) {
        trim_chars += unique_trim_chars[i % unique_trim_chars.size()];
    }
    const auto input_column{generate_input_column(num_rows, avg_word_length, trim_chars, percentage_trimmed_words)};
    auto trim_char_column{ColumnHelper::create_column(TypeDescriptor(TYPE_VARCHAR), true)};

    switch (trim_chars_type) {
        case CONSTANT: {
            trim_char_column = ColumnHelper::create_const_column<TYPE_VARCHAR>(Slice(trim_chars), 1);
            ctx->set_constant_columns({nullptr, trim_char_column});
            break;
        }
        case NON_CONSTANT: {
            for (size_t i{0}; i < num_rows; i++) {
                trim_char_column->append_datum(Slice(trim_chars));
            }
            ctx->set_constant_columns({nullptr, nullptr});
            break;
        }
    }

    int total_rows{0};
    for (auto _ : state) {
        total_rows += num_rows;

        ASSERT_TRUE(CelonisTrim::ltrim_prepare(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
        ASSERT_TRUE(CelonisTrim::ltrim_prepare(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisTrim::ltrim(ctx.get(), {input_column, trim_char_column}).ok());
        ASSERT_TRUE(CelonisTrim::trim_close(ctx.get(), FunctionContext::THREAD_LOCAL).ok());
        ASSERT_TRUE(CelonisTrim::trim_close(ctx.get(), FunctionContext::FRAGMENT_LOCAL).ok());
    }

    state.counters["RowInvRate"] =
            benchmark::Counter(total_rows, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}

static void BM_TrimConstant(benchmark::State& state) {
    do_bench(state, CONSTANT);
}

static void BM_TrimNonConstant(benchmark::State& state) {
    do_bench(state, NON_CONSTANT);
}

// Args: Number of rows / Avg length of words / Length of chars to trim / % of input rows that start with trim_chars
BENCHMARK(BM_TrimConstant)->ArgsProduct({{1'000, 10'000, 100'000}, {25, 50, 100}, {1, 5, 10, 25}, {25, 50, 75, 100}});
BENCHMARK(BM_TrimNonConstant)->ArgsProduct({{1'000, 10'000, 100'000}, {25, 50, 100}, {1, 5, 10, 25}, {25, 50, 75, 100}});

} // namespace starrocks

BENCHMARK_MAIN();