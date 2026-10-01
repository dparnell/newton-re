# gLex8moneyCFr: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 "0123456789" "t0"
set s1 "BKM" "t1"
set s2 "-" "t2"
set s3 "," "t3"
set s4 "0" "t4"
set s5 "123456789" "t5"
set s6 "'." "t6"
set s7 "£¥" "t7"
set s8 "F" "t8"
set s9 "." "t9"
set s10 "r" "t10"
set s11 "s" "t11"
set s12 "M" "t12"
set s13 "D" "t13"
set s14 "$" "t14"
set s15 "U" "t15"
set s16 "A" "t16"
set s17 "S" "t17"
set s18 "N" "t18"
set s19 "C" "t19"
set s20 "c¢" "t20"
set s21 "+" "t21"
set s22 ")" "t22"
set s23 "(" "t23"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s7 word -> n60
node n1 s8 word -> n126
node n2 s11 -> n136
node n3 s13 -> n137
node n4 s16 -> n139
node n5 s15 -> n140
node n6 s19 -> n142
node n7 s14 word -> n209
node n8 s4 word -> n351
node n9 s5 word -> n466
node n10 s3 word -> n503
node n11 s21 word -> n674
node n12 s2 word -> n1153
node n13 s23 -> n1663
node n14 s20 word last
node n15 s1 word
node n16 s0 last -> n15
node n17 s0 word -> n15
node n18 s1 word last
node n19 s0 -> n17
node n20 s2 word -> n22
node n21 s1 word last
node n22 s2 word last
node n23 s3 word -> n19
node n24 s4 -> n29
node n25 s5 -> n55
node n26 s1 word last
node n27 s0 -> n27
node n28 s1 word last
node n29 s3 -> n27
node n30 s4 -> n29
node n31 s5 -> n55
node n32 s1 word last
node n33 s4 -> n33
node n34 s5 -> n55
node n35 s3 -> n27
node n36 s1 word last
node n37 s4 -> n33
node n38 s5 -> n55
node n39 s3 -> n27
node n40 s6 -> n49
node n41 s1 word last
node n42 s3 -> n27
node n43 s4 -> n29
node n44 s5 -> n55
node n45 s6 -> n49
node n46 s1 word last
node n47 s0 last -> n42
node n48 s0 last -> n47
node n49 s0 last -> n48
node n50 s4 -> n37
node n51 s5 -> n55
node n52 s3 -> n27
node n53 s6 -> n49
node n54 s1 word last
node n55 s4 -> n50
node n56 s5 -> n55
node n57 s3 -> n27
node n58 s6 -> n49
node n59 s1 word last
node n60 s4 word -> n23
node n61 s5 word -> n105
node n62 s3 word last -> n124
node n63 s4 word -> n63
node n64 s5 word -> n77
node n65 s3 word -> n19
node n66 s1 word last
node n67 s4 word -> n63
node n68 s5 word -> n77
node n69 s3 word -> n19
node n70 s6 -> n49
node n71 s1 word last
node n72 s4 word -> n67
node n73 s5 word -> n77
node n74 s3 word -> n19
node n75 s6 -> n49
node n76 s1 word last
node n77 s4 word -> n72
node n78 s5 word -> n77
node n79 s3 word -> n19
node n80 s6 -> n49
node n81 s1 word last
node n82 s4 word -> n63
node n83 s5 word -> n77
node n84 s3 word -> n19
node n85 s6 -> n94
node n86 s1 word last
node n87 s3 word -> n19
node n88 s6 -> n94
node n89 s4 -> n29
node n90 s5 -> n55
node n91 s1 word last
node n92 s0 word last -> n87
node n93 s0 last -> n92
node n94 s0 last -> n93
node n95 s4 word -> n82
node n96 s5 word -> n100
node n97 s3 word -> n19
node n98 s6 -> n94
node n99 s1 word last
node n100 s4 word -> n72
node n101 s5 word -> n77
node n102 s3 word -> n19
node n103 s6 -> n94
node n104 s1 word last
node n105 s4 word -> n95
node n106 s5 word -> n115
node n107 s3 word -> n19
node n108 s6 -> n94
node n109 s1 word last
node n110 s4 word -> n67
node n111 s5 word -> n77
node n112 s3 word -> n19
node n113 s6 -> n94
node n114 s1 word last
node n115 s4 word -> n110
node n116 s5 word -> n100
node n117 s3 word -> n19
node n118 s6 -> n94
node n119 s1 word last
node n120 s0 -> n120
node n121 s1 word last
node n122 s0 word -> n120
node n123 s1 word last
node n124 s0 -> n122
node n125 s2 word last -> n22
node n126 s8 word -> n60
node n127 s10 word -> n131
node n128 s4 word -> n23
node n129 s5 word -> n105
node n130 s3 word last -> n124
node n131 s9 word -> n60
node n132 s4 word -> n23
node n133 s5 word -> n105
node n134 s3 word last -> n124
node n135 s10 word last -> n131
node n136 s8 last -> n135
node n137 s12 word last -> n60
node n138 s14 word last -> n60
node n139 s15 last -> n138
node n140 s17 last -> n138
node n141 s18 last -> n138
node n142 s13 last -> n141
node n143 s18 word last
node n144 s13 last -> n143
node n145 s19 -> n144
node n146 s0 -> n150
node n147 s15 -> n152
node n148 s16 -> n156
node n149 s1 word last -> n153
node n150 s0 -> n150
node n151 s1 word last -> n153
node n152 s17 word last
node n153 s15 -> n152
node n154 s16 -> n156
node n155 s19 last -> n144
node n156 s15 word last
node n157 s0 word -> n145
node n158 s1 word last -> n153
node n159 s0 -> n157
node n160 s19 -> n144
node n161 s15 -> n152
node n162 s16 -> n156
node n163 s1 word -> n153
node n164 s2 word last -> n165
node n165 s15 -> n152
node n166 s16 -> n156
node n167 s2 word -> n153
node n168 s19 last -> n144
node n169 s3 word -> n159
node n170 s19 -> n144
node n171 s4 -> n178
node n172 s5 -> n204
node n173 s15 -> n152
node n174 s16 -> n156
node n175 s1 word last -> n153
node n176 s0 -> n176
node n177 s1 word last -> n153
node n178 s3 -> n176
node n179 s4 -> n178
node n180 s5 -> n204
node n181 s1 word last -> n153
node n182 s4 -> n182
node n183 s5 -> n204
node n184 s3 -> n176
node n185 s1 word last -> n153
node n186 s4 -> n182
node n187 s5 -> n204
node n188 s3 -> n176
node n189 s6 -> n198
node n190 s1 word last -> n153
node n191 s3 -> n176
node n192 s4 -> n178
node n193 s5 -> n204
node n194 s6 -> n198
node n195 s1 word last -> n153
node n196 s0 last -> n191
node n197 s0 last -> n196
node n198 s0 last -> n197
node n199 s4 -> n186
node n200 s5 -> n204
node n201 s3 -> n176
node n202 s6 -> n198
node n203 s1 word last -> n153
node n204 s4 -> n199
node n205 s5 -> n204
node n206 s3 -> n176
node n207 s6 -> n198
node n208 s1 word last -> n153
node n209 s4 word -> n169
node n210 s5 word -> n281
node n211 s3 word -> n314
node n212 s19 -> n144
node n213 s15 -> n152
node n214 s16 last -> n156
node n215 s4 word -> n215
node n216 s5 word -> n238
node n217 s3 word -> n159
node n218 s19 -> n144
node n219 s15 -> n152
node n220 s16 -> n156
node n221 s1 word last -> n153
node n222 s4 word -> n215
node n223 s5 word -> n238
node n224 s3 word -> n159
node n225 s19 -> n144
node n226 s6 -> n198
node n227 s15 -> n152
node n228 s16 -> n156
node n229 s1 word last -> n153
node n230 s4 word -> n222
node n231 s5 word -> n238
node n232 s3 word -> n159
node n233 s19 -> n144
node n234 s6 -> n198
node n235 s15 -> n152
node n236 s16 -> n156
node n237 s1 word last -> n153
node n238 s4 word -> n230
node n239 s5 word -> n238
node n240 s3 word -> n159
node n241 s19 -> n144
node n242 s6 -> n198
node n243 s15 -> n152
node n244 s16 -> n156
node n245 s1 word last -> n153
node n246 s4 word -> n215
node n247 s5 word -> n238
node n248 s3 word -> n159
node n249 s19 -> n144
node n250 s6 -> n264
node n251 s15 -> n152
node n252 s16 -> n156
node n253 s1 word last -> n153
node n254 s3 word -> n159
node n255 s19 -> n144
node n256 s6 -> n264
node n257 s4 -> n178
node n258 s5 -> n204
node n259 s15 -> n152
node n260 s16 -> n156
node n261 s1 word last -> n153
node n262 s0 word last -> n254
node n263 s0 last -> n262
node n264 s0 last -> n263
node n265 s4 word -> n246
node n266 s5 word -> n273
node n267 s3 word -> n159
node n268 s19 -> n144
node n269 s6 -> n264
node n270 s15 -> n152
node n271 s16 -> n156
node n272 s1 word last -> n153
node n273 s4 word -> n230
node n274 s5 word -> n238
node n275 s3 word -> n159
node n276 s19 -> n144
node n277 s6 -> n264
node n278 s15 -> n152
node n279 s16 -> n156
node n280 s1 word last -> n153
node n281 s4 word -> n265
node n282 s5 word -> n297
node n283 s3 word -> n159
node n284 s19 -> n144
node n285 s6 -> n264
node n286 s15 -> n152
node n287 s16 -> n156
node n288 s1 word last -> n153
node n289 s4 word -> n222
node n290 s5 word -> n238
node n291 s3 word -> n159
node n292 s19 -> n144
node n293 s6 -> n264
node n294 s15 -> n152
node n295 s16 -> n156
node n296 s1 word last -> n153
node n297 s4 word -> n289
node n298 s5 word -> n273
node n299 s3 word -> n159
node n300 s19 -> n144
node n301 s6 -> n264
node n302 s15 -> n152
node n303 s16 -> n156
node n304 s1 word last -> n153
node n305 s19 -> n144
node n306 s0 -> n310
node n307 s15 -> n152
node n308 s16 -> n156
node n309 s1 word last -> n153
node n310 s0 -> n310
node n311 s1 word last -> n153
node n312 s0 word -> n305
node n313 s1 word last -> n153
node n314 s0 -> n312
node n315 s19 -> n144
node n316 s15 -> n152
node n317 s16 -> n156
node n318 s2 word last -> n165
node n319 s9 word last
node n320 s10 word -> n319
node n321 s8 word last
node n322 s8 word -> n320
node n323 s11 -> n329
node n324 s0 -> n330
node n325 s1 word -> n332
node n326 s13 -> n336
node n327 s20 word last
node n328 s10 word last -> n319
node n329 s8 last -> n328
node n330 s0 -> n330
node n331 s1 word last -> n332
node n332 s8 word -> n320
node n333 s11 -> n329
node n334 s13 -> n336
node n335 s20 word last
node n336 s12 word last
node n337 s0 word -> n322
node n338 s1 word last -> n332
node n339 s0 -> n337
node n340 s2 word -> n346
node n341 s8 word -> n320
node n342 s11 -> n329
node n343 s1 word -> n332
node n344 s13 -> n336
node n345 s20 word last
node n346 s8 word -> n320
node n347 s11 -> n329
node n348 s2 word -> n332
node n349 s13 -> n336
node n350 s20 word last
node n351 s3 word -> n339
node n352 s8 word -> n320
node n353 s11 -> n329
node n354 s1 word -> n332
node n355 s4 -> n361
node n356 s5 -> n387
node n357 s13 -> n336
node n358 s20 word last
node n359 s0 -> n359
node n360 s1 word last -> n332
node n361 s3 -> n359
node n362 s1 word -> n332
node n363 s4 -> n361
node n364 s5 last -> n387
node n365 s4 -> n365
node n366 s5 -> n387
node n367 s3 -> n359
node n368 s1 word last -> n332
node n369 s4 -> n365
node n370 s5 -> n387
node n371 s3 -> n359
node n372 s1 word -> n332
node n373 s6 last -> n381
node n374 s3 -> n359
node n375 s1 word -> n332
node n376 s4 -> n361
node n377 s5 -> n387
node n378 s6 last -> n381
node n379 s0 last -> n374
node n380 s0 last -> n379
node n381 s0 last -> n380
node n382 s4 -> n369
node n383 s5 -> n387
node n384 s3 -> n359
node n385 s1 word -> n332
node n386 s6 last -> n381
node n387 s4 -> n382
node n388 s5 -> n387
node n389 s3 -> n359
node n390 s1 word -> n332
node n391 s6 last -> n381
node n392 s4 word -> n392
node n393 s5 word -> n418
node n394 s3 word -> n339
node n395 s8 word -> n320
node n396 s11 -> n329
node n397 s1 word -> n332
node n398 s13 -> n336
node n399 s20 word last
node n400 s4 word -> n392
node n401 s5 word -> n418
node n402 s3 word -> n339
node n403 s8 word -> n320
node n404 s11 -> n329
node n405 s1 word -> n332
node n406 s6 -> n381
node n407 s13 -> n336
node n408 s20 word last
node n409 s4 word -> n400
node n410 s5 word -> n418
node n411 s3 word -> n339
node n412 s8 word -> n320
node n413 s11 -> n329
node n414 s1 word -> n332
node n415 s6 -> n381
node n416 s13 -> n336
node n417 s20 word last
node n418 s4 word -> n409
node n419 s5 word -> n418
node n420 s3 word -> n339
node n421 s8 word -> n320
node n422 s11 -> n329
node n423 s1 word -> n332
node n424 s6 -> n381
node n425 s13 -> n336
node n426 s20 word last
node n427 s4 word -> n392
node n428 s5 word -> n418
node n429 s3 word -> n339
node n430 s8 word -> n320
node n431 s11 -> n329
node n432 s6 -> n447
node n433 s1 word -> n332
node n434 s13 -> n336
node n435 s20 word last
node n436 s3 word -> n339
node n437 s8 word -> n320
node n438 s11 -> n329
node n439 s6 -> n447
node n440 s1 word -> n332
node n441 s4 -> n361
node n442 s5 -> n387
node n443 s13 -> n336
node n444 s20 word last
node n445 s0 word last -> n436
node n446 s0 last -> n445
node n447 s0 last -> n446
node n448 s4 word -> n427
node n449 s5 word -> n457
node n450 s3 word -> n339
node n451 s8 word -> n320
node n452 s11 -> n329
node n453 s6 -> n447
node n454 s1 word -> n332
node n455 s13 -> n336
node n456 s20 word last
node n457 s4 word -> n409
node n458 s5 word -> n418
node n459 s3 word -> n339
node n460 s8 word -> n320
node n461 s11 -> n329
node n462 s6 -> n447
node n463 s1 word -> n332
node n464 s13 -> n336
node n465 s20 word last
node n466 s4 word -> n448
node n467 s5 word -> n484
node n468 s3 word -> n339
node n469 s8 word -> n320
node n470 s11 -> n329
node n471 s6 -> n447
node n472 s1 word -> n332
node n473 s13 -> n336
node n474 s20 word last
node n475 s4 word -> n400
node n476 s5 word -> n418
node n477 s3 word -> n339
node n478 s8 word -> n320
node n479 s11 -> n329
node n480 s6 -> n447
node n481 s1 word -> n332
node n482 s13 -> n336
node n483 s20 word last
node n484 s4 word -> n475
node n485 s5 word -> n457
node n486 s3 word -> n339
node n487 s8 word -> n320
node n488 s11 -> n329
node n489 s6 -> n447
node n490 s1 word -> n332
node n491 s13 -> n336
node n492 s20 word last
node n493 s8 word -> n320
node n494 s11 -> n329
node n495 s1 word -> n332
node n496 s0 -> n499
node n497 s13 -> n336
node n498 s20 word last
node n499 s1 word -> n332
node n500 s0 last -> n499
node n501 s0 word -> n493
node n502 s1 word last -> n332
node n503 s0 -> n501
node n504 s2 word -> n346
node n505 s8 word -> n320
node n506 s11 -> n329
node n507 s13 -> n336
node n508 s20 word last
node n509 s0 -> n509
node n510 s1 word last -> n153
node n511 s0 -> n509
node n512 s15 -> n152
node n513 s16 -> n156
node n514 s19 -> n144
node n515 s1 word last -> n153
node n516 s0 word -> n511
node n517 s1 word last -> n153
node n518 s0 -> n516
node n519 s15 -> n152
node n520 s16 -> n156
node n521 s19 -> n144
node n522 s1 word -> n153
node n523 s2 word last -> n165
node n524 s3 word -> n518
node n525 s4 -> n533
node n526 s5 -> n559
node n527 s15 -> n152
node n528 s16 -> n156
node n529 s19 -> n144
node n530 s1 word last -> n153
node n531 s0 -> n531
node n532 s1 word last -> n153
node n533 s3 -> n531
node n534 s4 -> n533
node n535 s5 -> n559
node n536 s1 word last -> n153
node n537 s4 -> n537
node n538 s5 -> n559
node n539 s3 -> n531
node n540 s1 word last -> n153
node n541 s4 -> n537
node n542 s5 -> n559
node n543 s3 -> n531
node n544 s6 -> n553
node n545 s1 word last -> n153
node n546 s3 -> n531
node n547 s4 -> n533
node n548 s5 -> n559
node n549 s6 -> n553
node n550 s1 word last -> n153
node n551 s0 last -> n546
node n552 s0 last -> n551
node n553 s0 last -> n552
node n554 s4 -> n541
node n555 s5 -> n559
node n556 s3 -> n531
node n557 s6 -> n553
node n558 s1 word last -> n153
node n559 s4 -> n554
node n560 s5 -> n559
node n561 s3 -> n531
node n562 s6 -> n553
node n563 s1 word last -> n153
node n564 s4 word -> n524
node n565 s5 word -> n636
node n566 s3 word -> n669
node n567 s15 -> n152
node n568 s16 -> n156
node n569 s19 last -> n144
node n570 s4 word -> n570
node n571 s5 word -> n593
node n572 s3 word -> n518
node n573 s15 -> n152
node n574 s16 -> n156
node n575 s19 -> n144
node n576 s1 word last -> n153
node n577 s4 word -> n570
node n578 s5 word -> n593
node n579 s3 word -> n518
node n580 s6 -> n553
node n581 s15 -> n152
node n582 s16 -> n156
node n583 s19 -> n144
node n584 s1 word last -> n153
node n585 s4 word -> n577
node n586 s5 word -> n593
node n587 s3 word -> n518
node n588 s6 -> n553
node n589 s15 -> n152
node n590 s16 -> n156
node n591 s19 -> n144
node n592 s1 word last -> n153
node n593 s4 word -> n585
node n594 s5 word -> n593
node n595 s3 word -> n518
node n596 s6 -> n553
node n597 s15 -> n152
node n598 s16 -> n156
node n599 s19 -> n144
node n600 s1 word last -> n153
node n601 s4 word -> n570
node n602 s5 word -> n593
node n603 s3 word -> n518
node n604 s6 -> n619
node n605 s15 -> n152
node n606 s16 -> n156
node n607 s19 -> n144
node n608 s1 word last -> n153
node n609 s3 word -> n518
node n610 s6 -> n619
node n611 s4 -> n533
node n612 s5 -> n559
node n613 s15 -> n152
node n614 s16 -> n156
node n615 s19 -> n144
node n616 s1 word last -> n153
node n617 s0 word last -> n609
node n618 s0 last -> n617
node n619 s0 last -> n618
node n620 s4 word -> n601
node n621 s5 word -> n628
node n622 s3 word -> n518
node n623 s6 -> n619
node n624 s15 -> n152
node n625 s16 -> n156
node n626 s19 -> n144
node n627 s1 word last -> n153
node n628 s4 word -> n585
node n629 s5 word -> n593
node n630 s3 word -> n518
node n631 s6 -> n619
node n632 s15 -> n152
node n633 s16 -> n156
node n634 s19 -> n144
node n635 s1 word last -> n153
node n636 s4 word -> n620
node n637 s5 word -> n652
node n638 s3 word -> n518
node n639 s6 -> n619
node n640 s15 -> n152
node n641 s16 -> n156
node n642 s19 -> n144
node n643 s1 word last -> n153
node n644 s4 word -> n577
node n645 s5 word -> n593
node n646 s3 word -> n518
node n647 s6 -> n619
node n648 s15 -> n152
node n649 s16 -> n156
node n650 s19 -> n144
node n651 s1 word last -> n153
node n652 s4 word -> n644
node n653 s5 word -> n628
node n654 s3 word -> n518
node n655 s6 -> n619
node n656 s15 -> n152
node n657 s16 -> n156
node n658 s19 -> n144
node n659 s1 word last -> n153
node n660 s0 -> n660
node n661 s1 word last -> n153
node n662 s0 -> n660
node n663 s15 -> n152
node n664 s16 -> n156
node n665 s19 -> n144
node n666 s1 word last -> n153
node n667 s0 word -> n662
node n668 s1 word last -> n153
node n669 s0 -> n667
node n670 s15 -> n152
node n671 s16 -> n156
node n672 s19 -> n144
node n673 s2 word last -> n165
node n674 s14 word -> n564
node n675 s4 word -> n703
node n676 s5 word -> n818
node n677 s3 word -> n855
node n678 s8 word -> n971
node n679 s11 -> n981
node n680 s13 -> n982
node n681 s7 word -> n905
node n682 s16 -> n984
node n683 s15 -> n985
node n684 s19 -> n987
node n685 s20 word last
node n686 s0 -> n686
node n687 s1 word last -> n332
node n688 s0 -> n686
node n689 s13 -> n336
node n690 s8 word -> n320
node n691 s11 -> n329
node n692 s1 word -> n332
node n693 s20 word last
node n694 s0 word -> n688
node n695 s1 word last -> n332
node n696 s0 -> n694
node n697 s13 -> n336
node n698 s8 word -> n320
node n699 s11 -> n329
node n700 s1 word -> n332
node n701 s2 word -> n346
node n702 s20 word last
node n703 s3 word -> n696
node n704 s4 -> n713
node n705 s5 -> n739
node n706 s13 -> n336
node n707 s8 word -> n320
node n708 s11 -> n329
node n709 s1 word -> n332
node n710 s20 word last
node n711 s0 -> n711
node n712 s1 word last -> n332
node n713 s3 -> n711
node n714 s4 -> n713
node n715 s5 -> n739
node n716 s1 word last -> n332
node n717 s4 -> n717
node n718 s5 -> n739
node n719 s3 -> n711
node n720 s1 word last -> n332
node n721 s4 -> n717
node n722 s5 -> n739
node n723 s3 -> n711
node n724 s6 -> n733
node n725 s1 word last -> n332
node n726 s3 -> n711
node n727 s4 -> n713
node n728 s5 -> n739
node n729 s6 -> n733
node n730 s1 word last -> n332
node n731 s0 last -> n726
node n732 s0 last -> n731
node n733 s0 last -> n732
node n734 s4 -> n721
node n735 s5 -> n739
node n736 s3 -> n711
node n737 s6 -> n733
node n738 s1 word last -> n332
node n739 s4 -> n734
node n740 s5 -> n739
node n741 s3 -> n711
node n742 s6 -> n733
node n743 s1 word last -> n332
node n744 s4 word -> n744
node n745 s5 word -> n770
node n746 s3 word -> n696
node n747 s13 -> n336
node n748 s8 word -> n320
node n749 s11 -> n329
node n750 s1 word -> n332
node n751 s20 word last
node n752 s4 word -> n744
node n753 s5 word -> n770
node n754 s3 word -> n696
node n755 s6 -> n733
node n756 s13 -> n336
node n757 s8 word -> n320
node n758 s11 -> n329
node n759 s1 word -> n332
node n760 s20 word last
node n761 s4 word -> n752
node n762 s5 word -> n770
node n763 s3 word -> n696
node n764 s6 -> n733
node n765 s13 -> n336
node n766 s8 word -> n320
node n767 s11 -> n329
node n768 s1 word -> n332
node n769 s20 word last
node n770 s4 word -> n761
node n771 s5 word -> n770
node n772 s3 word -> n696
node n773 s6 -> n733
node n774 s13 -> n336
node n775 s8 word -> n320
node n776 s11 -> n329
node n777 s1 word -> n332
node n778 s20 word last
node n779 s4 word -> n744
node n780 s5 word -> n770
node n781 s3 word -> n696
node n782 s6 -> n799
node n783 s13 -> n336
node n784 s8 word -> n320
node n785 s11 -> n329
node n786 s1 word -> n332
node n787 s20 word last
node n788 s3 word -> n696
node n789 s6 -> n799
node n790 s4 -> n713
node n791 s5 -> n739
node n792 s13 -> n336
node n793 s8 word -> n320
node n794 s11 -> n329
node n795 s1 word -> n332
node n796 s20 word last
node n797 s0 word last -> n788
node n798 s0 last -> n797
node n799 s0 last -> n798
node n800 s4 word -> n779
node n801 s5 word -> n809
node n802 s3 word -> n696
node n803 s6 -> n799
node n804 s13 -> n336
node n805 s8 word -> n320
node n806 s11 -> n329
node n807 s1 word -> n332
node n808 s20 word last
node n809 s4 word -> n761
node n810 s5 word -> n770
node n811 s3 word -> n696
node n812 s6 -> n799
node n813 s13 -> n336
node n814 s8 word -> n320
node n815 s11 -> n329
node n816 s1 word -> n332
node n817 s20 word last
node n818 s4 word -> n800
node n819 s5 word -> n836
node n820 s3 word -> n696
node n821 s6 -> n799
node n822 s13 -> n336
node n823 s8 word -> n320
node n824 s11 -> n329
node n825 s1 word -> n332
node n826 s20 word last
node n827 s4 word -> n752
node n828 s5 word -> n770
node n829 s3 word -> n696
node n830 s6 -> n799
node n831 s13 -> n336
node n832 s8 word -> n320
node n833 s11 -> n329
node n834 s1 word -> n332
node n835 s20 word last
node n836 s4 word -> n827
node n837 s5 word -> n809
node n838 s3 word -> n696
node n839 s6 -> n799
node n840 s13 -> n336
node n841 s8 word -> n320
node n842 s11 -> n329
node n843 s1 word -> n332
node n844 s20 word last
node n845 s0 -> n845
node n846 s1 word last -> n332
node n847 s0 -> n845
node n848 s13 -> n336
node n849 s8 word -> n320
node n850 s11 -> n329
node n851 s1 word -> n332
node n852 s20 word last
node n853 s0 word -> n847
node n854 s1 word last -> n332
node n855 s0 -> n853
node n856 s13 -> n336
node n857 s8 word -> n320
node n858 s11 -> n329
node n859 s2 word -> n346
node n860 s20 word last
node n861 s0 -> n861
node n862 s1 word last
node n863 s0 word -> n861
node n864 s1 word last
node n865 s0 -> n863
node n866 s2 word -> n22
node n867 s1 word last
node n868 s3 word -> n865
node n869 s4 -> n874
node n870 s5 -> n900
node n871 s1 word last
node n872 s0 -> n872
node n873 s1 word last
node n874 s3 -> n872
node n875 s4 -> n874
node n876 s5 -> n900
node n877 s1 word last
node n878 s4 -> n878
node n879 s5 -> n900
node n880 s3 -> n872
node n881 s1 word last
node n882 s4 -> n878
node n883 s5 -> n900
node n884 s3 -> n872
node n885 s6 -> n894
node n886 s1 word last
node n887 s3 -> n872
node n888 s4 -> n874
node n889 s5 -> n900
node n890 s6 -> n894
node n891 s1 word last
node n892 s0 last -> n887
node n893 s0 last -> n892
node n894 s0 last -> n893
node n895 s4 -> n882
node n896 s5 -> n900
node n897 s3 -> n872
node n898 s6 -> n894
node n899 s1 word last
node n900 s4 -> n895
node n901 s5 -> n900
node n902 s3 -> n872
node n903 s6 -> n894
node n904 s1 word last
node n905 s4 word -> n868
node n906 s5 word -> n950
node n907 s3 word last -> n969
node n908 s4 word -> n908
node n909 s5 word -> n922
node n910 s3 word -> n865
node n911 s1 word last
node n912 s4 word -> n908
node n913 s5 word -> n922
node n914 s3 word -> n865
node n915 s6 -> n894
node n916 s1 word last
node n917 s4 word -> n912
node n918 s5 word -> n922
node n919 s3 word -> n865
node n920 s6 -> n894
node n921 s1 word last
node n922 s4 word -> n917
node n923 s5 word -> n922
node n924 s3 word -> n865
node n925 s6 -> n894
node n926 s1 word last
node n927 s4 word -> n908
node n928 s5 word -> n922
node n929 s3 word -> n865
node n930 s6 -> n939
node n931 s1 word last
node n932 s3 word -> n865
node n933 s6 -> n939
node n934 s4 -> n874
node n935 s5 -> n900
node n936 s1 word last
node n937 s0 word last -> n932
node n938 s0 last -> n937
node n939 s0 last -> n938
node n940 s4 word -> n927
node n941 s5 word -> n945
node n942 s3 word -> n865
node n943 s6 -> n939
node n944 s1 word last
node n945 s4 word -> n917
node n946 s5 word -> n922
node n947 s3 word -> n865
node n948 s6 -> n939
node n949 s1 word last
node n950 s4 word -> n940
node n951 s5 word -> n960
node n952 s3 word -> n865
node n953 s6 -> n939
node n954 s1 word last
node n955 s4 word -> n912
node n956 s5 word -> n922
node n957 s3 word -> n865
node n958 s6 -> n939
node n959 s1 word last
node n960 s4 word -> n955
node n961 s5 word -> n945
node n962 s3 word -> n865
node n963 s6 -> n939
node n964 s1 word last
node n965 s0 -> n965
node n966 s1 word last
node n967 s0 word -> n965
node n968 s1 word last
node n969 s0 -> n967
node n970 s2 word last -> n22
node n971 s8 word -> n905
node n972 s10 word -> n976
node n973 s4 word -> n868
node n974 s5 word -> n950
node n975 s3 word last -> n969
node n976 s9 word -> n905
node n977 s4 word -> n868
node n978 s5 word -> n950
node n979 s3 word last -> n969
node n980 s10 word last -> n976
node n981 s8 last -> n980
node n982 s12 word last -> n905
node n983 s14 word last -> n905
node n984 s15 last -> n983
node n985 s17 last -> n983
node n986 s18 last -> n983
node n987 s13 last -> n986
node n988 s15 -> n152
node n989 s16 -> n156
node n990 s0 -> n993
node n991 s1 word -> n153
node n992 s19 last -> n144
node n993 s0 -> n993
node n994 s1 word last -> n153
node n995 s0 word -> n988
node n996 s1 word last -> n153
node n997 s0 -> n995
node n998 s2 word -> n165
node n999 s15 -> n152
node n1000 s16 -> n156
node n1001 s1 word -> n153
node n1002 s19 last -> n144
node n1003 s3 word -> n997
node n1004 s15 -> n152
node n1005 s16 -> n156
node n1006 s1 word -> n153
node n1007 s4 -> n1012
node n1008 s5 -> n1038
node n1009 s19 last -> n144
node n1010 s0 -> n1010
node n1011 s1 word last -> n153
node n1012 s3 -> n1010
node n1013 s1 word -> n153
node n1014 s4 -> n1012
node n1015 s5 last -> n1038
node n1016 s4 -> n1016
node n1017 s5 -> n1038
node n1018 s3 -> n1010
node n1019 s1 word last -> n153
node n1020 s4 -> n1016
node n1021 s5 -> n1038
node n1022 s3 -> n1010
node n1023 s1 word -> n153
node n1024 s6 last -> n1032
node n1025 s3 -> n1010
node n1026 s1 word -> n153
node n1027 s4 -> n1012
node n1028 s5 -> n1038
node n1029 s6 last -> n1032
node n1030 s0 last -> n1025
node n1031 s0 last -> n1030
node n1032 s0 last -> n1031
node n1033 s4 -> n1020
node n1034 s5 -> n1038
node n1035 s3 -> n1010
node n1036 s1 word -> n153
node n1037 s6 last -> n1032
node n1038 s4 -> n1033
node n1039 s5 -> n1038
node n1040 s3 -> n1010
node n1041 s1 word -> n153
node n1042 s6 last -> n1032
node n1043 s4 word -> n1003
node n1044 s5 word -> n1115
node n1045 s3 word -> n1148
node n1046 s15 -> n152
node n1047 s16 -> n156
node n1048 s19 last -> n144
node n1049 s4 word -> n1049
node n1050 s5 word -> n1072
node n1051 s3 word -> n997
node n1052 s15 -> n152
node n1053 s16 -> n156
node n1054 s1 word -> n153
node n1055 s19 last -> n144
node n1056 s4 word -> n1049
node n1057 s5 word -> n1072
node n1058 s3 word -> n997
node n1059 s15 -> n152
node n1060 s16 -> n156
node n1061 s1 word -> n153
node n1062 s6 -> n1032
node n1063 s19 last -> n144
node n1064 s4 word -> n1056
node n1065 s5 word -> n1072
node n1066 s3 word -> n997
node n1067 s15 -> n152
node n1068 s16 -> n156
node n1069 s1 word -> n153
node n1070 s6 -> n1032
node n1071 s19 last -> n144
node n1072 s4 word -> n1064
node n1073 s5 word -> n1072
node n1074 s3 word -> n997
node n1075 s15 -> n152
node n1076 s16 -> n156
node n1077 s1 word -> n153
node n1078 s6 -> n1032
node n1079 s19 last -> n144
node n1080 s4 word -> n1049
node n1081 s5 word -> n1072
node n1082 s3 word -> n997
node n1083 s15 -> n152
node n1084 s16 -> n156
node n1085 s6 -> n1098
node n1086 s1 word -> n153
node n1087 s19 last -> n144
node n1088 s3 word -> n997
node n1089 s15 -> n152
node n1090 s16 -> n156
node n1091 s6 -> n1098
node n1092 s1 word -> n153
node n1093 s4 -> n1012
node n1094 s5 -> n1038
node n1095 s19 last -> n144
node n1096 s0 word last -> n1088
node n1097 s0 last -> n1096
node n1098 s0 last -> n1097
node n1099 s4 word -> n1080
node n1100 s5 word -> n1107
node n1101 s3 word -> n997
node n1102 s15 -> n152
node n1103 s16 -> n156
node n1104 s6 -> n1098
node n1105 s1 word -> n153
node n1106 s19 last -> n144
node n1107 s4 word -> n1064
node n1108 s5 word -> n1072
node n1109 s3 word -> n997
node n1110 s15 -> n152
node n1111 s16 -> n156
node n1112 s6 -> n1098
node n1113 s1 word -> n153
node n1114 s19 last -> n144
node n1115 s4 word -> n1099
node n1116 s5 word -> n1131
node n1117 s3 word -> n997
node n1118 s15 -> n152
node n1119 s16 -> n156
node n1120 s6 -> n1098
node n1121 s1 word -> n153
node n1122 s19 last -> n144
node n1123 s4 word -> n1056
node n1124 s5 word -> n1072
node n1125 s3 word -> n997
node n1126 s15 -> n152
node n1127 s16 -> n156
node n1128 s6 -> n1098
node n1129 s1 word -> n153
node n1130 s19 last -> n144
node n1131 s4 word -> n1123
node n1132 s5 word -> n1107
node n1133 s3 word -> n997
node n1134 s15 -> n152
node n1135 s16 -> n156
node n1136 s6 -> n1098
node n1137 s1 word -> n153
node n1138 s19 last -> n144
node n1139 s15 -> n152
node n1140 s16 -> n156
node n1141 s1 word -> n153
node n1142 s0 -> n1144
node n1143 s19 last -> n144
node n1144 s1 word -> n153
node n1145 s0 last -> n1144
node n1146 s0 word -> n1139
node n1147 s1 word last -> n153
node n1148 s0 -> n1146
node n1149 s2 word -> n165
node n1150 s15 -> n152
node n1151 s16 -> n156
node n1152 s19 last -> n144
node n1153 s14 word -> n1043
node n1154 s4 word -> n1182
node n1155 s5 word -> n1297
node n1156 s3 word -> n1334
node n1157 s8 word -> n1450
node n1158 s11 -> n1460
node n1159 s13 -> n1461
node n1160 s7 word -> n1384
node n1161 s16 -> n1463
node n1162 s15 -> n1464
node n1163 s19 -> n1466
node n1164 s20 word last
node n1165 s13 -> n336
node n1166 s0 -> n1171
node n1167 s8 word -> n320
node n1168 s11 -> n329
node n1169 s1 word -> n332
node n1170 s20 word last
node n1171 s0 -> n1171
node n1172 s1 word last -> n332
node n1173 s0 word -> n1165
node n1174 s1 word last -> n332
node n1175 s0 -> n1173
node n1176 s13 -> n336
node n1177 s8 word -> n320
node n1178 s11 -> n329
node n1179 s1 word -> n332
node n1180 s2 word -> n346
node n1181 s20 word last
node n1182 s3 word -> n1175
node n1183 s13 -> n336
node n1184 s4 -> n1192
node n1185 s5 -> n1218
node n1186 s8 word -> n320
node n1187 s11 -> n329
node n1188 s1 word -> n332
node n1189 s20 word last
node n1190 s0 -> n1190
node n1191 s1 word last -> n332
node n1192 s3 -> n1190
node n1193 s4 -> n1192
node n1194 s5 -> n1218
node n1195 s1 word last -> n332
node n1196 s4 -> n1196
node n1197 s5 -> n1218
node n1198 s3 -> n1190
node n1199 s1 word last -> n332
node n1200 s4 -> n1196
node n1201 s5 -> n1218
node n1202 s3 -> n1190
node n1203 s6 -> n1212
node n1204 s1 word last -> n332
node n1205 s3 -> n1190
node n1206 s4 -> n1192
node n1207 s5 -> n1218
node n1208 s6 -> n1212
node n1209 s1 word last -> n332
node n1210 s0 last -> n1205
node n1211 s0 last -> n1210
node n1212 s0 last -> n1211
node n1213 s4 -> n1200
node n1214 s5 -> n1218
node n1215 s3 -> n1190
node n1216 s6 -> n1212
node n1217 s1 word last -> n332
node n1218 s4 -> n1213
node n1219 s5 -> n1218
node n1220 s3 -> n1190
node n1221 s6 -> n1212
node n1222 s1 word last -> n332
node n1223 s4 word -> n1223
node n1224 s5 word -> n1249
node n1225 s3 word -> n1175
node n1226 s13 -> n336
node n1227 s8 word -> n320
node n1228 s11 -> n329
node n1229 s1 word -> n332
node n1230 s20 word last
node n1231 s4 word -> n1223
node n1232 s5 word -> n1249
node n1233 s3 word -> n1175
node n1234 s13 -> n336
node n1235 s6 -> n1212
node n1236 s8 word -> n320
node n1237 s11 -> n329
node n1238 s1 word -> n332
node n1239 s20 word last
node n1240 s4 word -> n1231
node n1241 s5 word -> n1249
node n1242 s3 word -> n1175
node n1243 s13 -> n336
node n1244 s6 -> n1212
node n1245 s8 word -> n320
node n1246 s11 -> n329
node n1247 s1 word -> n332
node n1248 s20 word last
node n1249 s4 word -> n1240
node n1250 s5 word -> n1249
node n1251 s3 word -> n1175
node n1252 s13 -> n336
node n1253 s6 -> n1212
node n1254 s8 word -> n320
node n1255 s11 -> n329
node n1256 s1 word -> n332
node n1257 s20 word last
node n1258 s4 word -> n1223
node n1259 s5 word -> n1249
node n1260 s3 word -> n1175
node n1261 s13 -> n336
node n1262 s6 -> n1278
node n1263 s8 word -> n320
node n1264 s11 -> n329
node n1265 s1 word -> n332
node n1266 s20 word last
node n1267 s3 word -> n1175
node n1268 s13 -> n336
node n1269 s6 -> n1278
node n1270 s4 -> n1192
node n1271 s5 -> n1218
node n1272 s8 word -> n320
node n1273 s11 -> n329
node n1274 s1 word -> n332
node n1275 s20 word last
node n1276 s0 word last -> n1267
node n1277 s0 last -> n1276
node n1278 s0 last -> n1277
node n1279 s4 word -> n1258
node n1280 s5 word -> n1288
node n1281 s3 word -> n1175
node n1282 s13 -> n336
node n1283 s6 -> n1278
node n1284 s8 word -> n320
node n1285 s11 -> n329
node n1286 s1 word -> n332
node n1287 s20 word last
node n1288 s4 word -> n1240
node n1289 s5 word -> n1249
node n1290 s3 word -> n1175
node n1291 s13 -> n336
node n1292 s6 -> n1278
node n1293 s8 word -> n320
node n1294 s11 -> n329
node n1295 s1 word -> n332
node n1296 s20 word last
node n1297 s4 word -> n1279
node n1298 s5 word -> n1315
node n1299 s3 word -> n1175
node n1300 s13 -> n336
node n1301 s6 -> n1278
node n1302 s8 word -> n320
node n1303 s11 -> n329
node n1304 s1 word -> n332
node n1305 s20 word last
node n1306 s4 word -> n1231
node n1307 s5 word -> n1249
node n1308 s3 word -> n1175
node n1309 s13 -> n336
node n1310 s6 -> n1278
node n1311 s8 word -> n320
node n1312 s11 -> n329
node n1313 s1 word -> n332
node n1314 s20 word last
node n1315 s4 word -> n1306
node n1316 s5 word -> n1288
node n1317 s3 word -> n1175
node n1318 s13 -> n336
node n1319 s6 -> n1278
node n1320 s8 word -> n320
node n1321 s11 -> n329
node n1322 s1 word -> n332
node n1323 s20 word last
node n1324 s13 -> n336
node n1325 s0 -> n1330
node n1326 s8 word -> n320
node n1327 s11 -> n329
node n1328 s1 word -> n332
node n1329 s20 word last
node n1330 s0 -> n1330
node n1331 s1 word last -> n332
node n1332 s0 word -> n1324
node n1333 s1 word last -> n332
node n1334 s0 -> n1332
node n1335 s13 -> n336
node n1336 s8 word -> n320
node n1337 s11 -> n329
node n1338 s2 word -> n346
node n1339 s20 word last
node n1340 s0 -> n1340
node n1341 s1 word last
node n1342 s0 word -> n1340
node n1343 s1 word last
node n1344 s0 -> n1342
node n1345 s2 word -> n22
node n1346 s1 word last
node n1347 s3 word -> n1344
node n1348 s4 -> n1353
node n1349 s5 -> n1379
node n1350 s1 word last
node n1351 s0 -> n1351
node n1352 s1 word last
node n1353 s3 -> n1351
node n1354 s4 -> n1353
node n1355 s5 -> n1379
node n1356 s1 word last
node n1357 s4 -> n1357
node n1358 s5 -> n1379
node n1359 s3 -> n1351
node n1360 s1 word last
node n1361 s4 -> n1357
node n1362 s5 -> n1379
node n1363 s3 -> n1351
node n1364 s6 -> n1373
node n1365 s1 word last
node n1366 s3 -> n1351
node n1367 s4 -> n1353
node n1368 s5 -> n1379
node n1369 s6 -> n1373
node n1370 s1 word last
node n1371 s0 last -> n1366
node n1372 s0 last -> n1371
node n1373 s0 last -> n1372
node n1374 s4 -> n1361
node n1375 s5 -> n1379
node n1376 s3 -> n1351
node n1377 s6 -> n1373
node n1378 s1 word last
node n1379 s4 -> n1374
node n1380 s5 -> n1379
node n1381 s3 -> n1351
node n1382 s6 -> n1373
node n1383 s1 word last
node n1384 s4 word -> n1347
node n1385 s5 word -> n1429
node n1386 s3 word last -> n1448
node n1387 s4 word -> n1387
node n1388 s5 word -> n1401
node n1389 s3 word -> n1344
node n1390 s1 word last
node n1391 s4 word -> n1387
node n1392 s5 word -> n1401
node n1393 s3 word -> n1344
node n1394 s6 -> n1373
node n1395 s1 word last
node n1396 s4 word -> n1391
node n1397 s5 word -> n1401
node n1398 s3 word -> n1344
node n1399 s6 -> n1373
node n1400 s1 word last
node n1401 s4 word -> n1396
node n1402 s5 word -> n1401
node n1403 s3 word -> n1344
node n1404 s6 -> n1373
node n1405 s1 word last
node n1406 s4 word -> n1387
node n1407 s5 word -> n1401
node n1408 s3 word -> n1344
node n1409 s6 -> n1418
node n1410 s1 word last
node n1411 s3 word -> n1344
node n1412 s6 -> n1418
node n1413 s4 -> n1353
node n1414 s5 -> n1379
node n1415 s1 word last
node n1416 s0 word last -> n1411
node n1417 s0 last -> n1416
node n1418 s0 last -> n1417
node n1419 s4 word -> n1406
node n1420 s5 word -> n1424
node n1421 s3 word -> n1344
node n1422 s6 -> n1418
node n1423 s1 word last
node n1424 s4 word -> n1396
node n1425 s5 word -> n1401
node n1426 s3 word -> n1344
node n1427 s6 -> n1418
node n1428 s1 word last
node n1429 s4 word -> n1419
node n1430 s5 word -> n1439
node n1431 s3 word -> n1344
node n1432 s6 -> n1418
node n1433 s1 word last
node n1434 s4 word -> n1391
node n1435 s5 word -> n1401
node n1436 s3 word -> n1344
node n1437 s6 -> n1418
node n1438 s1 word last
node n1439 s4 word -> n1434
node n1440 s5 word -> n1424
node n1441 s3 word -> n1344
node n1442 s6 -> n1418
node n1443 s1 word last
node n1444 s0 -> n1444
node n1445 s1 word last
node n1446 s0 word -> n1444
node n1447 s1 word last
node n1448 s0 -> n1446
node n1449 s2 word last -> n22
node n1450 s8 word -> n1384
node n1451 s10 word -> n1455
node n1452 s4 word -> n1347
node n1453 s5 word -> n1429
node n1454 s3 word last -> n1448
node n1455 s9 word -> n1384
node n1456 s4 word -> n1347
node n1457 s5 word -> n1429
node n1458 s3 word last -> n1448
node n1459 s10 word last -> n1455
node n1460 s8 last -> n1459
node n1461 s12 word last -> n1384
node n1462 s14 word last -> n1384
node n1463 s15 last -> n1462
node n1464 s17 last -> n1462
node n1465 s18 last -> n1462
node n1466 s13 last -> n1465
node n1467 s22 word last
node n1468 s17 last -> n1467
node n1469 s15 -> n1468
node n1470 s16 -> n1475
node n1471 s19 -> n1477
node n1472 s0 -> n1478
node n1473 s1 -> n1480
node n1474 s22 word last
node n1475 s15 last -> n1467
node n1476 s18 last -> n1467
node n1477 s13 last -> n1476
node n1478 s0 -> n1478
node n1479 s1 last -> n1480
node n1480 s15 -> n1468
node n1481 s16 -> n1475
node n1482 s19 -> n1477
node n1483 s22 word last
node n1484 s0 -> n1469
node n1485 s1 last -> n1480
node n1486 s0 -> n1484
node n1487 s2 -> n1493
node n1488 s15 -> n1468
node n1489 s16 -> n1475
node n1490 s19 -> n1477
node n1491 s1 -> n1480
node n1492 s22 word last
node n1493 s15 -> n1468
node n1494 s16 -> n1475
node n1495 s19 -> n1477
node n1496 s2 -> n1480
node n1497 s22 word last
node n1498 s3 -> n1486
node n1499 s15 -> n1468
node n1500 s16 -> n1475
node n1501 s19 -> n1477
node n1502 s1 -> n1480
node n1503 s4 -> n1508
node n1504 s5 -> n1534
node n1505 s22 word last
node n1506 s0 -> n1506
node n1507 s1 last -> n1480
node n1508 s3 -> n1506
node n1509 s1 -> n1480
node n1510 s4 -> n1508
node n1511 s5 last -> n1534
node n1512 s4 -> n1512
node n1513 s5 -> n1534
node n1514 s3 -> n1506
node n1515 s1 last -> n1480
node n1516 s4 -> n1512
node n1517 s5 -> n1534
node n1518 s3 -> n1506
node n1519 s1 -> n1480
node n1520 s6 last -> n1528
node n1521 s3 -> n1506
node n1522 s1 -> n1480
node n1523 s4 -> n1508
node n1524 s5 -> n1534
node n1525 s6 last -> n1528
node n1526 s0 last -> n1521
node n1527 s0 last -> n1526
node n1528 s0 last -> n1527
node n1529 s4 -> n1516
node n1530 s5 -> n1534
node n1531 s3 -> n1506
node n1532 s1 -> n1480
node n1533 s6 last -> n1528
node n1534 s4 -> n1529
node n1535 s5 -> n1534
node n1536 s3 -> n1506
node n1537 s1 -> n1480
node n1538 s6 last -> n1528
node n1539 s4 -> n1498
node n1540 s5 -> n1620
node n1541 s3 -> n1657
node n1542 s15 -> n1468
node n1543 s16 -> n1475
node n1544 s19 -> n1477
node n1545 s22 word last
node n1546 s4 -> n1546
node n1547 s5 -> n1572
node n1548 s3 -> n1486
node n1549 s15 -> n1468
node n1550 s16 -> n1475
node n1551 s19 -> n1477
node n1552 s1 -> n1480
node n1553 s22 word last
node n1554 s4 -> n1546
node n1555 s5 -> n1572
node n1556 s3 -> n1486
node n1557 s15 -> n1468
node n1558 s16 -> n1475
node n1559 s19 -> n1477
node n1560 s1 -> n1480
node n1561 s6 -> n1528
node n1562 s22 word last
node n1563 s4 -> n1554
node n1564 s5 -> n1572
node n1565 s3 -> n1486
node n1566 s15 -> n1468
node n1567 s16 -> n1475
node n1568 s19 -> n1477
node n1569 s1 -> n1480
node n1570 s6 -> n1528
node n1571 s22 word last
node n1572 s4 -> n1563
node n1573 s5 -> n1572
node n1574 s3 -> n1486
node n1575 s15 -> n1468
node n1576 s16 -> n1475
node n1577 s19 -> n1477
node n1578 s1 -> n1480
node n1579 s6 -> n1528
node n1580 s22 word last
node n1581 s4 -> n1546
node n1582 s5 -> n1572
node n1583 s3 -> n1486
node n1584 s15 -> n1468
node n1585 s16 -> n1475
node n1586 s19 -> n1477
node n1587 s6 -> n1601
node n1588 s1 -> n1480
node n1589 s22 word last
node n1590 s3 -> n1486
node n1591 s15 -> n1468
node n1592 s16 -> n1475
node n1593 s19 -> n1477
node n1594 s6 -> n1601
node n1595 s1 -> n1480
node n1596 s4 -> n1508
node n1597 s5 -> n1534
node n1598 s22 word last
node n1599 s0 last -> n1590
node n1600 s0 last -> n1599
node n1601 s0 last -> n1600
node n1602 s4 -> n1581
node n1603 s5 -> n1611
node n1604 s3 -> n1486
node n1605 s15 -> n1468
node n1606 s16 -> n1475
node n1607 s19 -> n1477
node n1608 s6 -> n1601
node n1609 s1 -> n1480
node n1610 s22 word last
node n1611 s4 -> n1563
node n1612 s5 -> n1572
node n1613 s3 -> n1486
node n1614 s15 -> n1468
node n1615 s16 -> n1475
node n1616 s19 -> n1477
node n1617 s6 -> n1601
node n1618 s1 -> n1480
node n1619 s22 word last
node n1620 s4 -> n1602
node n1621 s5 -> n1638
node n1622 s3 -> n1486
node n1623 s15 -> n1468
node n1624 s16 -> n1475
node n1625 s19 -> n1477
node n1626 s6 -> n1601
node n1627 s1 -> n1480
node n1628 s22 word last
node n1629 s4 -> n1554
node n1630 s5 -> n1572
node n1631 s3 -> n1486
node n1632 s15 -> n1468
node n1633 s16 -> n1475
node n1634 s19 -> n1477
node n1635 s6 -> n1601
node n1636 s1 -> n1480
node n1637 s22 word last
node n1638 s4 -> n1629
node n1639 s5 -> n1611
node n1640 s3 -> n1486
node n1641 s15 -> n1468
node n1642 s16 -> n1475
node n1643 s19 -> n1477
node n1644 s6 -> n1601
node n1645 s1 -> n1480
node n1646 s22 word last
node n1647 s15 -> n1468
node n1648 s16 -> n1475
node n1649 s19 -> n1477
node n1650 s1 -> n1480
node n1651 s0 -> n1653
node n1652 s22 word last
node n1653 s1 -> n1480
node n1654 s0 last -> n1653
node n1655 s0 -> n1647
node n1656 s1 last -> n1480
node n1657 s0 -> n1655
node n1658 s2 -> n1493
node n1659 s15 -> n1468
node n1660 s16 -> n1475
node n1661 s19 -> n1477
node n1662 s22 word last
node n1663 s14 -> n1539
node n1664 s4 -> n1714
node n1665 s5 -> n1838
node n1666 s3 -> n1879
node n1667 s20 -> n1467
node n1668 s8 -> n2019
node n1669 s11 -> n2031
node n1670 s13 -> n2032
node n1671 s7 -> n1937
node n1672 s16 -> n2034
node n1673 s15 -> n2035
node n1674 s19 -> n2037
node n1675 s22 word last
node n1676 s20 -> n1467
node n1677 s8 -> n1683
node n1678 s11 -> n1689
node n1679 s13 -> n1690
node n1680 s0 -> n1691
node n1681 s1 -> n1693
node n1682 s22 word last
node n1683 s8 -> n1467
node n1684 s10 -> n1686
node n1685 s22 word last
node n1686 s9 -> n1467
node n1687 s22 word last
node n1688 s10 last -> n1686
node n1689 s8 last -> n1688
node n1690 s12 last -> n1467
node n1691 s0 -> n1691
node n1692 s1 last -> n1693
node n1693 s20 -> n1467
node n1694 s8 -> n1683
node n1695 s11 -> n1689
node n1696 s13 -> n1690
node n1697 s22 word last
node n1698 s0 -> n1676
node n1699 s1 last -> n1693
node n1700 s0 -> n1698
node n1701 s2 -> n1708
node n1702 s20 -> n1467
node n1703 s8 -> n1683
node n1704 s11 -> n1689
node n1705 s13 -> n1690
node n1706 s1 -> n1693
node n1707 s22 word last
node n1708 s20 -> n1467
node n1709 s8 -> n1683
node n1710 s11 -> n1689
node n1711 s13 -> n1690
node n1712 s2 -> n1693
node n1713 s22 word last
node n1714 s3 -> n1700
node n1715 s20 -> n1467
node n1716 s8 -> n1683
node n1717 s11 -> n1689
node n1718 s13 -> n1690
node n1719 s1 -> n1693
node n1720 s4 -> n1725
node n1721 s5 -> n1751
node n1722 s22 word last
node n1723 s0 -> n1723
node n1724 s1 last -> n1693
node n1725 s3 -> n1723
node n1726 s1 -> n1693
node n1727 s4 -> n1725
node n1728 s5 last -> n1751
node n1729 s4 -> n1729
node n1730 s5 -> n1751
node n1731 s3 -> n1723
node n1732 s1 last -> n1693
node n1733 s4 -> n1729
node n1734 s5 -> n1751
node n1735 s3 -> n1723
node n1736 s1 -> n1693
node n1737 s6 last -> n1745
node n1738 s3 -> n1723
node n1739 s1 -> n1693
node n1740 s4 -> n1725
node n1741 s5 -> n1751
node n1742 s6 last -> n1745
node n1743 s0 last -> n1738
node n1744 s0 last -> n1743
node n1745 s0 last -> n1744
node n1746 s4 -> n1733
node n1747 s5 -> n1751
node n1748 s3 -> n1723
node n1749 s1 -> n1693
node n1750 s6 last -> n1745
node n1751 s4 -> n1746
node n1752 s5 -> n1751
node n1753 s3 -> n1723
node n1754 s1 -> n1693
node n1755 s6 last -> n1745
node n1756 s4 -> n1756
node n1757 s5 -> n1785
node n1758 s3 -> n1700
node n1759 s20 -> n1467
node n1760 s8 -> n1683
node n1761 s11 -> n1689
node n1762 s13 -> n1690
node n1763 s1 -> n1693
node n1764 s22 word last
node n1765 s4 -> n1756
node n1766 s5 -> n1785
node n1767 s3 -> n1700
node n1768 s20 -> n1467
node n1769 s8 -> n1683
node n1770 s11 -> n1689
node n1771 s13 -> n1690
node n1772 s1 -> n1693
node n1773 s6 -> n1745
node n1774 s22 word last
node n1775 s4 -> n1765
node n1776 s5 -> n1785
node n1777 s3 -> n1700
node n1778 s20 -> n1467
node n1779 s8 -> n1683
node n1780 s11 -> n1689
node n1781 s13 -> n1690
node n1782 s1 -> n1693
node n1783 s6 -> n1745
node n1784 s22 word last
node n1785 s4 -> n1775
node n1786 s5 -> n1785
node n1787 s3 -> n1700
node n1788 s20 -> n1467
node n1789 s8 -> n1683
node n1790 s11 -> n1689
node n1791 s13 -> n1690
node n1792 s1 -> n1693
node n1793 s6 -> n1745
node n1794 s22 word last
node n1795 s4 -> n1756
node n1796 s5 -> n1785
node n1797 s3 -> n1700
node n1798 s20 -> n1467
node n1799 s8 -> n1683
node n1800 s11 -> n1689
node n1801 s13 -> n1690
node n1802 s6 -> n1817
node n1803 s1 -> n1693
node n1804 s22 word last
node n1805 s3 -> n1700
node n1806 s20 -> n1467
node n1807 s8 -> n1683
node n1808 s11 -> n1689
node n1809 s13 -> n1690
node n1810 s6 -> n1817
node n1811 s1 -> n1693
node n1812 s4 -> n1725
node n1813 s5 -> n1751
node n1814 s22 word last
node n1815 s0 last -> n1805
node n1816 s0 last -> n1815
node n1817 s0 last -> n1816
node n1818 s4 -> n1795
node n1819 s5 -> n1828
node n1820 s3 -> n1700
node n1821 s20 -> n1467
node n1822 s8 -> n1683
node n1823 s11 -> n1689
node n1824 s13 -> n1690
node n1825 s6 -> n1817
node n1826 s1 -> n1693
node n1827 s22 word last
node n1828 s4 -> n1775
node n1829 s5 -> n1785
node n1830 s3 -> n1700
node n1831 s20 -> n1467
node n1832 s8 -> n1683
node n1833 s11 -> n1689
node n1834 s13 -> n1690
node n1835 s6 -> n1817
node n1836 s1 -> n1693
node n1837 s22 word last
node n1838 s4 -> n1818
node n1839 s5 -> n1858
node n1840 s3 -> n1700
node n1841 s20 -> n1467
node n1842 s8 -> n1683
node n1843 s11 -> n1689
node n1844 s13 -> n1690
node n1845 s6 -> n1817
node n1846 s1 -> n1693
node n1847 s22 word last
node n1848 s4 -> n1765
node n1849 s5 -> n1785
node n1850 s3 -> n1700
node n1851 s20 -> n1467
node n1852 s8 -> n1683
node n1853 s11 -> n1689
node n1854 s13 -> n1690
node n1855 s6 -> n1817
node n1856 s1 -> n1693
node n1857 s22 word last
node n1858 s4 -> n1848
node n1859 s5 -> n1828
node n1860 s3 -> n1700
node n1861 s20 -> n1467
node n1862 s8 -> n1683
node n1863 s11 -> n1689
node n1864 s13 -> n1690
node n1865 s6 -> n1817
node n1866 s1 -> n1693
node n1867 s22 word last
node n1868 s20 -> n1467
node n1869 s8 -> n1683
node n1870 s11 -> n1689
node n1871 s13 -> n1690
node n1872 s1 -> n1693
node n1873 s0 -> n1875
node n1874 s22 word last
node n1875 s1 -> n1693
node n1876 s0 last -> n1875
node n1877 s0 -> n1868
node n1878 s1 last -> n1693
node n1879 s0 -> n1877
node n1880 s2 -> n1708
node n1881 s20 -> n1467
node n1882 s8 -> n1683
node n1883 s11 -> n1689
node n1884 s13 -> n1690
node n1885 s22 word last
node n1886 s0 -> n1886
node n1887 s1 last -> n1467
node n1888 s0 -> n1886
node n1889 s1 -> n1467
node n1890 s22 word last
node n1891 s0 -> n1888
node n1892 s1 last -> n1467
node n1893 s0 -> n1891
node n1894 s2 -> n1897
node n1895 s1 -> n1467
node n1896 s22 word last
node n1897 s2 -> n1467
node n1898 s22 word last
node n1899 s3 -> n1893
node n1900 s1 -> n1467
node n1901 s4 -> n1906
node n1902 s5 -> n1932
node n1903 s22 word last
node n1904 s0 -> n1904
node n1905 s1 last -> n1467
node n1906 s3 -> n1904
node n1907 s1 -> n1467
node n1908 s4 -> n1906
node n1909 s5 last -> n1932
node n1910 s4 -> n1910
node n1911 s5 -> n1932
node n1912 s3 -> n1904
node n1913 s1 last -> n1467
node n1914 s4 -> n1910
node n1915 s5 -> n1932
node n1916 s3 -> n1904
node n1917 s1 -> n1467
node n1918 s6 last -> n1926
node n1919 s3 -> n1904
node n1920 s1 -> n1467
node n1921 s4 -> n1906
node n1922 s5 -> n1932
node n1923 s6 last -> n1926
node n1924 s0 last -> n1919
node n1925 s0 last -> n1924
node n1926 s0 last -> n1925
node n1927 s4 -> n1914
node n1928 s5 -> n1932
node n1929 s3 -> n1904
node n1930 s1 -> n1467
node n1931 s6 last -> n1926
node n1932 s4 -> n1927
node n1933 s5 -> n1932
node n1934 s3 -> n1904
node n1935 s1 -> n1467
node n1936 s6 last -> n1926
node n1937 s4 -> n1899
node n1938 s5 -> n1991
node n1939 s3 -> n2016
node n1940 s22 word last
node n1941 s4 -> n1941
node n1942 s5 -> n1958
node n1943 s3 -> n1893
node n1944 s1 -> n1467
node n1945 s22 word last
node n1946 s4 -> n1941
node n1947 s5 -> n1958
node n1948 s3 -> n1893
node n1949 s1 -> n1467
node n1950 s6 -> n1926
node n1951 s22 word last
node n1952 s4 -> n1946
node n1953 s5 -> n1958
node n1954 s3 -> n1893
node n1955 s1 -> n1467
node n1956 s6 -> n1926
node n1957 s22 word last
node n1958 s4 -> n1952
node n1959 s5 -> n1958
node n1960 s3 -> n1893
node n1961 s1 -> n1467
node n1962 s6 -> n1926
node n1963 s22 word last
node n1964 s4 -> n1941
node n1965 s5 -> n1958
node n1966 s3 -> n1893
node n1967 s6 -> n1978
node n1968 s1 -> n1467
node n1969 s22 word last
node n1970 s3 -> n1893
node n1971 s6 -> n1978
node n1972 s1 -> n1467
node n1973 s4 -> n1906
node n1974 s5 -> n1932
node n1975 s22 word last
node n1976 s0 last -> n1970
node n1977 s0 last -> n1976
node n1978 s0 last -> n1977
node n1979 s4 -> n1964
node n1980 s5 -> n1985
node n1981 s3 -> n1893
node n1982 s6 -> n1978
node n1983 s1 -> n1467
node n1984 s22 word last
node n1985 s4 -> n1952
node n1986 s5 -> n1958
node n1987 s3 -> n1893
node n1988 s6 -> n1978
node n1989 s1 -> n1467
node n1990 s22 word last
node n1991 s4 -> n1979
node n1992 s5 -> n2003
node n1993 s3 -> n1893
node n1994 s6 -> n1978
node n1995 s1 -> n1467
node n1996 s22 word last
node n1997 s4 -> n1946
node n1998 s5 -> n1958
node n1999 s3 -> n1893
node n2000 s6 -> n1978
node n2001 s1 -> n1467
node n2002 s22 word last
node n2003 s4 -> n1997
node n2004 s5 -> n1985
node n2005 s3 -> n1893
node n2006 s6 -> n1978
node n2007 s1 -> n1467
node n2008 s22 word last
node n2009 s1 -> n1467
node n2010 s0 -> n2012
node n2011 s22 word last
node n2012 s1 -> n1467
node n2013 s0 last -> n2012
node n2014 s0 -> n2009
node n2015 s1 last -> n1467
node n2016 s0 -> n2014
node n2017 s2 -> n1897
node n2018 s22 word last
node n2019 s8 -> n1937
node n2020 s10 -> n2025
node n2021 s4 -> n1899
node n2022 s5 -> n1991
node n2023 s3 -> n2016
node n2024 s22 word last
node n2025 s9 -> n1937
node n2026 s4 -> n1899
node n2027 s5 -> n1991
node n2028 s3 -> n2016
node n2029 s22 word last
node n2030 s10 last -> n2025
node n2031 s8 last -> n2030
node n2032 s12 last -> n1937
node n2033 s14 last -> n1937
node n2034 s15 last -> n2033
node n2035 s17 last -> n2033
node n2036 s18 last -> n2033
node n2037 s13 last -> n2036
