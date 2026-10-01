# gLex8phone: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 "0" "t0"
set s1 "1" "t1"
set s2 "23456789" "t2"
set s3 "#*0123456789" "t3"
set s4 "," "t4"
set s5 "0123456789" "t5"
set s6 "#*" "t6"
set s7 "-/" "t7"
set s8 "-" "t8"
set s9 "123456789" "t9"
set s10 ")" "t10"
set s11 "(" "t11"
set s12 "/" "t12"
set s13 "." "t13"
set s14 "-." "t14"
set s15 "Xx" "t15"
set s16 "t" "t16"
set s17 "x" "t17"
set s18 "e" "t18"
set s19 "T" "t19"
set s20 "X" "t20"
set s21 "E" "t21"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s6 word -> n305
node n1 s0 word -> n407
node n2 s1 word -> n563
node n3 s2 word -> n631
node n4 s11 -> n642
node n5 s15 word -> n650
node n6 s18 -> n657
node n7 s21 last -> n662
node n8 s0 word -> n8
node n9 s1 word -> n188
node n10 s2 word -> n22
node n11 s7 -> n124
node n12 s4 -> n175
node n13 s11 -> n93
node n14 s6 word last -> n64
node n15 s0 word -> n8
node n16 s1 word -> n188
node n17 s2 word -> n22
node n18 s7 -> n124
node n19 s4 -> n149
node n20 s11 -> n93
node n21 s6 word last -> n64
node n22 s0 word -> n181
node n23 s1 word -> n168
node n24 s2 word -> n43
node n25 s7 -> n124
node n26 s4 -> n155
node n27 s11 -> n93
node n28 s6 word last -> n64
node n29 s0 word -> n15
node n30 s1 word -> n36
node n31 s2 word -> n161
node n32 s7 -> n124
node n33 s4 -> n149
node n34 s11 -> n93
node n35 s6 word last -> n64
node n36 s0 word -> n181
node n37 s1 word -> n168
node n38 s2 word -> n43
node n39 s7 -> n124
node n40 s4 -> n149
node n41 s11 -> n93
node n42 s6 word last -> n64
node n43 s0 word -> n29
node n44 s1 word -> n50
node n45 s2 word -> n57
node n46 s7 -> n124
node n47 s4 -> n155
node n48 s11 -> n93
node n49 s6 word last -> n64
node n50 s0 word -> n29
node n51 s1 word -> n50
node n52 s2 word -> n57
node n53 s7 -> n124
node n54 s4 -> n149
node n55 s11 -> n93
node n56 s6 word last -> n64
node n57 s0 word -> n29
node n58 s1 word -> n50
node n59 s2 word -> n57
node n60 s7 -> n124
node n61 s4 -> n143
node n62 s11 -> n93
node n63 s6 word last -> n64
node n64 s3 word -> n64
node n65 s4 -> n69
node n66 s7 last -> n67
node n67 s5 word -> n72
node n68 s6 word last -> n64
node n69 s6 word -> n64
node n70 s4 -> n69
node n71 s5 word last -> n72
node n72 s4 -> n69
node n73 s6 word -> n64
node n74 s5 word -> n72
node n75 s7 last -> n67
node n76 s5 word last -> n72
node n77 s5 last -> n76
node n78 s5 last -> n77
node n79 s5 last -> n78
node n80 s8 -> n79
node n81 s5 last -> n78
node n82 s5 last -> n80
node n83 s5 last -> n82
node n84 s9 last -> n83
node n85 s7 -> n84
node n86 s4 -> n88
node n87 s9 last -> n83
node n88 s9 -> n83
node n89 s4 last -> n88
node n90 s10 last -> n85
node n91 s5 last -> n90
node n92 s5 last -> n91
node n93 s9 last -> n92
node n94 s11 -> n93
node n95 s6 word -> n64
node n96 s0 word -> n72
node n97 s9 word last -> n109
node n98 s7 -> n67
node n99 s4 -> n102
node n100 s5 word -> n72
node n101 s6 word last -> n64
node n102 s4 -> n102
node n103 s6 word -> n64
node n104 s5 word last -> n72
node n105 s5 word -> n98
node n106 s4 -> n69
node n107 s6 word -> n64
node n108 s7 last -> n67
node n109 s5 word -> n105
node n110 s4 -> n69
node n111 s6 word -> n64
node n112 s7 last -> n67
node n113 s7 -> n94
node n114 s4 -> n119
node n115 s11 -> n93
node n116 s6 word -> n64
node n117 s0 word -> n72
node n118 s9 word last -> n109
node n119 s11 -> n93
node n120 s4 -> n119
node n121 s6 word -> n64
node n122 s0 word -> n72
node n123 s9 word last -> n109
node n124 s0 word -> n113
node n125 s1 word -> n129
node n126 s2 word -> n109
node n127 s11 -> n93
node n128 s6 word last -> n64
node n129 s7 -> n94
node n130 s4 -> n119
node n131 s9 word -> n139
node n132 s11 -> n93
node n133 s0 word -> n105
node n134 s6 word last -> n64
node n135 s7 -> n67
node n136 s4 -> n102
node n137 s6 word -> n64
node n138 s5 word last -> n98
node n139 s5 word -> n135
node n140 s4 -> n69
node n141 s6 word -> n64
node n142 s7 last -> n67
node n143 s0 word -> n113
node n144 s1 word -> n129
node n145 s2 word -> n109
node n146 s11 -> n93
node n147 s4 -> n143
node n148 s6 word last -> n64
node n149 s0 word -> n113
node n150 s1 word -> n129
node n151 s2 word -> n109
node n152 s11 -> n93
node n153 s4 -> n149
node n154 s6 word last -> n64
node n155 s0 word -> n113
node n156 s1 word -> n129
node n157 s2 word -> n109
node n158 s11 -> n93
node n159 s4 -> n155
node n160 s6 word last -> n64
node n161 s0 word -> n181
node n162 s1 word -> n168
node n163 s2 word -> n43
node n164 s7 -> n124
node n165 s4 -> n143
node n166 s11 -> n93
node n167 s6 word last -> n64
node n168 s0 word -> n29
node n169 s1 word -> n50
node n170 s2 word -> n57
node n171 s7 -> n124
node n172 s4 -> n175
node n173 s11 -> n93
node n174 s6 word last -> n64
node n175 s0 word -> n113
node n176 s1 word -> n129
node n177 s2 word -> n109
node n178 s11 -> n93
node n179 s4 -> n175
node n180 s6 word last -> n64
node n181 s0 word -> n15
node n182 s1 word -> n36
node n183 s2 word -> n161
node n184 s7 -> n124
node n185 s4 -> n175
node n186 s11 -> n93
node n187 s6 word last -> n64
node n188 s0 word -> n181
node n189 s1 word -> n168
node n190 s2 word -> n43
node n191 s7 -> n124
node n192 s4 -> n175
node n193 s11 -> n93
node n194 s6 word last -> n64
node n195 s0 word -> n8
node n196 s1 word -> n188
node n197 s2 word -> n22
node n198 s7 -> n215
node n199 s4 -> n241
node n200 s11 last -> n93
node n201 s5 last -> n85
node n202 s5 last -> n201
node n203 s9 -> n202
node n204 s11 last -> n93
node n205 s7 -> n203
node n206 s4 -> n210
node n207 s9 -> n214
node n208 s11 -> n93
node n209 s0 last -> n77
node n210 s9 -> n202
node n211 s11 -> n93
node n212 s4 last -> n210
node n213 s5 word last -> n98
node n214 s5 last -> n213
node n215 s0 -> n205
node n216 s1 -> n219
node n217 s2 -> n240
node n218 s11 last -> n93
node n219 s7 -> n203
node n220 s4 -> n210
node n221 s9 -> n234
node n222 s11 -> n93
node n223 s0 last -> n239
node n224 s8 -> n79
node n225 s5 word last -> n72
node n226 s5 last -> n224
node n227 s5 last -> n226
node n228 s9 -> n227
node n229 s0 last -> n78
node n230 s8 -> n228
node n231 s12 -> n84
node n232 s4 -> n88
node n233 s5 word last -> n98
node n234 s5 last -> n230
node n235 s8 -> n228
node n236 s12 -> n84
node n237 s4 -> n88
node n238 s5 word last -> n72
node n239 s5 last -> n235
node n240 s5 last -> n239
node n241 s0 -> n205
node n242 s1 -> n219
node n243 s2 -> n240
node n244 s11 -> n93
node n245 s4 last -> n241
node n246 s0 word -> n195
node n247 s1 word -> n252
node n248 s2 word -> n258
node n249 s7 -> n215
node n250 s4 -> n241
node n251 s11 last -> n93
node n252 s0 word -> n181
node n253 s1 word -> n168
node n254 s2 word -> n43
node n255 s7 -> n215
node n256 s4 -> n241
node n257 s11 last -> n93
node n258 s0 word -> n181
node n259 s1 word -> n168
node n260 s2 word -> n43
node n261 s7 -> n215
node n262 s4 -> n264
node n263 s11 last -> n93
node n264 s0 -> n205
node n265 s1 -> n219
node n266 s2 -> n240
node n267 s11 -> n93
node n268 s4 last -> n264
node n269 s0 word -> n246
node n270 s1 word -> n281
node n271 s2 word -> n299
node n272 s7 -> n215
node n273 s4 -> n241
node n274 s11 last -> n93
node n275 s0 word -> n15
node n276 s1 word -> n36
node n277 s2 word -> n161
node n278 s7 -> n215
node n279 s4 -> n241
node n280 s11 last -> n93
node n281 s0 word -> n275
node n282 s1 word -> n287
node n283 s2 word -> n293
node n284 s7 -> n215
node n285 s4 -> n241
node n286 s11 last -> n93
node n287 s0 word -> n29
node n288 s1 word -> n50
node n289 s2 word -> n57
node n290 s7 -> n215
node n291 s4 -> n241
node n292 s11 last -> n93
node n293 s0 word -> n29
node n294 s1 word -> n50
node n295 s2 word -> n57
node n296 s7 -> n215
node n297 s4 -> n264
node n298 s11 last -> n93
node n299 s0 word -> n275
node n300 s1 word -> n287
node n301 s2 word -> n293
node n302 s7 -> n215
node n303 s4 -> n264
node n304 s11 last -> n93
node n305 s0 word -> n269
node n306 s1 word -> n346
node n307 s2 word -> n382
node n308 s7 -> n215
node n309 s4 -> n264
node n310 s11 -> n93
node n311 s6 word last -> n305
node n312 s0 word -> n8
node n313 s1 word -> n188
node n314 s2 word -> n22
node n315 s7 -> n215
node n316 s4 -> n318
node n317 s11 last -> n93
node n318 s0 -> n205
node n319 s1 -> n219
node n320 s2 -> n240
node n321 s11 -> n93
node n322 s4 last -> n318
node n323 s0 word -> n312
node n324 s1 word -> n329
node n325 s2 word -> n335
node n326 s7 -> n215
node n327 s4 -> n241
node n328 s11 last -> n93
node n329 s0 word -> n181
node n330 s1 word -> n168
node n331 s2 word -> n43
node n332 s7 -> n215
node n333 s4 -> n318
node n334 s11 last -> n93
node n335 s0 word -> n181
node n336 s1 word -> n168
node n337 s2 word -> n43
node n338 s7 -> n215
node n339 s4 -> n341
node n340 s11 last -> n93
node n341 s0 -> n205
node n342 s1 -> n219
node n343 s2 -> n240
node n344 s11 -> n93
node n345 s4 last -> n341
node n346 s0 word -> n323
node n347 s1 word -> n358
node n348 s2 word -> n376
node n349 s7 -> n215
node n350 s4 -> n241
node n351 s11 last -> n93
node n352 s0 word -> n15
node n353 s1 word -> n36
node n354 s2 word -> n161
node n355 s7 -> n215
node n356 s4 -> n318
node n357 s11 last -> n93
node n358 s0 word -> n352
node n359 s1 word -> n364
node n360 s2 word -> n370
node n361 s7 -> n215
node n362 s4 -> n241
node n363 s11 last -> n93
node n364 s0 word -> n29
node n365 s1 word -> n50
node n366 s2 word -> n57
node n367 s7 -> n215
node n368 s4 -> n318
node n369 s11 last -> n93
node n370 s0 word -> n29
node n371 s1 word -> n50
node n372 s2 word -> n57
node n373 s7 -> n215
node n374 s4 -> n341
node n375 s11 last -> n93
node n376 s0 word -> n352
node n377 s1 word -> n364
node n378 s2 word -> n370
node n379 s7 -> n215
node n380 s4 -> n264
node n381 s11 last -> n93
node n382 s0 word -> n323
node n383 s1 word -> n358
node n384 s2 word -> n376
node n385 s7 -> n215
node n386 s4 -> n264
node n387 s11 last -> n93
node n388 s7 -> n203
node n389 s4 -> n210
node n390 s9 word -> n393
node n391 s11 -> n93
node n392 s0 word last -> n394
node n393 s5 word last -> n213
node n394 s5 word last -> n76
node n395 s0 word -> n388
node n396 s1 word -> n399
node n397 s2 word -> n406
node n398 s11 last -> n93
node n399 s7 -> n203
node n400 s4 -> n210
node n401 s9 word -> n404
node n402 s11 -> n93
node n403 s0 word last -> n405
node n404 s5 word last -> n230
node n405 s5 word last -> n235
node n406 s5 word last -> n405
node n407 s8 -> n395
node n408 s12 -> n215
node n409 s4 -> n241
node n410 s0 word -> n415
node n411 s1 word -> n529
node n412 s2 word -> n555
node n413 s11 -> n93
node n414 s13 last -> n528
node n415 s8 -> n395
node n416 s12 -> n215
node n417 s4 -> n241
node n418 s1 word -> n423
node n419 s2 word -> n513
node n420 s11 -> n93
node n421 s0 word -> n519
node n422 s13 last -> n528
node n423 s7 -> n215
node n424 s4 -> n241
node n425 s11 -> n93
node n426 s0 word -> n429
node n427 s1 word -> n457
node n428 s2 word last -> n478
node n429 s7 -> n124
node n430 s4 -> n175
node n431 s6 word -> n64
node n432 s11 -> n93
node n433 s0 word -> n436
node n434 s1 word -> n471
node n435 s2 word last -> n499
node n436 s7 -> n124
node n437 s4 -> n149
node n438 s11 -> n93
node n439 s6 word -> n64
node n440 s0 word -> n443
node n441 s1 word -> n450
node n442 s2 word last -> n506
node n443 s7 -> n124
node n444 s4 -> n175
node n445 s11 -> n93
node n446 s6 word -> n64
node n447 s0 word -> n443
node n448 s1 word -> n450
node n449 s2 word last -> n506
node n450 s0 word -> n429
node n451 s1 word -> n457
node n452 s2 word -> n478
node n453 s7 -> n124
node n454 s4 -> n175
node n455 s11 -> n93
node n456 s6 word last -> n64
node n457 s7 -> n124
node n458 s4 -> n175
node n459 s6 word -> n64
node n460 s11 -> n93
node n461 s0 word -> n464
node n462 s1 word -> n485
node n463 s2 word last -> n492
node n464 s7 -> n124
node n465 s4 -> n149
node n466 s11 -> n93
node n467 s6 word -> n64
node n468 s0 word -> n436
node n469 s1 word -> n471
node n470 s2 word last -> n499
node n471 s7 -> n124
node n472 s4 -> n149
node n473 s1 word -> n457
node n474 s2 word -> n478
node n475 s0 word -> n429
node n476 s11 -> n93
node n477 s6 word last -> n64
node n478 s7 -> n124
node n479 s4 -> n155
node n480 s6 word -> n64
node n481 s11 -> n93
node n482 s0 word -> n464
node n483 s1 word -> n485
node n484 s2 word last -> n492
node n485 s7 -> n124
node n486 s4 -> n149
node n487 s11 -> n93
node n488 s6 word -> n64
node n489 s0 word -> n464
node n490 s1 word -> n485
node n491 s2 word last -> n492
node n492 s7 -> n124
node n493 s4 -> n143
node n494 s11 -> n93
node n495 s6 word -> n64
node n496 s0 word -> n464
node n497 s1 word -> n485
node n498 s2 word last -> n492
node n499 s7 -> n124
node n500 s4 -> n143
node n501 s1 word -> n457
node n502 s2 word -> n478
node n503 s0 word -> n429
node n504 s11 -> n93
node n505 s6 word last -> n64
node n506 s0 word -> n429
node n507 s1 word -> n457
node n508 s2 word -> n478
node n509 s7 -> n124
node n510 s4 -> n155
node n511 s11 -> n93
node n512 s6 word last -> n64
node n513 s7 -> n215
node n514 s4 -> n264
node n515 s11 -> n93
node n516 s0 word -> n429
node n517 s1 word -> n457
node n518 s2 word last -> n478
node n519 s0 word -> n443
node n520 s1 word -> n450
node n521 s2 word -> n506
node n522 s7 -> n215
node n523 s4 -> n241
node n524 s11 last -> n93
node n525 s5 word last
node n526 s5 word last -> n525
node n527 s5 word last -> n526
node n528 s5 word last -> n527
node n529 s8 -> n395
node n530 s12 -> n215
node n531 s4 -> n241
node n532 s1 word -> n537
node n533 s2 word -> n543
node n534 s11 -> n93
node n535 s0 word -> n549
node n536 s13 last -> n528
node n537 s0 word -> n464
node n538 s1 word -> n485
node n539 s2 word -> n492
node n540 s7 -> n215
node n541 s4 -> n241
node n542 s11 last -> n93
node n543 s0 word -> n464
node n544 s1 word -> n485
node n545 s2 word -> n492
node n546 s7 -> n215
node n547 s4 -> n264
node n548 s11 last -> n93
node n549 s0 word -> n436
node n550 s1 word -> n471
node n551 s2 word -> n499
node n552 s7 -> n215
node n553 s4 -> n241
node n554 s11 last -> n93
node n555 s0 word -> n549
node n556 s1 word -> n537
node n557 s2 word -> n543
node n558 s8 -> n395
node n559 s12 -> n215
node n560 s4 -> n264
node n561 s11 -> n93
node n562 s13 last -> n528
node n563 s8 -> n395
node n564 s12 -> n215
node n565 s4 -> n241
node n566 s0 word -> n571
node n567 s1 word -> n597
node n568 s2 word -> n623
node n569 s11 -> n93
node n570 s13 last -> n528
node n571 s8 -> n395
node n572 s12 -> n215
node n573 s4 -> n241
node n574 s1 word -> n579
node n575 s2 word -> n585
node n576 s11 -> n93
node n577 s0 word -> n591
node n578 s13 last -> n528
node n579 s7 -> n215
node n580 s4 -> n318
node n581 s11 -> n93
node n582 s0 word -> n429
node n583 s1 word -> n457
node n584 s2 word last -> n478
node n585 s7 -> n215
node n586 s4 -> n341
node n587 s11 -> n93
node n588 s0 word -> n429
node n589 s1 word -> n457
node n590 s2 word last -> n478
node n591 s0 word -> n443
node n592 s1 word -> n450
node n593 s2 word -> n506
node n594 s7 -> n215
node n595 s4 -> n318
node n596 s11 last -> n93
node n597 s8 -> n395
node n598 s12 -> n215
node n599 s4 -> n241
node n600 s1 word -> n605
node n601 s2 word -> n611
node n602 s11 -> n93
node n603 s0 word -> n617
node n604 s13 last -> n528
node n605 s0 word -> n464
node n606 s1 word -> n485
node n607 s2 word -> n492
node n608 s7 -> n215
node n609 s4 -> n318
node n610 s11 last -> n93
node n611 s0 word -> n464
node n612 s1 word -> n485
node n613 s2 word -> n492
node n614 s7 -> n215
node n615 s4 -> n341
node n616 s11 last -> n93
node n617 s0 word -> n436
node n618 s1 word -> n471
node n619 s2 word -> n499
node n620 s7 -> n215
node n621 s4 -> n318
node n622 s11 last -> n93
node n623 s0 word -> n617
node n624 s1 word -> n605
node n625 s2 word -> n611
node n626 s8 -> n395
node n627 s12 -> n215
node n628 s4 -> n264
node n629 s11 -> n93
node n630 s13 last -> n528
node n631 s8 -> n395
node n632 s12 -> n215
node n633 s4 -> n264
node n634 s0 word -> n571
node n635 s1 word -> n597
node n636 s2 word -> n623
node n637 s11 -> n93
node n638 s13 last -> n528
node n639 s10 word last -> n85
node n640 s5 last -> n639
node n641 s5 last -> n640
node n642 s9 last -> n641
node n643 s14 -> n528
node n644 s5 word last -> n527
node n645 s5 word -> n643
node n646 s14 last -> n528
node n647 s5 word last -> n645
node n648 s8 word -> n647
node n649 s5 word last -> n645
node n650 s13 word -> n648
node n651 s8 word -> n647
node n652 s5 word last -> n645
node n653 s13 word -> n648
node n654 s8 word -> n647
node n655 s16 word -> n650
node n656 s5 word last -> n645
node n657 s17 word last -> n653
node n658 s13 word -> n648
node n659 s8 word -> n647
node n660 s19 word -> n650
node n661 s5 word last -> n645
node n662 s20 word -> n658
node n663 s17 word last -> n653
