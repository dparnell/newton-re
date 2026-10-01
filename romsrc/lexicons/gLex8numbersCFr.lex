# gLex8numbersCFr: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 "0123456789" "t0"
set s1 "," "t1"
set s2 "0" "t2"
set s3 "'." "t3"
set s4 "123456789" "t4"
set s5 "+-" "t5"
set s6 "^" "t6"
set s7 "Ee" "t7"
set s8 "1" "t8"
set s9 "Xx" "t9"
set s10 "#%" "t10"
set s11 "-:_" "t11"
set s12 "CF" "t12"
set s13 "°" "t13"
set s14 "e" "t14"
set s15 "m" "t15"
set s16 "E" "t16"
set s17 "M" "t17"
set s18 "è" "t18"
set s19 "È" "t19"
set s20 "h" "t20"
set s21 "t" "t21"
set s22 "H" "t22"
set s23 "T" "t23"
set s24 "#%ªº" "t24"
set s25 "2" "t25"
set s26 "3" "t26"
set s27 "0456789" "t27"
set s28 "d" "t28"
set s29 "r" "t29"
set s30 "D" "t30"
set s31 "R" "t31"
set s32 "n" "t32"
set s33 "N" "t33"
set s34 "023456789" "t34"
set s35 "s" "t35"
set s36 "S" "t36"
set s37 "456789" "t37"
set s38 ")" "t38"
set s39 "(" "t39"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s10 -> n91
node n1 s5 -> n150
node n2 s2 word -> n195
node n3 s8 word -> n390
node n4 s25 word -> n481
node n5 s26 word -> n547
node n6 s37 word -> n564
node n7 s1 -> n588
node n8 s39 last -> n593
node n9 s0 word last -> n9
node n10 s1 word last -> n9
node n11 s2 word -> n10
node n12 s4 word -> n27
node n13 s1 last -> n30
node n14 s0 word -> n14
node n15 s1 word last -> n9
node n16 s0 word -> n14
node n17 s3 -> n23
node n18 s1 word last -> n9
node n19 s3 -> n23
node n20 s1 word last -> n9
node n21 s0 word last -> n19
node n22 s0 last -> n21
node n23 s0 last -> n22
node n24 s0 word -> n16
node n25 s3 -> n23
node n26 s1 word last -> n9
node n27 s0 word -> n24
node n28 s3 -> n23
node n29 s1 word last -> n9
node n30 s0 word last -> n30
node n31 s5 -> n11
node n32 s2 word -> n10
node n33 s4 word -> n27
node n34 s1 last -> n30
node n35 s6 -> n31
node n36 s1 word -> n39
node n37 s7 -> n45
node n38 s9 last -> n51
node n39 s0 word -> n39
node n40 s6 -> n31
node n41 s7 -> n45
node n42 s9 last -> n51
node n43 s0 word last -> n43
node n44 s4 word last -> n43
node n45 s5 -> n44
node n46 s4 word last -> n43
node n47 s6 -> n45
node n48 s5 -> n44
node n49 s4 word last -> n43
node n50 s2 last -> n47
node n51 s8 last -> n50
node n52 s2 word -> n35
node n53 s4 word -> n80
node n54 s1 last -> n90
node n55 s0 word -> n55
node n56 s6 -> n31
node n57 s1 word -> n39
node n58 s7 -> n45
node n59 s9 last -> n51
node n60 s0 word -> n55
node n61 s6 -> n31
node n62 s3 -> n73
node n63 s1 word -> n39
node n64 s7 -> n45
node n65 s9 last -> n51
node n66 s6 -> n31
node n67 s3 -> n73
node n68 s1 word -> n39
node n69 s7 -> n45
node n70 s9 last -> n51
node n71 s0 word last -> n66
node n72 s0 last -> n71
node n73 s0 last -> n72
node n74 s0 word -> n60
node n75 s6 -> n31
node n76 s3 -> n73
node n77 s1 word -> n39
node n78 s7 -> n45
node n79 s9 last -> n51
node n80 s0 word -> n74
node n81 s6 -> n31
node n82 s3 -> n73
node n83 s1 word -> n39
node n84 s7 -> n45
node n85 s9 last -> n51
node n86 s6 -> n31
node n87 s0 word -> n86
node n88 s7 -> n45
node n89 s9 last -> n51
node n90 s0 word last -> n86
node n91 s5 -> n52
node n92 s2 word -> n35
node n93 s4 word -> n80
node n94 s1 last -> n90
node n95 s0 word -> n95
node n96 s10 word last
node n97 s1 word -> n95
node n98 s10 word last
node n99 s2 word -> n97
node n100 s4 word -> n119
node n101 s1 last -> n125
node n102 s0 word -> n102
node n103 s1 word -> n95
node n104 s10 word last
node n105 s0 word -> n102
node n106 s3 -> n114
node n107 s1 word -> n95
node n108 s10 word last
node n109 s3 -> n114
node n110 s1 word -> n95
node n111 s10 word last
node n112 s0 word last -> n109
node n113 s0 last -> n112
node n114 s0 last -> n113
node n115 s0 word -> n105
node n116 s3 -> n114
node n117 s1 word -> n95
node n118 s10 word last
node n119 s0 word -> n115
node n120 s3 -> n114
node n121 s1 word -> n95
node n122 s10 word last
node n123 s0 word -> n123
node n124 s10 word last
node n125 s0 word last -> n123
node n126 s5 -> n99
node n127 s2 word -> n97
node n128 s4 word -> n119
node n129 s1 last -> n125
node n130 s6 -> n126
node n131 s1 word -> n135
node n132 s7 -> n143
node n133 s9 -> n149
node n134 s10 word last
node n135 s0 word -> n135
node n136 s6 -> n126
node n137 s7 -> n143
node n138 s9 -> n149
node n139 s10 word last
node n140 s0 word -> n140
node n141 s10 word last
node n142 s4 word last -> n140
node n143 s5 -> n142
node n144 s4 word last -> n140
node n145 s6 -> n143
node n146 s5 -> n142
node n147 s4 word last -> n140
node n148 s2 last -> n145
node n149 s8 last -> n148
node n150 s2 word -> n130
node n151 s4 word -> n182
node n152 s1 last -> n194
node n153 s0 word -> n153
node n154 s6 -> n126
node n155 s1 word -> n135
node n156 s7 -> n143
node n157 s9 -> n149
node n158 s10 word last
node n159 s0 word -> n153
node n160 s6 -> n126
node n161 s3 -> n174
node n162 s1 word -> n135
node n163 s7 -> n143
node n164 s9 -> n149
node n165 s10 word last
node n166 s6 -> n126
node n167 s3 -> n174
node n168 s1 word -> n135
node n169 s7 -> n143
node n170 s9 -> n149
node n171 s10 word last
node n172 s0 word last -> n166
node n173 s0 last -> n172
node n174 s0 last -> n173
node n175 s0 word -> n159
node n176 s6 -> n126
node n177 s3 -> n174
node n178 s1 word -> n135
node n179 s7 -> n143
node n180 s9 -> n149
node n181 s10 word last
node n182 s0 word -> n175
node n183 s6 -> n126
node n184 s3 -> n174
node n185 s1 word -> n135
node n186 s7 -> n143
node n187 s9 -> n149
node n188 s10 word last
node n189 s6 -> n126
node n190 s0 word -> n189
node n191 s7 -> n143
node n192 s9 -> n149
node n193 s10 word last
node n194 s0 word last -> n189
node n195 s6 -> n126
node n196 s1 word -> n207
node n197 s14 word -> n237
node n198 s16 word -> n241
node n199 s9 -> n149
node n200 s11 -> n216
node n201 s13 word -> n236
node n202 s18 -> n245
node n203 s19 -> n246
node n204 s21 -> n247
node n205 s23 -> n248
node n206 s24 word last
node n207 s0 word -> n207
node n208 s6 -> n126
node n209 s7 -> n143
node n210 s9 -> n149
node n211 s11 -> n216
node n212 s13 word -> n236
node n213 s10 word last
node n214 s0 word last -> n214
node n215 s1 word last -> n214
node n216 s2 word -> n215
node n217 s4 word -> n232
node n218 s1 last -> n235
node n219 s0 word -> n219
node n220 s1 word last -> n214
node n221 s0 word -> n219
node n222 s3 -> n228
node n223 s1 word last -> n214
node n224 s3 -> n228
node n225 s1 word last -> n214
node n226 s0 word last -> n224
node n227 s0 last -> n226
node n228 s0 last -> n227
node n229 s0 word -> n221
node n230 s3 -> n228
node n231 s1 word last -> n214
node n232 s0 word -> n229
node n233 s3 -> n228
node n234 s1 word last -> n214
node n235 s0 word last -> n235
node n236 s12 word last
node n237 s5 -> n142
node n238 s4 word -> n140
node n239 s15 last -> n240
node n240 s14 word last
node n241 s5 -> n142
node n242 s4 word -> n140
node n243 s17 last -> n244
node n244 s16 word last
node n245 s15 last -> n240
node n246 s17 last -> n244
node n247 s20 word last
node n248 s22 word last
node n249 s8 word -> n299
node n250 s25 word -> n265
node n251 s26 word -> n281
node n252 s6 -> n126
node n253 s1 word -> n207
node n254 s14 word -> n237
node n255 s16 word -> n241
node n256 s9 -> n149
node n257 s11 -> n216
node n258 s13 word -> n236
node n259 s21 -> n247
node n260 s23 -> n248
node n261 s18 -> n245
node n262 s19 -> n246
node n263 s27 word -> n249
node n264 s24 word last
node n265 s8 word -> n299
node n266 s25 word -> n265
node n267 s26 word -> n281
node n268 s6 -> n126
node n269 s1 word -> n207
node n270 s14 word -> n237
node n271 s16 word -> n241
node n272 s9 -> n149
node n273 s11 -> n216
node n274 s13 word -> n236
node n275 s32 -> n297
node n276 s33 -> n298
node n277 s18 -> n245
node n278 s19 -> n246
node n279 s27 word -> n249
node n280 s24 word last
node n281 s8 word -> n299
node n282 s25 word -> n265
node n283 s26 word -> n281
node n284 s6 -> n126
node n285 s1 word -> n207
node n286 s14 word -> n237
node n287 s16 word -> n241
node n288 s9 -> n149
node n289 s11 -> n216
node n290 s13 word -> n236
node n291 s18 -> n245
node n292 s19 -> n246
node n293 s27 word -> n249
node n294 s29 -> n297
node n295 s31 -> n298
node n296 s24 word last
node n297 s28 word last
node n298 s30 word last
node n299 s34 word -> n249
node n300 s8 word -> n313
node n301 s6 -> n126
node n302 s1 word -> n207
node n303 s14 word -> n237
node n304 s16 word -> n241
node n305 s9 -> n149
node n306 s11 -> n216
node n307 s13 word -> n236
node n308 s35 -> n327
node n309 s36 -> n328
node n310 s18 -> n245
node n311 s19 -> n246
node n312 s24 word last
node n313 s34 word -> n249
node n314 s8 word -> n313
node n315 s6 -> n126
node n316 s1 word -> n207
node n317 s14 word -> n237
node n318 s16 word -> n241
node n319 s9 -> n149
node n320 s11 -> n216
node n321 s13 word -> n236
node n322 s21 -> n247
node n323 s23 -> n248
node n324 s18 -> n245
node n325 s19 -> n246
node n326 s24 word last
node n327 s21 word last
node n328 s23 word last
node n329 s8 word -> n299
node n330 s25 word -> n265
node n331 s26 word -> n281
node n332 s6 -> n126
node n333 s3 -> n359
node n334 s1 word -> n207
node n335 s14 word -> n237
node n336 s16 word -> n241
node n337 s9 -> n149
node n338 s11 -> n216
node n339 s13 word -> n236
node n340 s21 -> n247
node n341 s23 -> n248
node n342 s18 -> n245
node n343 s19 -> n246
node n344 s27 word -> n249
node n345 s24 word last
node n346 s6 -> n126
node n347 s3 -> n359
node n348 s1 word -> n207
node n349 s14 word -> n237
node n350 s16 word -> n241
node n351 s9 -> n149
node n352 s11 -> n216
node n353 s13 word -> n236
node n354 s18 -> n245
node n355 s19 -> n246
node n356 s24 word last
node n357 s0 word last -> n346
node n358 s0 last -> n357
node n359 s0 last -> n358
node n360 s34 word -> n329
node n361 s8 word -> n375
node n362 s6 -> n126
node n363 s3 -> n359
node n364 s1 word -> n207
node n365 s14 word -> n237
node n366 s16 word -> n241
node n367 s9 -> n149
node n368 s11 -> n216
node n369 s13 word -> n236
node n370 s21 -> n247
node n371 s23 -> n248
node n372 s18 -> n245
node n373 s19 -> n246
node n374 s24 word last
node n375 s34 word -> n249
node n376 s8 word -> n313
node n377 s6 -> n126
node n378 s3 -> n359
node n379 s1 word -> n207
node n380 s14 word -> n237
node n381 s16 word -> n241
node n382 s9 -> n149
node n383 s11 -> n216
node n384 s13 word -> n236
node n385 s21 -> n247
node n386 s23 -> n248
node n387 s18 -> n245
node n388 s19 -> n246
node n389 s24 word last
node n390 s8 word -> n360
node n391 s6 -> n126
node n392 s3 -> n359
node n393 s1 word -> n207
node n394 s14 word -> n407
node n395 s16 word -> n411
node n396 s9 -> n149
node n397 s11 -> n216
node n398 s13 word -> n236
node n399 s35 -> n327
node n400 s36 -> n328
node n401 s29 -> n240
node n402 s31 -> n244
node n403 s18 -> n245
node n404 s19 -> n246
node n405 s34 word -> n430
node n406 s24 word last
node n407 s5 -> n142
node n408 s4 word -> n140
node n409 s15 -> n240
node n410 s29 word last
node n411 s5 -> n142
node n412 s4 word -> n140
node n413 s17 -> n244
node n414 s31 word last
node n415 s34 word -> n249
node n416 s8 word -> n313
node n417 s6 -> n126
node n418 s3 -> n359
node n419 s1 word -> n207
node n420 s14 word -> n237
node n421 s16 word -> n241
node n422 s9 -> n149
node n423 s11 -> n216
node n424 s13 word -> n236
node n425 s35 -> n327
node n426 s36 -> n328
node n427 s18 -> n245
node n428 s19 -> n246
node n429 s24 word last
node n430 s8 word -> n415
node n431 s25 word -> n447
node n432 s26 word -> n464
node n433 s6 -> n126
node n434 s3 -> n359
node n435 s1 word -> n207
node n436 s14 word -> n237
node n437 s16 word -> n241
node n438 s9 -> n149
node n439 s11 -> n216
node n440 s13 word -> n236
node n441 s18 -> n245
node n442 s19 -> n246
node n443 s21 -> n247
node n444 s23 -> n248
node n445 s27 word -> n329
node n446 s24 word last
node n447 s8 word -> n299
node n448 s25 word -> n265
node n449 s26 word -> n281
node n450 s6 -> n126
node n451 s3 -> n359
node n452 s1 word -> n207
node n453 s14 word -> n237
node n454 s16 word -> n241
node n455 s9 -> n149
node n456 s11 -> n216
node n457 s13 word -> n236
node n458 s32 -> n297
node n459 s33 -> n298
node n460 s18 -> n245
node n461 s19 -> n246
node n462 s27 word -> n249
node n463 s24 word last
node n464 s8 word -> n299
node n465 s25 word -> n265
node n466 s26 word -> n281
node n467 s6 -> n126
node n468 s3 -> n359
node n469 s1 word -> n207
node n470 s14 word -> n237
node n471 s16 word -> n241
node n472 s9 -> n149
node n473 s11 -> n216
node n474 s13 word -> n236
node n475 s18 -> n245
node n476 s19 -> n246
node n477 s27 word -> n249
node n478 s29 -> n297
node n479 s31 -> n298
node n480 s24 word last
node n481 s27 word -> n430
node n482 s8 word -> n498
node n483 s25 word -> n513
node n484 s26 word -> n530
node n485 s6 -> n126
node n486 s3 -> n359
node n487 s1 word -> n207
node n488 s14 word -> n237
node n489 s16 word -> n241
node n490 s9 -> n149
node n491 s11 -> n216
node n492 s13 word -> n236
node n493 s32 -> n297
node n494 s33 -> n298
node n495 s18 -> n245
node n496 s19 -> n246
node n497 s24 word last
node n498 s34 word -> n329
node n499 s8 word -> n375
node n500 s6 -> n126
node n501 s3 -> n359
node n502 s1 word -> n207
node n503 s14 word -> n237
node n504 s16 word -> n241
node n505 s9 -> n149
node n506 s11 -> n216
node n507 s13 word -> n236
node n508 s35 -> n327
node n509 s36 -> n328
node n510 s18 -> n245
node n511 s19 -> n246
node n512 s24 word last
node n513 s8 word -> n415
node n514 s25 word -> n447
node n515 s26 word -> n464
node n516 s6 -> n126
node n517 s3 -> n359
node n518 s1 word -> n207
node n519 s14 word -> n237
node n520 s16 word -> n241
node n521 s9 -> n149
node n522 s11 -> n216
node n523 s13 word -> n236
node n524 s32 -> n297
node n525 s33 -> n298
node n526 s18 -> n245
node n527 s19 -> n246
node n528 s27 word -> n329
node n529 s24 word last
node n530 s8 word -> n415
node n531 s25 word -> n447
node n532 s26 word -> n464
node n533 s6 -> n126
node n534 s3 -> n359
node n535 s1 word -> n207
node n536 s14 word -> n237
node n537 s16 word -> n241
node n538 s9 -> n149
node n539 s11 -> n216
node n540 s13 word -> n236
node n541 s18 -> n245
node n542 s19 -> n246
node n543 s29 -> n297
node n544 s31 -> n298
node n545 s27 word -> n329
node n546 s24 word last
node n547 s27 word -> n430
node n548 s8 word -> n498
node n549 s25 word -> n513
node n550 s26 word -> n530
node n551 s6 -> n126
node n552 s3 -> n359
node n553 s1 word -> n207
node n554 s14 word -> n237
node n555 s16 word -> n241
node n556 s9 -> n149
node n557 s11 -> n216
node n558 s13 word -> n236
node n559 s18 -> n245
node n560 s19 -> n246
node n561 s29 -> n297
node n562 s31 -> n298
node n563 s24 word last
node n564 s27 word -> n430
node n565 s8 word -> n498
node n566 s25 word -> n513
node n567 s26 word -> n530
node n568 s6 -> n126
node n569 s3 -> n359
node n570 s1 word -> n207
node n571 s14 word -> n237
node n572 s16 word -> n241
node n573 s9 -> n149
node n574 s11 -> n216
node n575 s13 word -> n236
node n576 s18 -> n245
node n577 s19 -> n246
node n578 s21 -> n247
node n579 s23 -> n248
node n580 s24 word last
node n581 s6 -> n126
node n582 s0 word -> n581
node n583 s7 -> n143
node n584 s9 -> n149
node n585 s11 -> n216
node n586 s13 word -> n236
node n587 s10 word last
node n588 s0 word last -> n581
node n589 s0 -> n589
node n590 s38 word last
node n591 s1 -> n589
node n592 s38 word last
node n593 s2 -> n591
node n594 s4 -> n613
node n595 s1 last -> n619
node n596 s0 -> n596
node n597 s1 -> n589
node n598 s38 word last
node n599 s0 -> n596
node n600 s3 -> n608
node n601 s1 -> n589
node n602 s38 word last
node n603 s3 -> n608
node n604 s1 -> n589
node n605 s38 word last
node n606 s0 last -> n603
node n607 s0 last -> n606
node n608 s0 last -> n607
node n609 s0 -> n599
node n610 s3 -> n608
node n611 s1 -> n589
node n612 s38 word last
node n613 s0 -> n609
node n614 s3 -> n608
node n615 s1 -> n589
node n616 s38 word last
node n617 s0 -> n617
node n618 s38 word last
node n619 s0 last -> n617
