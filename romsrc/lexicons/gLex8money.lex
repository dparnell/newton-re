# gLex8money: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 "0123456789" "t0"
set s1 "BKM" "t1"
set s2 "-" "t2"
set s3 "." "t3"
set s4 "0" "t4"
set s5 "123456789" "t5"
set s6 "," "t6"
set s7 "£¥" "t7"
set s8 "F" "t8"
set s9 "r" "t9"
set s10 "s" "t10"
set s11 "M" "t11"
set s12 "D" "t12"
set s13 "$" "t13"
set s14 "U" "t14"
set s15 "A" "t15"
set s16 "S" "t16"
set s17 "N" "t17"
set s18 "C" "t18"
set s19 "c¢" "t19"
set s20 "+" "t20"
set s21 ")" "t21"
set s22 "(" "t22"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s7 word -> n60
node n1 s8 word -> n126
node n2 s10 -> n215
node n3 s12 -> n216
node n4 s15 -> n218
node n5 s14 -> n219
node n6 s18 -> n221
node n7 s13 word -> n288
node n8 s4 word -> n430
node n9 s5 word -> n545
node n10 s3 word -> n582
node n11 s20 word -> n753
node n12 s2 word -> n1311
node n13 s22 -> n1900
node n14 s19 word last
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
node n127 s9 word -> n211
node n128 s4 word -> n23
node n129 s5 word -> n105
node n130 s3 word last -> n124
node n131 s3 word -> n19
node n132 s4 word -> n135
node n133 s5 word -> n153
node n134 s1 word last
node n135 s3 -> n27
node n136 s4 -> n135
node n137 s5 -> n153
node n138 s1 word last
node n139 s4 -> n139
node n140 s5 -> n153
node n141 s3 -> n27
node n142 s1 word last
node n143 s4 -> n139
node n144 s5 -> n153
node n145 s3 -> n27
node n146 s6 -> n49
node n147 s1 word last
node n148 s4 -> n143
node n149 s5 -> n153
node n150 s3 -> n27
node n151 s6 -> n49
node n152 s1 word last
node n153 s4 -> n148
node n154 s5 -> n153
node n155 s3 -> n27
node n156 s6 -> n49
node n157 s1 word last
node n158 s4 word -> n131
node n159 s5 word -> n196
node n160 s3 word -> n124
node n161 s2 word last -> n22
node n162 s4 word -> n162
node n163 s5 word -> n176
node n164 s3 word -> n19
node n165 s1 word last
node n166 s4 word -> n162
node n167 s5 word -> n176
node n168 s3 word -> n19
node n169 s6 -> n49
node n170 s1 word last
node n171 s4 word -> n166
node n172 s5 word -> n176
node n173 s3 word -> n19
node n174 s6 -> n49
node n175 s1 word last
node n176 s4 word -> n171
node n177 s5 word -> n176
node n178 s3 word -> n19
node n179 s6 -> n49
node n180 s1 word last
node n181 s4 word -> n162
node n182 s5 word -> n176
node n183 s3 word -> n19
node n184 s6 -> n94
node n185 s1 word last
node n186 s4 word -> n181
node n187 s5 word -> n191
node n188 s3 word -> n19
node n189 s6 -> n94
node n190 s1 word last
node n191 s4 word -> n171
node n192 s5 word -> n176
node n193 s3 word -> n19
node n194 s6 -> n94
node n195 s1 word last
node n196 s4 word -> n186
node n197 s5 word -> n206
node n198 s3 word -> n19
node n199 s6 -> n94
node n200 s1 word last
node n201 s4 word -> n166
node n202 s5 word -> n176
node n203 s3 word -> n19
node n204 s6 -> n94
node n205 s1 word last
node n206 s4 word -> n201
node n207 s5 word -> n191
node n208 s3 word -> n19
node n209 s6 -> n94
node n210 s1 word last
node n211 s3 word -> n158
node n212 s4 word -> n23
node n213 s5 word last -> n105
node n214 s9 word last -> n211
node n215 s8 last -> n214
node n216 s11 word last -> n60
node n217 s13 word last -> n60
node n218 s14 last -> n217
node n219 s16 last -> n217
node n220 s17 last -> n217
node n221 s12 last -> n220
node n222 s17 word last
node n223 s12 last -> n222
node n224 s18 -> n223
node n225 s0 -> n229
node n226 s14 -> n231
node n227 s15 -> n235
node n228 s1 word last -> n232
node n229 s0 -> n229
node n230 s1 word last -> n232
node n231 s16 word last
node n232 s14 -> n231
node n233 s15 -> n235
node n234 s18 last -> n223
node n235 s14 word last
node n236 s0 word -> n224
node n237 s1 word last -> n232
node n238 s0 -> n236
node n239 s18 -> n223
node n240 s14 -> n231
node n241 s15 -> n235
node n242 s1 word -> n232
node n243 s2 word last -> n244
node n244 s14 -> n231
node n245 s15 -> n235
node n246 s2 word -> n232
node n247 s18 last -> n223
node n248 s3 word -> n238
node n249 s18 -> n223
node n250 s4 -> n257
node n251 s5 -> n283
node n252 s14 -> n231
node n253 s15 -> n235
node n254 s1 word last -> n232
node n255 s0 -> n255
node n256 s1 word last -> n232
node n257 s3 -> n255
node n258 s4 -> n257
node n259 s5 -> n283
node n260 s1 word last -> n232
node n261 s4 -> n261
node n262 s5 -> n283
node n263 s3 -> n255
node n264 s1 word last -> n232
node n265 s4 -> n261
node n266 s5 -> n283
node n267 s3 -> n255
node n268 s6 -> n277
node n269 s1 word last -> n232
node n270 s3 -> n255
node n271 s4 -> n257
node n272 s5 -> n283
node n273 s6 -> n277
node n274 s1 word last -> n232
node n275 s0 last -> n270
node n276 s0 last -> n275
node n277 s0 last -> n276
node n278 s4 -> n265
node n279 s5 -> n283
node n280 s3 -> n255
node n281 s6 -> n277
node n282 s1 word last -> n232
node n283 s4 -> n278
node n284 s5 -> n283
node n285 s3 -> n255
node n286 s6 -> n277
node n287 s1 word last -> n232
node n288 s4 word -> n248
node n289 s5 word -> n360
node n290 s3 word -> n393
node n291 s18 -> n223
node n292 s14 -> n231
node n293 s15 last -> n235
node n294 s4 word -> n294
node n295 s5 word -> n317
node n296 s3 word -> n238
node n297 s18 -> n223
node n298 s14 -> n231
node n299 s15 -> n235
node n300 s1 word last -> n232
node n301 s4 word -> n294
node n302 s5 word -> n317
node n303 s3 word -> n238
node n304 s18 -> n223
node n305 s6 -> n277
node n306 s14 -> n231
node n307 s15 -> n235
node n308 s1 word last -> n232
node n309 s4 word -> n301
node n310 s5 word -> n317
node n311 s3 word -> n238
node n312 s18 -> n223
node n313 s6 -> n277
node n314 s14 -> n231
node n315 s15 -> n235
node n316 s1 word last -> n232
node n317 s4 word -> n309
node n318 s5 word -> n317
node n319 s3 word -> n238
node n320 s18 -> n223
node n321 s6 -> n277
node n322 s14 -> n231
node n323 s15 -> n235
node n324 s1 word last -> n232
node n325 s4 word -> n294
node n326 s5 word -> n317
node n327 s3 word -> n238
node n328 s18 -> n223
node n329 s6 -> n343
node n330 s14 -> n231
node n331 s15 -> n235
node n332 s1 word last -> n232
node n333 s3 word -> n238
node n334 s18 -> n223
node n335 s6 -> n343
node n336 s4 -> n257
node n337 s5 -> n283
node n338 s14 -> n231
node n339 s15 -> n235
node n340 s1 word last -> n232
node n341 s0 word last -> n333
node n342 s0 last -> n341
node n343 s0 last -> n342
node n344 s4 word -> n325
node n345 s5 word -> n352
node n346 s3 word -> n238
node n347 s18 -> n223
node n348 s6 -> n343
node n349 s14 -> n231
node n350 s15 -> n235
node n351 s1 word last -> n232
node n352 s4 word -> n309
node n353 s5 word -> n317
node n354 s3 word -> n238
node n355 s18 -> n223
node n356 s6 -> n343
node n357 s14 -> n231
node n358 s15 -> n235
node n359 s1 word last -> n232
node n360 s4 word -> n344
node n361 s5 word -> n376
node n362 s3 word -> n238
node n363 s18 -> n223
node n364 s6 -> n343
node n365 s14 -> n231
node n366 s15 -> n235
node n367 s1 word last -> n232
node n368 s4 word -> n301
node n369 s5 word -> n317
node n370 s3 word -> n238
node n371 s18 -> n223
node n372 s6 -> n343
node n373 s14 -> n231
node n374 s15 -> n235
node n375 s1 word last -> n232
node n376 s4 word -> n368
node n377 s5 word -> n352
node n378 s3 word -> n238
node n379 s18 -> n223
node n380 s6 -> n343
node n381 s14 -> n231
node n382 s15 -> n235
node n383 s1 word last -> n232
node n384 s18 -> n223
node n385 s0 -> n389
node n386 s14 -> n231
node n387 s15 -> n235
node n388 s1 word last -> n232
node n389 s0 -> n389
node n390 s1 word last -> n232
node n391 s0 word -> n384
node n392 s1 word last -> n232
node n393 s0 -> n391
node n394 s18 -> n223
node n395 s14 -> n231
node n396 s15 -> n235
node n397 s2 word last -> n244
node n398 s3 word last
node n399 s9 word -> n398
node n400 s8 word last
node n401 s8 word -> n399
node n402 s10 -> n408
node n403 s0 -> n409
node n404 s1 word -> n411
node n405 s12 -> n415
node n406 s19 word last
node n407 s9 word last -> n398
node n408 s8 last -> n407
node n409 s0 -> n409
node n410 s1 word last -> n411
node n411 s8 word -> n399
node n412 s10 -> n408
node n413 s12 -> n415
node n414 s19 word last
node n415 s11 word last
node n416 s0 word -> n401
node n417 s1 word last -> n411
node n418 s0 -> n416
node n419 s2 word -> n425
node n420 s8 word -> n399
node n421 s10 -> n408
node n422 s1 word -> n411
node n423 s12 -> n415
node n424 s19 word last
node n425 s8 word -> n399
node n426 s10 -> n408
node n427 s2 word -> n411
node n428 s12 -> n415
node n429 s19 word last
node n430 s3 word -> n418
node n431 s8 word -> n399
node n432 s10 -> n408
node n433 s1 word -> n411
node n434 s4 -> n440
node n435 s5 -> n466
node n436 s12 -> n415
node n437 s19 word last
node n438 s0 -> n438
node n439 s1 word last -> n411
node n440 s3 -> n438
node n441 s1 word -> n411
node n442 s4 -> n440
node n443 s5 last -> n466
node n444 s4 -> n444
node n445 s5 -> n466
node n446 s3 -> n438
node n447 s1 word last -> n411
node n448 s4 -> n444
node n449 s5 -> n466
node n450 s3 -> n438
node n451 s1 word -> n411
node n452 s6 last -> n460
node n453 s3 -> n438
node n454 s1 word -> n411
node n455 s4 -> n440
node n456 s5 -> n466
node n457 s6 last -> n460
node n458 s0 last -> n453
node n459 s0 last -> n458
node n460 s0 last -> n459
node n461 s4 -> n448
node n462 s5 -> n466
node n463 s3 -> n438
node n464 s1 word -> n411
node n465 s6 last -> n460
node n466 s4 -> n461
node n467 s5 -> n466
node n468 s3 -> n438
node n469 s1 word -> n411
node n470 s6 last -> n460
node n471 s4 word -> n471
node n472 s5 word -> n497
node n473 s3 word -> n418
node n474 s8 word -> n399
node n475 s10 -> n408
node n476 s1 word -> n411
node n477 s12 -> n415
node n478 s19 word last
node n479 s4 word -> n471
node n480 s5 word -> n497
node n481 s3 word -> n418
node n482 s8 word -> n399
node n483 s10 -> n408
node n484 s1 word -> n411
node n485 s6 -> n460
node n486 s12 -> n415
node n487 s19 word last
node n488 s4 word -> n479
node n489 s5 word -> n497
node n490 s3 word -> n418
node n491 s8 word -> n399
node n492 s10 -> n408
node n493 s1 word -> n411
node n494 s6 -> n460
node n495 s12 -> n415
node n496 s19 word last
node n497 s4 word -> n488
node n498 s5 word -> n497
node n499 s3 word -> n418
node n500 s8 word -> n399
node n501 s10 -> n408
node n502 s1 word -> n411
node n503 s6 -> n460
node n504 s12 -> n415
node n505 s19 word last
node n506 s4 word -> n471
node n507 s5 word -> n497
node n508 s3 word -> n418
node n509 s8 word -> n399
node n510 s10 -> n408
node n511 s6 -> n526
node n512 s1 word -> n411
node n513 s12 -> n415
node n514 s19 word last
node n515 s3 word -> n418
node n516 s8 word -> n399
node n517 s10 -> n408
node n518 s6 -> n526
node n519 s1 word -> n411
node n520 s4 -> n440
node n521 s5 -> n466
node n522 s12 -> n415
node n523 s19 word last
node n524 s0 word last -> n515
node n525 s0 last -> n524
node n526 s0 last -> n525
node n527 s4 word -> n506
node n528 s5 word -> n536
node n529 s3 word -> n418
node n530 s8 word -> n399
node n531 s10 -> n408
node n532 s6 -> n526
node n533 s1 word -> n411
node n534 s12 -> n415
node n535 s19 word last
node n536 s4 word -> n488
node n537 s5 word -> n497
node n538 s3 word -> n418
node n539 s8 word -> n399
node n540 s10 -> n408
node n541 s6 -> n526
node n542 s1 word -> n411
node n543 s12 -> n415
node n544 s19 word last
node n545 s4 word -> n527
node n546 s5 word -> n563
node n547 s3 word -> n418
node n548 s8 word -> n399
node n549 s10 -> n408
node n550 s6 -> n526
node n551 s1 word -> n411
node n552 s12 -> n415
node n553 s19 word last
node n554 s4 word -> n479
node n555 s5 word -> n497
node n556 s3 word -> n418
node n557 s8 word -> n399
node n558 s10 -> n408
node n559 s6 -> n526
node n560 s1 word -> n411
node n561 s12 -> n415
node n562 s19 word last
node n563 s4 word -> n554
node n564 s5 word -> n536
node n565 s3 word -> n418
node n566 s8 word -> n399
node n567 s10 -> n408
node n568 s6 -> n526
node n569 s1 word -> n411
node n570 s12 -> n415
node n571 s19 word last
node n572 s8 word -> n399
node n573 s10 -> n408
node n574 s1 word -> n411
node n575 s0 -> n578
node n576 s12 -> n415
node n577 s19 word last
node n578 s1 word -> n411
node n579 s0 last -> n578
node n580 s0 word -> n572
node n581 s1 word last -> n411
node n582 s0 -> n580
node n583 s2 word -> n425
node n584 s8 word -> n399
node n585 s10 -> n408
node n586 s12 -> n415
node n587 s19 word last
node n588 s0 -> n588
node n589 s1 word last -> n232
node n590 s0 -> n588
node n591 s14 -> n231
node n592 s15 -> n235
node n593 s18 -> n223
node n594 s1 word last -> n232
node n595 s0 word -> n590
node n596 s1 word last -> n232
node n597 s0 -> n595
node n598 s14 -> n231
node n599 s15 -> n235
node n600 s18 -> n223
node n601 s1 word -> n232
node n602 s2 word last -> n244
node n603 s3 word -> n597
node n604 s4 -> n612
node n605 s5 -> n638
node n606 s14 -> n231
node n607 s15 -> n235
node n608 s18 -> n223
node n609 s1 word last -> n232
node n610 s0 -> n610
node n611 s1 word last -> n232
node n612 s3 -> n610
node n613 s4 -> n612
node n614 s5 -> n638
node n615 s1 word last -> n232
node n616 s4 -> n616
node n617 s5 -> n638
node n618 s3 -> n610
node n619 s1 word last -> n232
node n620 s4 -> n616
node n621 s5 -> n638
node n622 s3 -> n610
node n623 s6 -> n632
node n624 s1 word last -> n232
node n625 s3 -> n610
node n626 s4 -> n612
node n627 s5 -> n638
node n628 s6 -> n632
node n629 s1 word last -> n232
node n630 s0 last -> n625
node n631 s0 last -> n630
node n632 s0 last -> n631
node n633 s4 -> n620
node n634 s5 -> n638
node n635 s3 -> n610
node n636 s6 -> n632
node n637 s1 word last -> n232
node n638 s4 -> n633
node n639 s5 -> n638
node n640 s3 -> n610
node n641 s6 -> n632
node n642 s1 word last -> n232
node n643 s4 word -> n603
node n644 s5 word -> n715
node n645 s3 word -> n748
node n646 s14 -> n231
node n647 s15 -> n235
node n648 s18 last -> n223
node n649 s4 word -> n649
node n650 s5 word -> n672
node n651 s3 word -> n597
node n652 s14 -> n231
node n653 s15 -> n235
node n654 s18 -> n223
node n655 s1 word last -> n232
node n656 s4 word -> n649
node n657 s5 word -> n672
node n658 s3 word -> n597
node n659 s6 -> n632
node n660 s14 -> n231
node n661 s15 -> n235
node n662 s18 -> n223
node n663 s1 word last -> n232
node n664 s4 word -> n656
node n665 s5 word -> n672
node n666 s3 word -> n597
node n667 s6 -> n632
node n668 s14 -> n231
node n669 s15 -> n235
node n670 s18 -> n223
node n671 s1 word last -> n232
node n672 s4 word -> n664
node n673 s5 word -> n672
node n674 s3 word -> n597
node n675 s6 -> n632
node n676 s14 -> n231
node n677 s15 -> n235
node n678 s18 -> n223
node n679 s1 word last -> n232
node n680 s4 word -> n649
node n681 s5 word -> n672
node n682 s3 word -> n597
node n683 s6 -> n698
node n684 s14 -> n231
node n685 s15 -> n235
node n686 s18 -> n223
node n687 s1 word last -> n232
node n688 s3 word -> n597
node n689 s6 -> n698
node n690 s4 -> n612
node n691 s5 -> n638
node n692 s14 -> n231
node n693 s15 -> n235
node n694 s18 -> n223
node n695 s1 word last -> n232
node n696 s0 word last -> n688
node n697 s0 last -> n696
node n698 s0 last -> n697
node n699 s4 word -> n680
node n700 s5 word -> n707
node n701 s3 word -> n597
node n702 s6 -> n698
node n703 s14 -> n231
node n704 s15 -> n235
node n705 s18 -> n223
node n706 s1 word last -> n232
node n707 s4 word -> n664
node n708 s5 word -> n672
node n709 s3 word -> n597
node n710 s6 -> n698
node n711 s14 -> n231
node n712 s15 -> n235
node n713 s18 -> n223
node n714 s1 word last -> n232
node n715 s4 word -> n699
node n716 s5 word -> n731
node n717 s3 word -> n597
node n718 s6 -> n698
node n719 s14 -> n231
node n720 s15 -> n235
node n721 s18 -> n223
node n722 s1 word last -> n232
node n723 s4 word -> n656
node n724 s5 word -> n672
node n725 s3 word -> n597
node n726 s6 -> n698
node n727 s14 -> n231
node n728 s15 -> n235
node n729 s18 -> n223
node n730 s1 word last -> n232
node n731 s4 word -> n723
node n732 s5 word -> n707
node n733 s3 word -> n597
node n734 s6 -> n698
node n735 s14 -> n231
node n736 s15 -> n235
node n737 s18 -> n223
node n738 s1 word last -> n232
node n739 s0 -> n739
node n740 s1 word last -> n232
node n741 s0 -> n739
node n742 s14 -> n231
node n743 s15 -> n235
node n744 s18 -> n223
node n745 s1 word last -> n232
node n746 s0 word -> n741
node n747 s1 word last -> n232
node n748 s0 -> n746
node n749 s14 -> n231
node n750 s15 -> n235
node n751 s18 -> n223
node n752 s2 word last -> n244
node n753 s13 word -> n643
node n754 s4 word -> n782
node n755 s5 word -> n897
node n756 s3 word -> n934
node n757 s8 word -> n1050
node n758 s10 -> n1139
node n759 s12 -> n1140
node n760 s7 word -> n984
node n761 s15 -> n1142
node n762 s14 -> n1143
node n763 s18 -> n1145
node n764 s19 word last
node n765 s0 -> n765
node n766 s1 word last -> n411
node n767 s0 -> n765
node n768 s12 -> n415
node n769 s8 word -> n399
node n770 s10 -> n408
node n771 s1 word -> n411
node n772 s19 word last
node n773 s0 word -> n767
node n774 s1 word last -> n411
node n775 s0 -> n773
node n776 s12 -> n415
node n777 s8 word -> n399
node n778 s10 -> n408
node n779 s1 word -> n411
node n780 s2 word -> n425
node n781 s19 word last
node n782 s3 word -> n775
node n783 s4 -> n792
node n784 s5 -> n818
node n785 s12 -> n415
node n786 s8 word -> n399
node n787 s10 -> n408
node n788 s1 word -> n411
node n789 s19 word last
node n790 s0 -> n790
node n791 s1 word last -> n411
node n792 s3 -> n790
node n793 s4 -> n792
node n794 s5 -> n818
node n795 s1 word last -> n411
node n796 s4 -> n796
node n797 s5 -> n818
node n798 s3 -> n790
node n799 s1 word last -> n411
node n800 s4 -> n796
node n801 s5 -> n818
node n802 s3 -> n790
node n803 s6 -> n812
node n804 s1 word last -> n411
node n805 s3 -> n790
node n806 s4 -> n792
node n807 s5 -> n818
node n808 s6 -> n812
node n809 s1 word last -> n411
node n810 s0 last -> n805
node n811 s0 last -> n810
node n812 s0 last -> n811
node n813 s4 -> n800
node n814 s5 -> n818
node n815 s3 -> n790
node n816 s6 -> n812
node n817 s1 word last -> n411
node n818 s4 -> n813
node n819 s5 -> n818
node n820 s3 -> n790
node n821 s6 -> n812
node n822 s1 word last -> n411
node n823 s4 word -> n823
node n824 s5 word -> n849
node n825 s3 word -> n775
node n826 s12 -> n415
node n827 s8 word -> n399
node n828 s10 -> n408
node n829 s1 word -> n411
node n830 s19 word last
node n831 s4 word -> n823
node n832 s5 word -> n849
node n833 s3 word -> n775
node n834 s6 -> n812
node n835 s12 -> n415
node n836 s8 word -> n399
node n837 s10 -> n408
node n838 s1 word -> n411
node n839 s19 word last
node n840 s4 word -> n831
node n841 s5 word -> n849
node n842 s3 word -> n775
node n843 s6 -> n812
node n844 s12 -> n415
node n845 s8 word -> n399
node n846 s10 -> n408
node n847 s1 word -> n411
node n848 s19 word last
node n849 s4 word -> n840
node n850 s5 word -> n849
node n851 s3 word -> n775
node n852 s6 -> n812
node n853 s12 -> n415
node n854 s8 word -> n399
node n855 s10 -> n408
node n856 s1 word -> n411
node n857 s19 word last
node n858 s4 word -> n823
node n859 s5 word -> n849
node n860 s3 word -> n775
node n861 s6 -> n878
node n862 s12 -> n415
node n863 s8 word -> n399
node n864 s10 -> n408
node n865 s1 word -> n411
node n866 s19 word last
node n867 s3 word -> n775
node n868 s6 -> n878
node n869 s4 -> n792
node n870 s5 -> n818
node n871 s12 -> n415
node n872 s8 word -> n399
node n873 s10 -> n408
node n874 s1 word -> n411
node n875 s19 word last
node n876 s0 word last -> n867
node n877 s0 last -> n876
node n878 s0 last -> n877
node n879 s4 word -> n858
node n880 s5 word -> n888
node n881 s3 word -> n775
node n882 s6 -> n878
node n883 s12 -> n415
node n884 s8 word -> n399
node n885 s10 -> n408
node n886 s1 word -> n411
node n887 s19 word last
node n888 s4 word -> n840
node n889 s5 word -> n849
node n890 s3 word -> n775
node n891 s6 -> n878
node n892 s12 -> n415
node n893 s8 word -> n399
node n894 s10 -> n408
node n895 s1 word -> n411
node n896 s19 word last
node n897 s4 word -> n879
node n898 s5 word -> n915
node n899 s3 word -> n775
node n900 s6 -> n878
node n901 s12 -> n415
node n902 s8 word -> n399
node n903 s10 -> n408
node n904 s1 word -> n411
node n905 s19 word last
node n906 s4 word -> n831
node n907 s5 word -> n849
node n908 s3 word -> n775
node n909 s6 -> n878
node n910 s12 -> n415
node n911 s8 word -> n399
node n912 s10 -> n408
node n913 s1 word -> n411
node n914 s19 word last
node n915 s4 word -> n906
node n916 s5 word -> n888
node n917 s3 word -> n775
node n918 s6 -> n878
node n919 s12 -> n415
node n920 s8 word -> n399
node n921 s10 -> n408
node n922 s1 word -> n411
node n923 s19 word last
node n924 s0 -> n924
node n925 s1 word last -> n411
node n926 s0 -> n924
node n927 s12 -> n415
node n928 s8 word -> n399
node n929 s10 -> n408
node n930 s1 word -> n411
node n931 s19 word last
node n932 s0 word -> n926
node n933 s1 word last -> n411
node n934 s0 -> n932
node n935 s12 -> n415
node n936 s8 word -> n399
node n937 s10 -> n408
node n938 s2 word -> n425
node n939 s19 word last
node n940 s0 -> n940
node n941 s1 word last
node n942 s0 word -> n940
node n943 s1 word last
node n944 s0 -> n942
node n945 s2 word -> n22
node n946 s1 word last
node n947 s3 word -> n944
node n948 s4 -> n953
node n949 s5 -> n979
node n950 s1 word last
node n951 s0 -> n951
node n952 s1 word last
node n953 s3 -> n951
node n954 s4 -> n953
node n955 s5 -> n979
node n956 s1 word last
node n957 s4 -> n957
node n958 s5 -> n979
node n959 s3 -> n951
node n960 s1 word last
node n961 s4 -> n957
node n962 s5 -> n979
node n963 s3 -> n951
node n964 s6 -> n973
node n965 s1 word last
node n966 s3 -> n951
node n967 s4 -> n953
node n968 s5 -> n979
node n969 s6 -> n973
node n970 s1 word last
node n971 s0 last -> n966
node n972 s0 last -> n971
node n973 s0 last -> n972
node n974 s4 -> n961
node n975 s5 -> n979
node n976 s3 -> n951
node n977 s6 -> n973
node n978 s1 word last
node n979 s4 -> n974
node n980 s5 -> n979
node n981 s3 -> n951
node n982 s6 -> n973
node n983 s1 word last
node n984 s4 word -> n947
node n985 s5 word -> n1029
node n986 s3 word last -> n1048
node n987 s4 word -> n987
node n988 s5 word -> n1001
node n989 s3 word -> n944
node n990 s1 word last
node n991 s4 word -> n987
node n992 s5 word -> n1001
node n993 s3 word -> n944
node n994 s6 -> n973
node n995 s1 word last
node n996 s4 word -> n991
node n997 s5 word -> n1001
node n998 s3 word -> n944
node n999 s6 -> n973
node n1000 s1 word last
node n1001 s4 word -> n996
node n1002 s5 word -> n1001
node n1003 s3 word -> n944
node n1004 s6 -> n973
node n1005 s1 word last
node n1006 s4 word -> n987
node n1007 s5 word -> n1001
node n1008 s3 word -> n944
node n1009 s6 -> n1018
node n1010 s1 word last
node n1011 s3 word -> n944
node n1012 s6 -> n1018
node n1013 s4 -> n953
node n1014 s5 -> n979
node n1015 s1 word last
node n1016 s0 word last -> n1011
node n1017 s0 last -> n1016
node n1018 s0 last -> n1017
node n1019 s4 word -> n1006
node n1020 s5 word -> n1024
node n1021 s3 word -> n944
node n1022 s6 -> n1018
node n1023 s1 word last
node n1024 s4 word -> n996
node n1025 s5 word -> n1001
node n1026 s3 word -> n944
node n1027 s6 -> n1018
node n1028 s1 word last
node n1029 s4 word -> n1019
node n1030 s5 word -> n1039
node n1031 s3 word -> n944
node n1032 s6 -> n1018
node n1033 s1 word last
node n1034 s4 word -> n991
node n1035 s5 word -> n1001
node n1036 s3 word -> n944
node n1037 s6 -> n1018
node n1038 s1 word last
node n1039 s4 word -> n1034
node n1040 s5 word -> n1024
node n1041 s3 word -> n944
node n1042 s6 -> n1018
node n1043 s1 word last
node n1044 s0 -> n1044
node n1045 s1 word last
node n1046 s0 word -> n1044
node n1047 s1 word last
node n1048 s0 -> n1046
node n1049 s2 word last -> n22
node n1050 s8 word -> n984
node n1051 s9 word -> n1135
node n1052 s4 word -> n947
node n1053 s5 word -> n1029
node n1054 s3 word last -> n1048
node n1055 s3 word -> n944
node n1056 s4 word -> n1059
node n1057 s5 word -> n1077
node n1058 s1 word last
node n1059 s3 -> n951
node n1060 s4 -> n1059
node n1061 s5 -> n1077
node n1062 s1 word last
node n1063 s4 -> n1063
node n1064 s5 -> n1077
node n1065 s3 -> n951
node n1066 s1 word last
node n1067 s4 -> n1063
node n1068 s5 -> n1077
node n1069 s3 -> n951
node n1070 s6 -> n973
node n1071 s1 word last
node n1072 s4 -> n1067
node n1073 s5 -> n1077
node n1074 s3 -> n951
node n1075 s6 -> n973
node n1076 s1 word last
node n1077 s4 -> n1072
node n1078 s5 -> n1077
node n1079 s3 -> n951
node n1080 s6 -> n973
node n1081 s1 word last
node n1082 s4 word -> n1055
node n1083 s5 word -> n1120
node n1084 s3 word -> n1048
node n1085 s2 word last -> n22
node n1086 s4 word -> n1086
node n1087 s5 word -> n1100
node n1088 s3 word -> n944
node n1089 s1 word last
node n1090 s4 word -> n1086
node n1091 s5 word -> n1100
node n1092 s3 word -> n944
node n1093 s6 -> n973
node n1094 s1 word last
node n1095 s4 word -> n1090
node n1096 s5 word -> n1100
node n1097 s3 word -> n944
node n1098 s6 -> n973
node n1099 s1 word last
node n1100 s4 word -> n1095
node n1101 s5 word -> n1100
node n1102 s3 word -> n944
node n1103 s6 -> n973
node n1104 s1 word last
node n1105 s4 word -> n1086
node n1106 s5 word -> n1100
node n1107 s3 word -> n944
node n1108 s6 -> n1018
node n1109 s1 word last
node n1110 s4 word -> n1105
node n1111 s5 word -> n1115
node n1112 s3 word -> n944
node n1113 s6 -> n1018
node n1114 s1 word last
node n1115 s4 word -> n1095
node n1116 s5 word -> n1100
node n1117 s3 word -> n944
node n1118 s6 -> n1018
node n1119 s1 word last
node n1120 s4 word -> n1110
node n1121 s5 word -> n1130
node n1122 s3 word -> n944
node n1123 s6 -> n1018
node n1124 s1 word last
node n1125 s4 word -> n1090
node n1126 s5 word -> n1100
node n1127 s3 word -> n944
node n1128 s6 -> n1018
node n1129 s1 word last
node n1130 s4 word -> n1125
node n1131 s5 word -> n1115
node n1132 s3 word -> n944
node n1133 s6 -> n1018
node n1134 s1 word last
node n1135 s3 word -> n1082
node n1136 s4 word -> n947
node n1137 s5 word last -> n1029
node n1138 s9 word last -> n1135
node n1139 s8 last -> n1138
node n1140 s11 word last -> n984
node n1141 s13 word last -> n984
node n1142 s14 last -> n1141
node n1143 s16 last -> n1141
node n1144 s17 last -> n1141
node n1145 s12 last -> n1144
node n1146 s14 -> n231
node n1147 s15 -> n235
node n1148 s0 -> n1151
node n1149 s1 word -> n232
node n1150 s18 last -> n223
node n1151 s0 -> n1151
node n1152 s1 word last -> n232
node n1153 s0 word -> n1146
node n1154 s1 word last -> n232
node n1155 s0 -> n1153
node n1156 s2 word -> n244
node n1157 s14 -> n231
node n1158 s15 -> n235
node n1159 s1 word -> n232
node n1160 s18 last -> n223
node n1161 s3 word -> n1155
node n1162 s14 -> n231
node n1163 s15 -> n235
node n1164 s1 word -> n232
node n1165 s4 -> n1170
node n1166 s5 -> n1196
node n1167 s18 last -> n223
node n1168 s0 -> n1168
node n1169 s1 word last -> n232
node n1170 s3 -> n1168
node n1171 s1 word -> n232
node n1172 s4 -> n1170
node n1173 s5 last -> n1196
node n1174 s4 -> n1174
node n1175 s5 -> n1196
node n1176 s3 -> n1168
node n1177 s1 word last -> n232
node n1178 s4 -> n1174
node n1179 s5 -> n1196
node n1180 s3 -> n1168
node n1181 s1 word -> n232
node n1182 s6 last -> n1190
node n1183 s3 -> n1168
node n1184 s1 word -> n232
node n1185 s4 -> n1170
node n1186 s5 -> n1196
node n1187 s6 last -> n1190
node n1188 s0 last -> n1183
node n1189 s0 last -> n1188
node n1190 s0 last -> n1189
node n1191 s4 -> n1178
node n1192 s5 -> n1196
node n1193 s3 -> n1168
node n1194 s1 word -> n232
node n1195 s6 last -> n1190
node n1196 s4 -> n1191
node n1197 s5 -> n1196
node n1198 s3 -> n1168
node n1199 s1 word -> n232
node n1200 s6 last -> n1190
node n1201 s4 word -> n1161
node n1202 s5 word -> n1273
node n1203 s3 word -> n1306
node n1204 s14 -> n231
node n1205 s15 -> n235
node n1206 s18 last -> n223
node n1207 s4 word -> n1207
node n1208 s5 word -> n1230
node n1209 s3 word -> n1155
node n1210 s14 -> n231
node n1211 s15 -> n235
node n1212 s1 word -> n232
node n1213 s18 last -> n223
node n1214 s4 word -> n1207
node n1215 s5 word -> n1230
node n1216 s3 word -> n1155
node n1217 s14 -> n231
node n1218 s15 -> n235
node n1219 s1 word -> n232
node n1220 s6 -> n1190
node n1221 s18 last -> n223
node n1222 s4 word -> n1214
node n1223 s5 word -> n1230
node n1224 s3 word -> n1155
node n1225 s14 -> n231
node n1226 s15 -> n235
node n1227 s1 word -> n232
node n1228 s6 -> n1190
node n1229 s18 last -> n223
node n1230 s4 word -> n1222
node n1231 s5 word -> n1230
node n1232 s3 word -> n1155
node n1233 s14 -> n231
node n1234 s15 -> n235
node n1235 s1 word -> n232
node n1236 s6 -> n1190
node n1237 s18 last -> n223
node n1238 s4 word -> n1207
node n1239 s5 word -> n1230
node n1240 s3 word -> n1155
node n1241 s14 -> n231
node n1242 s15 -> n235
node n1243 s6 -> n1256
node n1244 s1 word -> n232
node n1245 s18 last -> n223
node n1246 s3 word -> n1155
node n1247 s14 -> n231
node n1248 s15 -> n235
node n1249 s6 -> n1256
node n1250 s1 word -> n232
node n1251 s4 -> n1170
node n1252 s5 -> n1196
node n1253 s18 last -> n223
node n1254 s0 word last -> n1246
node n1255 s0 last -> n1254
node n1256 s0 last -> n1255
node n1257 s4 word -> n1238
node n1258 s5 word -> n1265
node n1259 s3 word -> n1155
node n1260 s14 -> n231
node n1261 s15 -> n235
node n1262 s6 -> n1256
node n1263 s1 word -> n232
node n1264 s18 last -> n223
node n1265 s4 word -> n1222
node n1266 s5 word -> n1230
node n1267 s3 word -> n1155
node n1268 s14 -> n231
node n1269 s15 -> n235
node n1270 s6 -> n1256
node n1271 s1 word -> n232
node n1272 s18 last -> n223
node n1273 s4 word -> n1257
node n1274 s5 word -> n1289
node n1275 s3 word -> n1155
node n1276 s14 -> n231
node n1277 s15 -> n235
node n1278 s6 -> n1256
node n1279 s1 word -> n232
node n1280 s18 last -> n223
node n1281 s4 word -> n1214
node n1282 s5 word -> n1230
node n1283 s3 word -> n1155
node n1284 s14 -> n231
node n1285 s15 -> n235
node n1286 s6 -> n1256
node n1287 s1 word -> n232
node n1288 s18 last -> n223
node n1289 s4 word -> n1281
node n1290 s5 word -> n1265
node n1291 s3 word -> n1155
node n1292 s14 -> n231
node n1293 s15 -> n235
node n1294 s6 -> n1256
node n1295 s1 word -> n232
node n1296 s18 last -> n223
node n1297 s14 -> n231
node n1298 s15 -> n235
node n1299 s1 word -> n232
node n1300 s0 -> n1302
node n1301 s18 last -> n223
node n1302 s1 word -> n232
node n1303 s0 last -> n1302
node n1304 s0 word -> n1297
node n1305 s1 word last -> n232
node n1306 s0 -> n1304
node n1307 s2 word -> n244
node n1308 s14 -> n231
node n1309 s15 -> n235
node n1310 s18 last -> n223
node n1311 s13 word -> n1201
node n1312 s4 word -> n1340
node n1313 s5 word -> n1455
node n1314 s3 word -> n1492
node n1315 s8 word -> n1608
node n1316 s10 -> n1697
node n1317 s12 -> n1698
node n1318 s7 word -> n1542
node n1319 s15 -> n1700
node n1320 s14 -> n1701
node n1321 s18 -> n1703
node n1322 s19 word last
node n1323 s12 -> n415
node n1324 s0 -> n1329
node n1325 s8 word -> n399
node n1326 s10 -> n408
node n1327 s1 word -> n411
node n1328 s19 word last
node n1329 s0 -> n1329
node n1330 s1 word last -> n411
node n1331 s0 word -> n1323
node n1332 s1 word last -> n411
node n1333 s0 -> n1331
node n1334 s12 -> n415
node n1335 s8 word -> n399
node n1336 s10 -> n408
node n1337 s1 word -> n411
node n1338 s2 word -> n425
node n1339 s19 word last
node n1340 s3 word -> n1333
node n1341 s12 -> n415
node n1342 s4 -> n1350
node n1343 s5 -> n1376
node n1344 s8 word -> n399
node n1345 s10 -> n408
node n1346 s1 word -> n411
node n1347 s19 word last
node n1348 s0 -> n1348
node n1349 s1 word last -> n411
node n1350 s3 -> n1348
node n1351 s4 -> n1350
node n1352 s5 -> n1376
node n1353 s1 word last -> n411
node n1354 s4 -> n1354
node n1355 s5 -> n1376
node n1356 s3 -> n1348
node n1357 s1 word last -> n411
node n1358 s4 -> n1354
node n1359 s5 -> n1376
node n1360 s3 -> n1348
node n1361 s6 -> n1370
node n1362 s1 word last -> n411
node n1363 s3 -> n1348
node n1364 s4 -> n1350
node n1365 s5 -> n1376
node n1366 s6 -> n1370
node n1367 s1 word last -> n411
node n1368 s0 last -> n1363
node n1369 s0 last -> n1368
node n1370 s0 last -> n1369
node n1371 s4 -> n1358
node n1372 s5 -> n1376
node n1373 s3 -> n1348
node n1374 s6 -> n1370
node n1375 s1 word last -> n411
node n1376 s4 -> n1371
node n1377 s5 -> n1376
node n1378 s3 -> n1348
node n1379 s6 -> n1370
node n1380 s1 word last -> n411
node n1381 s4 word -> n1381
node n1382 s5 word -> n1407
node n1383 s3 word -> n1333
node n1384 s12 -> n415
node n1385 s8 word -> n399
node n1386 s10 -> n408
node n1387 s1 word -> n411
node n1388 s19 word last
node n1389 s4 word -> n1381
node n1390 s5 word -> n1407
node n1391 s3 word -> n1333
node n1392 s12 -> n415
node n1393 s6 -> n1370
node n1394 s8 word -> n399
node n1395 s10 -> n408
node n1396 s1 word -> n411
node n1397 s19 word last
node n1398 s4 word -> n1389
node n1399 s5 word -> n1407
node n1400 s3 word -> n1333
node n1401 s12 -> n415
node n1402 s6 -> n1370
node n1403 s8 word -> n399
node n1404 s10 -> n408
node n1405 s1 word -> n411
node n1406 s19 word last
node n1407 s4 word -> n1398
node n1408 s5 word -> n1407
node n1409 s3 word -> n1333
node n1410 s12 -> n415
node n1411 s6 -> n1370
node n1412 s8 word -> n399
node n1413 s10 -> n408
node n1414 s1 word -> n411
node n1415 s19 word last
node n1416 s4 word -> n1381
node n1417 s5 word -> n1407
node n1418 s3 word -> n1333
node n1419 s12 -> n415
node n1420 s6 -> n1436
node n1421 s8 word -> n399
node n1422 s10 -> n408
node n1423 s1 word -> n411
node n1424 s19 word last
node n1425 s3 word -> n1333
node n1426 s12 -> n415
node n1427 s6 -> n1436
node n1428 s4 -> n1350
node n1429 s5 -> n1376
node n1430 s8 word -> n399
node n1431 s10 -> n408
node n1432 s1 word -> n411
node n1433 s19 word last
node n1434 s0 word last -> n1425
node n1435 s0 last -> n1434
node n1436 s0 last -> n1435
node n1437 s4 word -> n1416
node n1438 s5 word -> n1446
node n1439 s3 word -> n1333
node n1440 s12 -> n415
node n1441 s6 -> n1436
node n1442 s8 word -> n399
node n1443 s10 -> n408
node n1444 s1 word -> n411
node n1445 s19 word last
node n1446 s4 word -> n1398
node n1447 s5 word -> n1407
node n1448 s3 word -> n1333
node n1449 s12 -> n415
node n1450 s6 -> n1436
node n1451 s8 word -> n399
node n1452 s10 -> n408
node n1453 s1 word -> n411
node n1454 s19 word last
node n1455 s4 word -> n1437
node n1456 s5 word -> n1473
node n1457 s3 word -> n1333
node n1458 s12 -> n415
node n1459 s6 -> n1436
node n1460 s8 word -> n399
node n1461 s10 -> n408
node n1462 s1 word -> n411
node n1463 s19 word last
node n1464 s4 word -> n1389
node n1465 s5 word -> n1407
node n1466 s3 word -> n1333
node n1467 s12 -> n415
node n1468 s6 -> n1436
node n1469 s8 word -> n399
node n1470 s10 -> n408
node n1471 s1 word -> n411
node n1472 s19 word last
node n1473 s4 word -> n1464
node n1474 s5 word -> n1446
node n1475 s3 word -> n1333
node n1476 s12 -> n415
node n1477 s6 -> n1436
node n1478 s8 word -> n399
node n1479 s10 -> n408
node n1480 s1 word -> n411
node n1481 s19 word last
node n1482 s12 -> n415
node n1483 s0 -> n1488
node n1484 s8 word -> n399
node n1485 s10 -> n408
node n1486 s1 word -> n411
node n1487 s19 word last
node n1488 s0 -> n1488
node n1489 s1 word last -> n411
node n1490 s0 word -> n1482
node n1491 s1 word last -> n411
node n1492 s0 -> n1490
node n1493 s12 -> n415
node n1494 s8 word -> n399
node n1495 s10 -> n408
node n1496 s2 word -> n425
node n1497 s19 word last
node n1498 s0 -> n1498
node n1499 s1 word last
node n1500 s0 word -> n1498
node n1501 s1 word last
node n1502 s0 -> n1500
node n1503 s2 word -> n22
node n1504 s1 word last
node n1505 s3 word -> n1502
node n1506 s4 -> n1511
node n1507 s5 -> n1537
node n1508 s1 word last
node n1509 s0 -> n1509
node n1510 s1 word last
node n1511 s3 -> n1509
node n1512 s4 -> n1511
node n1513 s5 -> n1537
node n1514 s1 word last
node n1515 s4 -> n1515
node n1516 s5 -> n1537
node n1517 s3 -> n1509
node n1518 s1 word last
node n1519 s4 -> n1515
node n1520 s5 -> n1537
node n1521 s3 -> n1509
node n1522 s6 -> n1531
node n1523 s1 word last
node n1524 s3 -> n1509
node n1525 s4 -> n1511
node n1526 s5 -> n1537
node n1527 s6 -> n1531
node n1528 s1 word last
node n1529 s0 last -> n1524
node n1530 s0 last -> n1529
node n1531 s0 last -> n1530
node n1532 s4 -> n1519
node n1533 s5 -> n1537
node n1534 s3 -> n1509
node n1535 s6 -> n1531
node n1536 s1 word last
node n1537 s4 -> n1532
node n1538 s5 -> n1537
node n1539 s3 -> n1509
node n1540 s6 -> n1531
node n1541 s1 word last
node n1542 s4 word -> n1505
node n1543 s5 word -> n1587
node n1544 s3 word last -> n1606
node n1545 s4 word -> n1545
node n1546 s5 word -> n1559
node n1547 s3 word -> n1502
node n1548 s1 word last
node n1549 s4 word -> n1545
node n1550 s5 word -> n1559
node n1551 s3 word -> n1502
node n1552 s6 -> n1531
node n1553 s1 word last
node n1554 s4 word -> n1549
node n1555 s5 word -> n1559
node n1556 s3 word -> n1502
node n1557 s6 -> n1531
node n1558 s1 word last
node n1559 s4 word -> n1554
node n1560 s5 word -> n1559
node n1561 s3 word -> n1502
node n1562 s6 -> n1531
node n1563 s1 word last
node n1564 s4 word -> n1545
node n1565 s5 word -> n1559
node n1566 s3 word -> n1502
node n1567 s6 -> n1576
node n1568 s1 word last
node n1569 s3 word -> n1502
node n1570 s6 -> n1576
node n1571 s4 -> n1511
node n1572 s5 -> n1537
node n1573 s1 word last
node n1574 s0 word last -> n1569
node n1575 s0 last -> n1574
node n1576 s0 last -> n1575
node n1577 s4 word -> n1564
node n1578 s5 word -> n1582
node n1579 s3 word -> n1502
node n1580 s6 -> n1576
node n1581 s1 word last
node n1582 s4 word -> n1554
node n1583 s5 word -> n1559
node n1584 s3 word -> n1502
node n1585 s6 -> n1576
node n1586 s1 word last
node n1587 s4 word -> n1577
node n1588 s5 word -> n1597
node n1589 s3 word -> n1502
node n1590 s6 -> n1576
node n1591 s1 word last
node n1592 s4 word -> n1549
node n1593 s5 word -> n1559
node n1594 s3 word -> n1502
node n1595 s6 -> n1576
node n1596 s1 word last
node n1597 s4 word -> n1592
node n1598 s5 word -> n1582
node n1599 s3 word -> n1502
node n1600 s6 -> n1576
node n1601 s1 word last
node n1602 s0 -> n1602
node n1603 s1 word last
node n1604 s0 word -> n1602
node n1605 s1 word last
node n1606 s0 -> n1604
node n1607 s2 word last -> n22
node n1608 s8 word -> n1542
node n1609 s9 word -> n1693
node n1610 s4 word -> n1505
node n1611 s5 word -> n1587
node n1612 s3 word last -> n1606
node n1613 s3 word -> n1502
node n1614 s4 word -> n1617
node n1615 s5 word -> n1635
node n1616 s1 word last
node n1617 s3 -> n1509
node n1618 s4 -> n1617
node n1619 s5 -> n1635
node n1620 s1 word last
node n1621 s4 -> n1621
node n1622 s5 -> n1635
node n1623 s3 -> n1509
node n1624 s1 word last
node n1625 s4 -> n1621
node n1626 s5 -> n1635
node n1627 s3 -> n1509
node n1628 s6 -> n1531
node n1629 s1 word last
node n1630 s4 -> n1625
node n1631 s5 -> n1635
node n1632 s3 -> n1509
node n1633 s6 -> n1531
node n1634 s1 word last
node n1635 s4 -> n1630
node n1636 s5 -> n1635
node n1637 s3 -> n1509
node n1638 s6 -> n1531
node n1639 s1 word last
node n1640 s4 word -> n1613
node n1641 s5 word -> n1678
node n1642 s3 word -> n1606
node n1643 s2 word last -> n22
node n1644 s4 word -> n1644
node n1645 s5 word -> n1658
node n1646 s3 word -> n1502
node n1647 s1 word last
node n1648 s4 word -> n1644
node n1649 s5 word -> n1658
node n1650 s3 word -> n1502
node n1651 s6 -> n1531
node n1652 s1 word last
node n1653 s4 word -> n1648
node n1654 s5 word -> n1658
node n1655 s3 word -> n1502
node n1656 s6 -> n1531
node n1657 s1 word last
node n1658 s4 word -> n1653
node n1659 s5 word -> n1658
node n1660 s3 word -> n1502
node n1661 s6 -> n1531
node n1662 s1 word last
node n1663 s4 word -> n1644
node n1664 s5 word -> n1658
node n1665 s3 word -> n1502
node n1666 s6 -> n1576
node n1667 s1 word last
node n1668 s4 word -> n1663
node n1669 s5 word -> n1673
node n1670 s3 word -> n1502
node n1671 s6 -> n1576
node n1672 s1 word last
node n1673 s4 word -> n1653
node n1674 s5 word -> n1658
node n1675 s3 word -> n1502
node n1676 s6 -> n1576
node n1677 s1 word last
node n1678 s4 word -> n1668
node n1679 s5 word -> n1688
node n1680 s3 word -> n1502
node n1681 s6 -> n1576
node n1682 s1 word last
node n1683 s4 word -> n1648
node n1684 s5 word -> n1658
node n1685 s3 word -> n1502
node n1686 s6 -> n1576
node n1687 s1 word last
node n1688 s4 word -> n1683
node n1689 s5 word -> n1673
node n1690 s3 word -> n1502
node n1691 s6 -> n1576
node n1692 s1 word last
node n1693 s3 word -> n1640
node n1694 s4 word -> n1505
node n1695 s5 word last -> n1587
node n1696 s9 word last -> n1693
node n1697 s8 last -> n1696
node n1698 s11 word last -> n1542
node n1699 s13 word last -> n1542
node n1700 s14 last -> n1699
node n1701 s16 last -> n1699
node n1702 s17 last -> n1699
node n1703 s12 last -> n1702
node n1704 s21 word last
node n1705 s16 last -> n1704
node n1706 s14 -> n1705
node n1707 s15 -> n1712
node n1708 s18 -> n1714
node n1709 s0 -> n1715
node n1710 s1 -> n1717
node n1711 s21 word last
node n1712 s14 last -> n1704
node n1713 s17 last -> n1704
node n1714 s12 last -> n1713
node n1715 s0 -> n1715
node n1716 s1 last -> n1717
node n1717 s14 -> n1705
node n1718 s15 -> n1712
node n1719 s18 -> n1714
node n1720 s21 word last
node n1721 s0 -> n1706
node n1722 s1 last -> n1717
node n1723 s0 -> n1721
node n1724 s2 -> n1730
node n1725 s14 -> n1705
node n1726 s15 -> n1712
node n1727 s18 -> n1714
node n1728 s1 -> n1717
node n1729 s21 word last
node n1730 s14 -> n1705
node n1731 s15 -> n1712
node n1732 s18 -> n1714
node n1733 s2 -> n1717
node n1734 s21 word last
node n1735 s3 -> n1723
node n1736 s14 -> n1705
node n1737 s15 -> n1712
node n1738 s18 -> n1714
node n1739 s1 -> n1717
node n1740 s4 -> n1745
node n1741 s5 -> n1771
node n1742 s21 word last
node n1743 s0 -> n1743
node n1744 s1 last -> n1717
node n1745 s3 -> n1743
node n1746 s1 -> n1717
node n1747 s4 -> n1745
node n1748 s5 last -> n1771
node n1749 s4 -> n1749
node n1750 s5 -> n1771
node n1751 s3 -> n1743
node n1752 s1 last -> n1717
node n1753 s4 -> n1749
node n1754 s5 -> n1771
node n1755 s3 -> n1743
node n1756 s1 -> n1717
node n1757 s6 last -> n1765
node n1758 s3 -> n1743
node n1759 s1 -> n1717
node n1760 s4 -> n1745
node n1761 s5 -> n1771
node n1762 s6 last -> n1765
node n1763 s0 last -> n1758
node n1764 s0 last -> n1763
node n1765 s0 last -> n1764
node n1766 s4 -> n1753
node n1767 s5 -> n1771
node n1768 s3 -> n1743
node n1769 s1 -> n1717
node n1770 s6 last -> n1765
node n1771 s4 -> n1766
node n1772 s5 -> n1771
node n1773 s3 -> n1743
node n1774 s1 -> n1717
node n1775 s6 last -> n1765
node n1776 s4 -> n1735
node n1777 s5 -> n1857
node n1778 s3 -> n1894
node n1779 s14 -> n1705
node n1780 s15 -> n1712
node n1781 s18 -> n1714
node n1782 s21 word last
node n1783 s4 -> n1783
node n1784 s5 -> n1809
node n1785 s3 -> n1723
node n1786 s14 -> n1705
node n1787 s15 -> n1712
node n1788 s18 -> n1714
node n1789 s1 -> n1717
node n1790 s21 word last
node n1791 s4 -> n1783
node n1792 s5 -> n1809
node n1793 s3 -> n1723
node n1794 s14 -> n1705
node n1795 s15 -> n1712
node n1796 s18 -> n1714
node n1797 s1 -> n1717
node n1798 s6 -> n1765
node n1799 s21 word last
node n1800 s4 -> n1791
node n1801 s5 -> n1809
node n1802 s3 -> n1723
node n1803 s14 -> n1705
node n1804 s15 -> n1712
node n1805 s18 -> n1714
node n1806 s1 -> n1717
node n1807 s6 -> n1765
node n1808 s21 word last
node n1809 s4 -> n1800
node n1810 s5 -> n1809
node n1811 s3 -> n1723
node n1812 s14 -> n1705
node n1813 s15 -> n1712
node n1814 s18 -> n1714
node n1815 s1 -> n1717
node n1816 s6 -> n1765
node n1817 s21 word last
node n1818 s4 -> n1783
node n1819 s5 -> n1809
node n1820 s3 -> n1723
node n1821 s14 -> n1705
node n1822 s15 -> n1712
node n1823 s18 -> n1714
node n1824 s6 -> n1838
node n1825 s1 -> n1717
node n1826 s21 word last
node n1827 s3 -> n1723
node n1828 s14 -> n1705
node n1829 s15 -> n1712
node n1830 s18 -> n1714
node n1831 s6 -> n1838
node n1832 s1 -> n1717
node n1833 s4 -> n1745
node n1834 s5 -> n1771
node n1835 s21 word last
node n1836 s0 last -> n1827
node n1837 s0 last -> n1836
node n1838 s0 last -> n1837
node n1839 s4 -> n1818
node n1840 s5 -> n1848
node n1841 s3 -> n1723
node n1842 s14 -> n1705
node n1843 s15 -> n1712
node n1844 s18 -> n1714
node n1845 s6 -> n1838
node n1846 s1 -> n1717
node n1847 s21 word last
node n1848 s4 -> n1800
node n1849 s5 -> n1809
node n1850 s3 -> n1723
node n1851 s14 -> n1705
node n1852 s15 -> n1712
node n1853 s18 -> n1714
node n1854 s6 -> n1838
node n1855 s1 -> n1717
node n1856 s21 word last
node n1857 s4 -> n1839
node n1858 s5 -> n1875
node n1859 s3 -> n1723
node n1860 s14 -> n1705
node n1861 s15 -> n1712
node n1862 s18 -> n1714
node n1863 s6 -> n1838
node n1864 s1 -> n1717
node n1865 s21 word last
node n1866 s4 -> n1791
node n1867 s5 -> n1809
node n1868 s3 -> n1723
node n1869 s14 -> n1705
node n1870 s15 -> n1712
node n1871 s18 -> n1714
node n1872 s6 -> n1838
node n1873 s1 -> n1717
node n1874 s21 word last
node n1875 s4 -> n1866
node n1876 s5 -> n1848
node n1877 s3 -> n1723
node n1878 s14 -> n1705
node n1879 s15 -> n1712
node n1880 s18 -> n1714
node n1881 s6 -> n1838
node n1882 s1 -> n1717
node n1883 s21 word last
node n1884 s14 -> n1705
node n1885 s15 -> n1712
node n1886 s18 -> n1714
node n1887 s1 -> n1717
node n1888 s0 -> n1890
node n1889 s21 word last
node n1890 s1 -> n1717
node n1891 s0 last -> n1890
node n1892 s0 -> n1884
node n1893 s1 last -> n1717
node n1894 s0 -> n1892
node n1895 s2 -> n1730
node n1896 s14 -> n1705
node n1897 s15 -> n1712
node n1898 s18 -> n1714
node n1899 s21 word last
node n1900 s13 -> n1776
node n1901 s4 -> n1951
node n1902 s5 -> n2075
node n1903 s3 -> n2116
node n1904 s19 -> n1704
node n1905 s8 -> n2256
node n1906 s10 -> n2370
node n1907 s12 -> n2371
node n1908 s7 -> n2174
node n1909 s15 -> n2373
node n1910 s14 -> n2374
node n1911 s18 -> n2376
node n1912 s21 word last
node n1913 s19 -> n1704
node n1914 s8 -> n1920
node n1915 s10 -> n1926
node n1916 s12 -> n1927
node n1917 s0 -> n1928
node n1918 s1 -> n1930
node n1919 s21 word last
node n1920 s8 -> n1704
node n1921 s9 -> n1923
node n1922 s21 word last
node n1923 s3 -> n1704
node n1924 s21 word last
node n1925 s9 last -> n1923
node n1926 s8 last -> n1925
node n1927 s11 last -> n1704
node n1928 s0 -> n1928
node n1929 s1 last -> n1930
node n1930 s19 -> n1704
node n1931 s8 -> n1920
node n1932 s10 -> n1926
node n1933 s12 -> n1927
node n1934 s21 word last
node n1935 s0 -> n1913
node n1936 s1 last -> n1930
node n1937 s0 -> n1935
node n1938 s2 -> n1945
node n1939 s19 -> n1704
node n1940 s8 -> n1920
node n1941 s10 -> n1926
node n1942 s12 -> n1927
node n1943 s1 -> n1930
node n1944 s21 word last
node n1945 s19 -> n1704
node n1946 s8 -> n1920
node n1947 s10 -> n1926
node n1948 s12 -> n1927
node n1949 s2 -> n1930
node n1950 s21 word last
node n1951 s3 -> n1937
node n1952 s19 -> n1704
node n1953 s8 -> n1920
node n1954 s10 -> n1926
node n1955 s12 -> n1927
node n1956 s1 -> n1930
node n1957 s4 -> n1962
node n1958 s5 -> n1988
node n1959 s21 word last
node n1960 s0 -> n1960
node n1961 s1 last -> n1930
node n1962 s3 -> n1960
node n1963 s1 -> n1930
node n1964 s4 -> n1962
node n1965 s5 last -> n1988
node n1966 s4 -> n1966
node n1967 s5 -> n1988
node n1968 s3 -> n1960
node n1969 s1 last -> n1930
node n1970 s4 -> n1966
node n1971 s5 -> n1988
node n1972 s3 -> n1960
node n1973 s1 -> n1930
node n1974 s6 last -> n1982
node n1975 s3 -> n1960
node n1976 s1 -> n1930
node n1977 s4 -> n1962
node n1978 s5 -> n1988
node n1979 s6 last -> n1982
node n1980 s0 last -> n1975
node n1981 s0 last -> n1980
node n1982 s0 last -> n1981
node n1983 s4 -> n1970
node n1984 s5 -> n1988
node n1985 s3 -> n1960
node n1986 s1 -> n1930
node n1987 s6 last -> n1982
node n1988 s4 -> n1983
node n1989 s5 -> n1988
node n1990 s3 -> n1960
node n1991 s1 -> n1930
node n1992 s6 last -> n1982
node n1993 s4 -> n1993
node n1994 s5 -> n2022
node n1995 s3 -> n1937
node n1996 s19 -> n1704
node n1997 s8 -> n1920
node n1998 s10 -> n1926
node n1999 s12 -> n1927
node n2000 s1 -> n1930
node n2001 s21 word last
node n2002 s4 -> n1993
node n2003 s5 -> n2022
node n2004 s3 -> n1937
node n2005 s19 -> n1704
node n2006 s8 -> n1920
node n2007 s10 -> n1926
node n2008 s12 -> n1927
node n2009 s1 -> n1930
node n2010 s6 -> n1982
node n2011 s21 word last
node n2012 s4 -> n2002
node n2013 s5 -> n2022
node n2014 s3 -> n1937
node n2015 s19 -> n1704
node n2016 s8 -> n1920
node n2017 s10 -> n1926
node n2018 s12 -> n1927
node n2019 s1 -> n1930
node n2020 s6 -> n1982
node n2021 s21 word last
node n2022 s4 -> n2012
node n2023 s5 -> n2022
node n2024 s3 -> n1937
node n2025 s19 -> n1704
node n2026 s8 -> n1920
node n2027 s10 -> n1926
node n2028 s12 -> n1927
node n2029 s1 -> n1930
node n2030 s6 -> n1982
node n2031 s21 word last
node n2032 s4 -> n1993
node n2033 s5 -> n2022
node n2034 s3 -> n1937
node n2035 s19 -> n1704
node n2036 s8 -> n1920
node n2037 s10 -> n1926
node n2038 s12 -> n1927
node n2039 s6 -> n2054
node n2040 s1 -> n1930
node n2041 s21 word last
node n2042 s3 -> n1937
node n2043 s19 -> n1704
node n2044 s8 -> n1920
node n2045 s10 -> n1926
node n2046 s12 -> n1927
node n2047 s6 -> n2054
node n2048 s1 -> n1930
node n2049 s4 -> n1962
node n2050 s5 -> n1988
node n2051 s21 word last
node n2052 s0 last -> n2042
node n2053 s0 last -> n2052
node n2054 s0 last -> n2053
node n2055 s4 -> n2032
node n2056 s5 -> n2065
node n2057 s3 -> n1937
node n2058 s19 -> n1704
node n2059 s8 -> n1920
node n2060 s10 -> n1926
node n2061 s12 -> n1927
node n2062 s6 -> n2054
node n2063 s1 -> n1930
node n2064 s21 word last
node n2065 s4 -> n2012
node n2066 s5 -> n2022
node n2067 s3 -> n1937
node n2068 s19 -> n1704
node n2069 s8 -> n1920
node n2070 s10 -> n1926
node n2071 s12 -> n1927
node n2072 s6 -> n2054
node n2073 s1 -> n1930
node n2074 s21 word last
node n2075 s4 -> n2055
node n2076 s5 -> n2095
node n2077 s3 -> n1937
node n2078 s19 -> n1704
node n2079 s8 -> n1920
node n2080 s10 -> n1926
node n2081 s12 -> n1927
node n2082 s6 -> n2054
node n2083 s1 -> n1930
node n2084 s21 word last
node n2085 s4 -> n2002
node n2086 s5 -> n2022
node n2087 s3 -> n1937
node n2088 s19 -> n1704
node n2089 s8 -> n1920
node n2090 s10 -> n1926
node n2091 s12 -> n1927
node n2092 s6 -> n2054
node n2093 s1 -> n1930
node n2094 s21 word last
node n2095 s4 -> n2085
node n2096 s5 -> n2065
node n2097 s3 -> n1937
node n2098 s19 -> n1704
node n2099 s8 -> n1920
node n2100 s10 -> n1926
node n2101 s12 -> n1927
node n2102 s6 -> n2054
node n2103 s1 -> n1930
node n2104 s21 word last
node n2105 s19 -> n1704
node n2106 s8 -> n1920
node n2107 s10 -> n1926
node n2108 s12 -> n1927
node n2109 s1 -> n1930
node n2110 s0 -> n2112
node n2111 s21 word last
node n2112 s1 -> n1930
node n2113 s0 last -> n2112
node n2114 s0 -> n2105
node n2115 s1 last -> n1930
node n2116 s0 -> n2114
node n2117 s2 -> n1945
node n2118 s19 -> n1704
node n2119 s8 -> n1920
node n2120 s10 -> n1926
node n2121 s12 -> n1927
node n2122 s21 word last
node n2123 s0 -> n2123
node n2124 s1 last -> n1704
node n2125 s0 -> n2123
node n2126 s1 -> n1704
node n2127 s21 word last
node n2128 s0 -> n2125
node n2129 s1 last -> n1704
node n2130 s0 -> n2128
node n2131 s2 -> n2134
node n2132 s1 -> n1704
node n2133 s21 word last
node n2134 s2 -> n1704
node n2135 s21 word last
node n2136 s3 -> n2130
node n2137 s1 -> n1704
node n2138 s4 -> n2143
node n2139 s5 -> n2169
node n2140 s21 word last
node n2141 s0 -> n2141
node n2142 s1 last -> n1704
node n2143 s3 -> n2141
node n2144 s1 -> n1704
node n2145 s4 -> n2143
node n2146 s5 last -> n2169
node n2147 s4 -> n2147
node n2148 s5 -> n2169
node n2149 s3 -> n2141
node n2150 s1 last -> n1704
node n2151 s4 -> n2147
node n2152 s5 -> n2169
node n2153 s3 -> n2141
node n2154 s1 -> n1704
node n2155 s6 last -> n2163
node n2156 s3 -> n2141
node n2157 s1 -> n1704
node n2158 s4 -> n2143
node n2159 s5 -> n2169
node n2160 s6 last -> n2163
node n2161 s0 last -> n2156
node n2162 s0 last -> n2161
node n2163 s0 last -> n2162
node n2164 s4 -> n2151
node n2165 s5 -> n2169
node n2166 s3 -> n2141
node n2167 s1 -> n1704
node n2168 s6 last -> n2163
node n2169 s4 -> n2164
node n2170 s5 -> n2169
node n2171 s3 -> n2141
node n2172 s1 -> n1704
node n2173 s6 last -> n2163
node n2174 s4 -> n2136
node n2175 s5 -> n2228
node n2176 s3 -> n2253
node n2177 s21 word last
node n2178 s4 -> n2178
node n2179 s5 -> n2195
node n2180 s3 -> n2130
node n2181 s1 -> n1704
node n2182 s21 word last
node n2183 s4 -> n2178
node n2184 s5 -> n2195
node n2185 s3 -> n2130
node n2186 s1 -> n1704
node n2187 s6 -> n2163
node n2188 s21 word last
node n2189 s4 -> n2183
node n2190 s5 -> n2195
node n2191 s3 -> n2130
node n2192 s1 -> n1704
node n2193 s6 -> n2163
node n2194 s21 word last
node n2195 s4 -> n2189
node n2196 s5 -> n2195
node n2197 s3 -> n2130
node n2198 s1 -> n1704
node n2199 s6 -> n2163
node n2200 s21 word last
node n2201 s4 -> n2178
node n2202 s5 -> n2195
node n2203 s3 -> n2130
node n2204 s6 -> n2215
node n2205 s1 -> n1704
node n2206 s21 word last
node n2207 s3 -> n2130
node n2208 s6 -> n2215
node n2209 s1 -> n1704
node n2210 s4 -> n2143
node n2211 s5 -> n2169
node n2212 s21 word last
node n2213 s0 last -> n2207
node n2214 s0 last -> n2213
node n2215 s0 last -> n2214
node n2216 s4 -> n2201
node n2217 s5 -> n2222
node n2218 s3 -> n2130
node n2219 s6 -> n2215
node n2220 s1 -> n1704
node n2221 s21 word last
node n2222 s4 -> n2189
node n2223 s5 -> n2195
node n2224 s3 -> n2130
node n2225 s6 -> n2215
node n2226 s1 -> n1704
node n2227 s21 word last
node n2228 s4 -> n2216
node n2229 s5 -> n2240
node n2230 s3 -> n2130
node n2231 s6 -> n2215
node n2232 s1 -> n1704
node n2233 s21 word last
node n2234 s4 -> n2183
node n2235 s5 -> n2195
node n2236 s3 -> n2130
node n2237 s6 -> n2215
node n2238 s1 -> n1704
node n2239 s21 word last
node n2240 s4 -> n2234
node n2241 s5 -> n2222
node n2242 s3 -> n2130
node n2243 s6 -> n2215
node n2244 s1 -> n1704
node n2245 s21 word last
node n2246 s1 -> n1704
node n2247 s0 -> n2249
node n2248 s21 word last
node n2249 s1 -> n1704
node n2250 s0 last -> n2249
node n2251 s0 -> n2246
node n2252 s1 last -> n1704
node n2253 s0 -> n2251
node n2254 s2 -> n2134
node n2255 s21 word last
node n2256 s8 -> n2174
node n2257 s9 -> n2365
node n2258 s4 -> n2136
node n2259 s5 -> n2228
node n2260 s3 -> n2253
node n2261 s21 word last
node n2262 s3 -> n2130
node n2263 s1 -> n1704
node n2264 s4 -> n2267
node n2265 s5 -> n2295
node n2266 s21 word last
node n2267 s3 -> n2141
node n2268 s1 -> n1704
node n2269 s4 -> n2272
node n2270 s5 -> n2290
node n2271 s21 word last
node n2272 s3 -> n2141
node n2273 s1 -> n1704
node n2274 s4 -> n2272
node n2275 s5 last -> n2290
node n2276 s4 -> n2276
node n2277 s5 -> n2290
node n2278 s3 -> n2141
node n2279 s1 last -> n1704
node n2280 s4 -> n2276
node n2281 s5 -> n2290
node n2282 s3 -> n2141
node n2283 s1 -> n1704
node n2284 s6 last -> n2163
node n2285 s4 -> n2280
node n2286 s5 -> n2290
node n2287 s3 -> n2141
node n2288 s1 -> n1704
node n2289 s6 last -> n2163
node n2290 s4 -> n2285
node n2291 s5 -> n2290
node n2292 s3 -> n2141
node n2293 s1 -> n1704
node n2294 s6 last -> n2163
node n2295 s4 -> n2285
node n2296 s5 -> n2290
node n2297 s3 -> n2141
node n2298 s1 -> n1704
node n2299 s6 -> n2163
node n2300 s21 word last
node n2301 s4 -> n2262
node n2302 s5 -> n2347
node n2303 s3 -> n2253
node n2304 s2 -> n2134
node n2305 s21 word last
node n2306 s4 -> n2306
node n2307 s5 -> n2323
node n2308 s3 -> n2130
node n2309 s1 -> n1704
node n2310 s21 word last
node n2311 s4 -> n2306
node n2312 s5 -> n2323
node n2313 s3 -> n2130
node n2314 s1 -> n1704
node n2315 s6 -> n2163
node n2316 s21 word last
node n2317 s4 -> n2311
node n2318 s5 -> n2323
node n2319 s3 -> n2130
node n2320 s1 -> n1704
node n2321 s6 -> n2163
node n2322 s21 word last
node n2323 s4 -> n2317
node n2324 s5 -> n2323
node n2325 s3 -> n2130
node n2326 s1 -> n1704
node n2327 s6 -> n2163
node n2328 s21 word last
node n2329 s4 -> n2306
node n2330 s5 -> n2323
node n2331 s3 -> n2130
node n2332 s6 -> n2215
node n2333 s1 -> n1704
node n2334 s21 word last
node n2335 s4 -> n2329
node n2336 s5 -> n2341
node n2337 s3 -> n2130
node n2338 s6 -> n2215
node n2339 s1 -> n1704
node n2340 s21 word last
node n2341 s4 -> n2317
node n2342 s5 -> n2323
node n2343 s3 -> n2130
node n2344 s6 -> n2215
node n2345 s1 -> n1704
node n2346 s21 word last
node n2347 s4 -> n2335
node n2348 s5 -> n2359
node n2349 s3 -> n2130
node n2350 s6 -> n2215
node n2351 s1 -> n1704
node n2352 s21 word last
node n2353 s4 -> n2311
node n2354 s5 -> n2323
node n2355 s3 -> n2130
node n2356 s6 -> n2215
node n2357 s1 -> n1704
node n2358 s21 word last
node n2359 s4 -> n2353
node n2360 s5 -> n2341
node n2361 s3 -> n2130
node n2362 s6 -> n2215
node n2363 s1 -> n1704
node n2364 s21 word last
node n2365 s3 -> n2301
node n2366 s4 -> n2136
node n2367 s5 -> n2228
node n2368 s21 word last
node n2369 s9 last -> n2365
node n2370 s8 last -> n2369
node n2371 s11 last -> n2174
node n2372 s13 last -> n2174
node n2373 s14 last -> n2372
node n2374 s16 last -> n2372
node n2375 s17 last -> n2372
node n2376 s12 last -> n2375
