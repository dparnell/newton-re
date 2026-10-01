# gLex8time: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 "." "t0"
set s1 "M" "t1"
set s2 "AP" "t2"
set s3 "m" "t3"
set s4 "ap" "t4"
set s5 "0123456789" "t5"
set s6 "012345" "t6"
set s7 ":" "t7"
set s8 "012" "t8"
set s9 "1" "t9"
set s10 "23456789" "t10"
set s11 "-" "t11"
set s12 "K" "t12"
set s13 "C" "t13"
set s14 "O" "t14"
set s15 "L" "t15"
set s16 "k" "t16"
set s17 "c" "t17"
set s18 "o" "t18"
set s19 "l" "t19"
set s20 "'" "t20"
set s21 "S" "t21"
set s22 "R" "t22"
set s23 "s" "t23"
set s24 "r" "t24"
set s25 "H" "t25"
set s26 "h" "t26"
set s27 "U" "t27"
set s28 "u" "t28"
set s29 "T" "t29"
set s30 "DS" "t30"
set s31 "MT" "t31"
set s32 "P" "t32"
set s33 "G" "t33"
set s34 "6789" "t34"
set s35 "CEM" "t35"
set s36 "A" "t36"
set s37 "CEMP" "t37"
set s38 "345" "t38"
set s39 "01234" "t39"
set s40 "5" "t40"
set s41 "2" "t41"
set s42 "3456789" "t42"
set s43 "0" "t43"
set s44 "01" "t44"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s9 word -> n369
node n1 s41 word -> n423
node n2 s42 word -> n439
node n3 s43 word -> n454
node n4 s4 -> n21
node n5 s36 -> n16
node n6 s25 -> n76
node n7 s26 -> n79
node n8 s27 -> n81
node n9 s28 -> n84
node n10 s33 -> n119
node n11 s32 -> n103
node n12 s35 -> n144
node n13 s14 -> n63
node n14 s18 last -> n74
node n15 s1 last -> n422
node n16 s0 -> n15
node n17 s1 word last
node n18 s2 word -> n16
node n19 s4 word last -> n21
node n20 s3 last -> n422
node n21 s0 -> n20
node n22 s3 word last
node n23 s5 word last -> n18
node n24 s6 last -> n23
node n25 s7 -> n24
node n26 s2 word -> n16
node n27 s4 word last -> n21
node n28 s5 word last -> n25
node n29 s6 last -> n28
node n30 s7 -> n29
node n31 s2 word -> n16
node n32 s4 word last -> n21
node n33 s8 word -> n30
node n34 s7 -> n29
node n35 s2 word -> n16
node n36 s4 word last -> n21
node n37 s9 word -> n33
node n38 s10 word last -> n30
node n39 s11 last -> n37
node n40 s3 word -> n39
node n41 s11 -> n37
node n42 s0 last -> n44
node n43 s0 word last -> n39
node n44 s3 last -> n43
node n45 s4 word -> n40
node n46 s2 word -> n48
node n47 s11 last -> n195
node n48 s1 word -> n39
node n49 s11 -> n37
node n50 s0 last -> n51
node n51 s1 last -> n43
node n52 s12 word last
node n53 s13 last -> n52
node n54 s14 last -> n53
node n55 s15 -> n54
node n56 s19 last -> n59
node n57 s16 word last
node n58 s17 last -> n57
node n59 s18 last -> n58
node n60 s13 -> n55
node n61 s17 last -> n62
node n62 s19 last -> n59
node n63 s20 last -> n60
node n64 s14 -> n63
node n65 s18 -> n74
node n66 s25 word -> n76
node n67 s26 word -> n79
node n68 s27 -> n81
node n69 s28 -> n84
node n70 s7 -> n24
node n71 s2 word -> n16
node n72 s4 word last -> n21
node n73 s17 last -> n62
node n74 s20 last -> n73
node n75 s21 word last
node n76 s22 word -> n75
node n77 s24 word last -> n78
node n78 s23 word last
node n79 s24 word last -> n78
node n80 s22 word last
node n81 s25 -> n80
node n82 s26 last -> n83
node n83 s24 word last
node n84 s26 last -> n83
node n85 s5 word last -> n64
node n86 s6 last -> n85
node n87 s7 -> n86
node n88 s32 word -> n103
node n89 s25 word -> n110
node n90 s26 word -> n117
node n91 s33 -> n119
node n92 s0 -> n130
node n93 s6 word -> n137
node n94 s34 word -> n131
node n95 s27 -> n81
node n96 s28 -> n84
node n97 s35 -> n144
node n98 s36 word -> n16
node n99 s4 word -> n21
node n100 s14 -> n63
node n101 s18 last -> n74
node n102 s29 word last
node n103 s30 -> n102
node n104 s0 -> n15
node n105 s31 word last
node n106 s5 word last -> n75
node n107 s6 last -> n106
node n108 s1 word last -> n107
node n109 s5 word last -> n108
node n110 s6 -> n109
node n111 s22 word -> n75
node n112 s24 word last -> n78
node n113 s5 word last -> n78
node n114 s6 last -> n113
node n115 s3 word last -> n114
node n116 s5 word last -> n115
node n117 s6 -> n116
node n118 s24 word last -> n78
node n119 s1 last -> n102
node n120 s14 -> n63
node n121 s18 -> n74
node n122 s25 word -> n76
node n123 s26 word -> n79
node n124 s27 -> n81
node n125 s28 -> n84
node n126 s0 last -> n128
node n127 s5 word last
node n128 s6 last -> n127
node n129 s5 word last -> n120
node n130 s6 last -> n129
node n131 s14 -> n63
node n132 s18 -> n74
node n133 s25 word -> n76
node n134 s26 word -> n79
node n135 s27 -> n81
node n136 s28 last -> n84
node n137 s5 word -> n131
node n138 s14 -> n63
node n139 s18 -> n74
node n140 s25 word -> n76
node n141 s26 word -> n79
node n142 s27 -> n81
node n143 s28 last -> n84
node n144 s30 -> n102
node n145 s29 word last
node n146 s8 word -> n87
node n147 s7 -> n86
node n148 s32 word -> n103
node n149 s38 word -> n162
node n150 s34 word -> n183
node n151 s25 word -> n110
node n152 s26 word -> n117
node n153 s33 -> n119
node n154 s0 -> n130
node n155 s27 -> n81
node n156 s28 -> n84
node n157 s35 -> n144
node n158 s36 word -> n16
node n159 s4 word -> n21
node n160 s14 -> n63
node n161 s18 last -> n74
node n162 s25 word -> n110
node n163 s26 word -> n117
node n164 s37 -> n144
node n165 s33 -> n119
node n166 s7 -> n182
node n167 s0 -> n130
node n168 s6 word -> n137
node n169 s34 word -> n131
node n170 s27 -> n81
node n171 s28 -> n84
node n172 s14 -> n63
node n173 s18 last -> n74
node n174 s14 -> n63
node n175 s18 -> n74
node n176 s25 word -> n76
node n177 s26 word -> n79
node n178 s27 -> n81
node n179 s28 -> n84
node n180 s7 last -> n128
node n181 s5 word last -> n174
node n182 s6 last -> n181
node n183 s25 word -> n110
node n184 s26 word -> n117
node n185 s37 -> n144
node n186 s33 -> n119
node n187 s7 -> n182
node n188 s0 -> n130
node n189 s6 -> n194
node n190 s27 -> n81
node n191 s28 -> n84
node n192 s14 -> n63
node n193 s18 last -> n74
node n194 s5 word last -> n131
node n195 s9 word -> n146
node n196 s41 word -> n199
node n197 s42 word -> n214
node n198 s43 word last -> n228
node n199 s7 -> n86
node n200 s32 word -> n103
node n201 s25 word -> n110
node n202 s26 word -> n117
node n203 s33 -> n119
node n204 s0 -> n130
node n205 s39 word -> n162
node n206 s40 -> n194
node n207 s27 -> n81
node n208 s28 -> n84
node n209 s35 -> n144
node n210 s36 word -> n16
node n211 s4 word -> n21
node n212 s14 -> n63
node n213 s18 last -> n74
node n214 s7 -> n86
node n215 s32 word -> n103
node n216 s25 word -> n110
node n217 s26 word -> n117
node n218 s33 -> n119
node n219 s0 -> n130
node n220 s6 -> n194
node n221 s27 -> n81
node n222 s28 -> n84
node n223 s35 -> n144
node n224 s36 word -> n16
node n225 s4 word -> n21
node n226 s14 -> n63
node n227 s18 last -> n74
node n228 s6 word -> n162
node n229 s34 word -> n183
node n230 s25 word -> n110
node n231 s26 word -> n117
node n232 s37 -> n144
node n233 s33 -> n119
node n234 s7 -> n182
node n235 s0 -> n130
node n236 s27 -> n81
node n237 s28 -> n84
node n238 s14 -> n63
node n239 s18 last -> n74
node n240 s5 word last -> n45
node n241 s6 last -> n240
node n242 s7 -> n241
node n243 s4 word -> n40
node n244 s2 word -> n48
node n245 s11 -> n195
node n246 s14 -> n279
node n247 s18 -> n281
node n248 s25 word -> n282
node n249 s26 word -> n289
node n250 s27 -> n292
node n251 s28 last -> n295
node n252 s44 word -> n228
node n253 s41 word -> n255
node n254 s42 word last -> n183
node n255 s25 word -> n110
node n256 s26 word -> n117
node n257 s37 -> n144
node n258 s33 -> n119
node n259 s7 -> n182
node n260 s0 -> n130
node n261 s39 word -> n162
node n262 s40 -> n194
node n263 s27 -> n81
node n264 s28 -> n84
node n265 s14 -> n63
node n266 s18 last -> n74
node n267 s11 last -> n252
node n268 s12 word last -> n267
node n269 s13 last -> n268
node n270 s14 last -> n269
node n271 s15 -> n270
node n272 s19 last -> n275
node n273 s16 word last -> n267
node n274 s17 last -> n273
node n275 s18 last -> n274
node n276 s13 -> n271
node n277 s17 last -> n278
node n278 s19 last -> n275
node n279 s20 last -> n276
node n280 s17 last -> n278
node n281 s20 last -> n280
node n282 s11 -> n252
node n283 s24 word -> n285
node n284 s22 word last -> n287
node n285 s23 word -> n267
node n286 s11 last -> n252
node n287 s21 word -> n267
node n288 s11 last -> n252
node n289 s11 -> n252
node n290 s24 word last -> n285
node n291 s22 word last -> n267
node n292 s25 -> n291
node n293 s26 last -> n294
node n294 s24 word last -> n267
node n295 s26 last -> n294
node n296 s5 word last -> n242
node n297 s6 last -> n296
node n298 s7 -> n297
node n299 s4 word -> n40
node n300 s32 word -> n314
node n301 s11 -> n195
node n302 s25 word -> n320
node n303 s26 word -> n330
node n304 s33 -> n339
node n305 s0 -> n351
node n306 s6 word -> n359
node n307 s34 word -> n352
node n308 s35 -> n367
node n309 s27 -> n292
node n310 s28 -> n295
node n311 s36 word -> n48
node n312 s14 -> n279
node n313 s18 last -> n281
node n314 s1 word -> n39
node n315 s11 -> n37
node n316 s29 word -> n267
node n317 s30 -> n319
node n318 s0 last -> n51
node n319 s29 word last -> n267
node n320 s11 -> n252
node n321 s6 -> n329
node n322 s24 word -> n285
node n323 s22 word last -> n287
node n324 s5 word last -> n287
node n325 s6 -> n324
node n326 s11 last -> n252
node n327 s1 word -> n325
node n328 s11 last -> n252
node n329 s5 word last -> n327
node n330 s11 -> n252
node n331 s6 -> n338
node n332 s24 word last -> n285
node n333 s5 word last -> n285
node n334 s6 -> n333
node n335 s11 last -> n252
node n336 s3 word -> n334
node n337 s11 last -> n252
node n338 s5 word last -> n336
node n339 s1 last -> n319
node n340 s14 -> n279
node n341 s18 -> n281
node n342 s25 word -> n282
node n343 s26 word -> n289
node n344 s27 -> n292
node n345 s28 -> n295
node n346 s11 -> n252
node n347 s0 last -> n349
node n348 s5 word last -> n267
node n349 s6 last -> n348
node n350 s5 word last -> n340
node n351 s6 last -> n350
node n352 s14 -> n279
node n353 s18 -> n281
node n354 s25 word -> n282
node n355 s26 word -> n289
node n356 s27 -> n292
node n357 s28 -> n295
node n358 s11 last -> n252
node n359 s5 word -> n352
node n360 s14 -> n279
node n361 s18 -> n281
node n362 s25 word -> n282
node n363 s26 word -> n289
node n364 s27 -> n292
node n365 s28 -> n295
node n366 s11 last -> n252
node n367 s29 word -> n267
node n368 s30 last -> n319
node n369 s8 word -> n298
node n370 s7 -> n297
node n371 s4 word -> n40
node n372 s32 word -> n314
node n373 s11 -> n195
node n374 s38 word -> n386
node n375 s34 word -> n409
node n376 s25 word -> n320
node n377 s26 word -> n330
node n378 s33 -> n339
node n379 s0 -> n351
node n380 s35 -> n367
node n381 s27 -> n292
node n382 s28 -> n295
node n383 s36 word -> n48
node n384 s14 -> n279
node n385 s18 last -> n281
node n386 s25 word -> n320
node n387 s26 word -> n330
node n388 s37 -> n367
node n389 s33 -> n339
node n390 s7 -> n408
node n391 s0 -> n351
node n392 s6 word -> n359
node n393 s11 -> n252
node n394 s34 word -> n352
node n395 s27 -> n292
node n396 s28 -> n295
node n397 s14 -> n279
node n398 s18 last -> n281
node n399 s14 -> n279
node n400 s18 -> n281
node n401 s25 word -> n282
node n402 s26 word -> n289
node n403 s27 -> n292
node n404 s28 -> n295
node n405 s11 -> n252
node n406 s7 last -> n349
node n407 s5 word last -> n399
node n408 s6 last -> n407
node n409 s25 word -> n320
node n410 s26 word -> n330
node n411 s37 -> n367
node n412 s33 -> n339
node n413 s7 -> n408
node n414 s0 -> n351
node n415 s6 -> n421
node n416 s11 -> n252
node n417 s27 -> n292
node n418 s28 -> n295
node n419 s14 -> n279
node n420 s18 last -> n281
node n421 s5 word last -> n352
node n422 s0 word last
node n423 s7 -> n297
node n424 s4 word -> n40
node n425 s32 word -> n314
node n426 s11 -> n195
node n427 s25 word -> n320
node n428 s26 word -> n330
node n429 s33 -> n339
node n430 s0 -> n351
node n431 s39 word -> n386
node n432 s40 -> n421
node n433 s35 -> n367
node n434 s27 -> n292
node n435 s28 -> n295
node n436 s36 word -> n48
node n437 s14 -> n279
node n438 s18 last -> n281
node n439 s7 -> n297
node n440 s4 word -> n40
node n441 s32 word -> n314
node n442 s11 -> n195
node n443 s25 word -> n320
node n444 s26 word -> n330
node n445 s33 -> n339
node n446 s0 -> n351
node n447 s6 -> n421
node n448 s35 -> n367
node n449 s27 -> n292
node n450 s28 -> n295
node n451 s36 word -> n48
node n452 s14 -> n279
node n453 s18 last -> n281
node n454 s6 word -> n386
node n455 s34 word -> n409
node n456 s25 word -> n320
node n457 s26 word -> n330
node n458 s37 -> n367
node n459 s33 -> n339
node n460 s7 -> n408
node n461 s0 -> n351
node n462 s11 -> n252
node n463 s27 -> n292
node n464 s28 -> n295
node n465 s14 -> n279
node n466 s18 last -> n281
