# gLex8calcsheet: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 "0123456789" "t0"
set s1 "." "t1"
set s2 "0" "t2"
set s3 "123456789" "t3"
set s4 "*+-/<=>" "t4"
set s5 "," "t5"
set s6 ")" "t6"
set s7 "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz" "t7"
set s8 ":" "t8"
set s9 "(" "t9"
set s10 "h" "t10"
set s11 "s" "t11"
set s12 "o" "t12"
set s13 "c" "t13"
set s14 "n" "t14"
set s15 "i" "t15"
set s16 "a" "t16"
set s17 "t" "t17"
set s18 "y" "t18"
set s19 "u" "t19"
set s20 "g" "t20"
set s21 "v" "t21"
set s22 "b" "t22"
set s23 "A" "t23"
set s24 "m" "t24"
set s25 "N" "t25"
set s26 "d" "t26"
set s27 "r" "t27"
set s28 "q" "t28"
set s29 "e" "t29"
set s30 "S" "t30"
set s31 "R" "t31"
set s32 "D" "t32"
set s33 "T" "t33"
set s34 "V" "t34"
set s35 "p" "t35"
set s36 "l" "t36"
set s37 "C" "t37"
set s38 "x" "t38"
set s39 "M" "t39"
set s40 "F" "t40"
set s41 "f" "t41"
set s42 "1" "t42"
set s43 "E" "t43"
set s44 "G" "t44"
set s45 "H" "t45"
set s46 "I" "t46"
set s47 "0P" "t47"
set s48 "L" "t48"
set s49 "w" "t49"
set s50 "P" "t50"
set s51 "Y" "t51"
set s52 "BDJKOQUWXYZbdefghijklmpqruvwxyz" "t52"
set s53 "=" "t53"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s53 -> n29
node n1 s2 word -> n28
node n2 s3 word -> n55
node n3 s1 word -> n27
node n4 s23 -> n599
node n5 s25 -> n647
node n6 s30 -> n652
node n7 s34 -> n694
node n8 s16 -> n698
node n9 s14 -> n701
node n10 s12 -> n704
node n11 s13 -> n707
node n12 s11 -> n710
node n13 s17 -> n713
node n14 s37 -> n728
node n15 s40 -> n737
node n16 s43 -> n754
node n17 s44 -> n762
node n18 s45 -> n770
node n19 s46 -> n784
node n20 s48 -> n788
node n21 s50 -> n797
node n22 s31 -> n810
node n23 s33 -> n821
node n24 s39 -> n824
node n25 s8 -> n830
node n26 s52 last -> n869
node n27 s0 word last -> n27
node n28 s1 word last -> n27
node n29 s2 word -> n28
node n30 s3 word -> n55
node n31 s1 word -> n27
node n32 s23 -> n599
node n33 s25 -> n647
node n34 s30 -> n652
node n35 s34 -> n694
node n36 s16 -> n698
node n37 s14 -> n701
node n38 s12 -> n704
node n39 s13 -> n707
node n40 s11 -> n710
node n41 s17 -> n713
node n42 s37 -> n728
node n43 s40 -> n737
node n44 s43 -> n754
node n45 s44 -> n762
node n46 s45 -> n770
node n47 s46 -> n784
node n48 s48 -> n788
node n49 s50 -> n797
node n50 s31 -> n810
node n51 s33 -> n821
node n52 s39 -> n824
node n53 s8 -> n830
node n54 s52 last -> n869
node n55 s0 word -> n55
node n56 s1 word last -> n27
node n57 s4 -> n442
node n58 s5 -> n508
node n59 s1 -> n61
node n60 s6 word last -> n441
node n61 s0 -> n61
node n62 s4 -> n442
node n63 s5 -> n508
node n64 s6 word last -> n441
node n65 s4 -> n73
node n66 s5 -> n139
node n67 s1 -> n69
node n68 s6 word last -> n441
node n69 s0 -> n69
node n70 s4 -> n73
node n71 s5 -> n139
node n72 s6 word last -> n441
node n73 s2 -> n65
node n74 s3 -> n77
node n75 s1 -> n69
node n76 s7 last -> n123
node n77 s0 -> n77
node n78 s4 -> n73
node n79 s5 -> n139
node n80 s1 -> n69
node n81 s6 word last -> n441
node n82 s4 -> n106
node n83 s5 -> n139
node n84 s8 -> n96
node n85 s1 -> n97
node n86 s6 word last -> n441
node n87 s4 -> n106
node n88 s5 -> n139
node n89 s6 word last -> n441
node n90 s2 -> n87
node n91 s3 last -> n92
node n92 s0 -> n92
node n93 s4 -> n106
node n94 s5 -> n139
node n95 s6 word last -> n441
node n96 s7 last -> n90
node n97 s1 last -> n96
node n98 s2 -> n82
node n99 s3 last -> n100
node n100 s0 -> n100
node n101 s4 -> n106
node n102 s5 -> n139
node n103 s8 -> n96
node n104 s1 -> n97
node n105 s6 word last -> n441
node n106 s7 -> n98
node n107 s2 -> n65
node n108 s3 -> n77
node n109 s1 last -> n69
node n110 s4 -> n106
node n111 s5 -> n139
node n112 s8 -> n121
node n113 s1 -> n122
node n114 s6 word last -> n441
node n115 s2 -> n87
node n116 s3 last -> n117
node n117 s0 -> n117
node n118 s4 -> n106
node n119 s5 -> n139
node n120 s6 word last -> n441
node n121 s7 last -> n115
node n122 s1 last -> n121
node n123 s2 -> n110
node n124 s3 last -> n125
node n125 s0 -> n125
node n126 s4 -> n106
node n127 s5 -> n139
node n128 s8 -> n121
node n129 s1 -> n122
node n130 s6 word last -> n441
node n131 s4 -> n73
node n132 s5 -> n139
node n133 s1 -> n135
node n134 s6 word last -> n441
node n135 s0 -> n135
node n136 s4 -> n73
node n137 s5 -> n139
node n138 s6 word last -> n441
node n139 s2 -> n131
node n140 s3 -> n143
node n141 s1 -> n135
node n142 s7 last -> n217
node n143 s0 -> n143
node n144 s4 -> n73
node n145 s5 -> n139
node n146 s1 -> n135
node n147 s6 word last -> n441
node n148 s4 -> n172
node n149 s5 -> n139
node n150 s8 -> n162
node n151 s1 -> n163
node n152 s6 word last -> n441
node n153 s4 -> n172
node n154 s5 -> n139
node n155 s6 word last -> n441
node n156 s2 -> n153
node n157 s3 last -> n158
node n158 s0 -> n158
node n159 s4 -> n172
node n160 s5 -> n139
node n161 s6 word last -> n441
node n162 s7 last -> n156
node n163 s1 last -> n162
node n164 s2 -> n148
node n165 s3 last -> n166
node n166 s0 -> n166
node n167 s4 -> n172
node n168 s5 -> n139
node n169 s8 -> n162
node n170 s1 -> n163
node n171 s6 word last -> n441
node n172 s7 -> n164
node n173 s2 -> n65
node n174 s3 -> n77
node n175 s1 last -> n69
node n176 s4 -> n172
node n177 s5 -> n139
node n178 s8 -> n187
node n179 s1 -> n188
node n180 s6 word last -> n441
node n181 s2 -> n153
node n182 s3 last -> n183
node n183 s0 -> n183
node n184 s4 -> n172
node n185 s5 -> n139
node n186 s6 word last -> n441
node n187 s7 last -> n181
node n188 s1 last -> n187
node n189 s2 -> n176
node n190 s3 last -> n191
node n191 s0 -> n191
node n192 s4 -> n172
node n193 s5 -> n139
node n194 s8 -> n187
node n195 s1 -> n188
node n196 s6 word last -> n441
node n197 s7 -> n189
node n198 s2 -> n65
node n199 s3 -> n77
node n200 s1 last -> n69
node n201 s4 -> n197
node n202 s5 -> n139
node n203 s8 -> n215
node n204 s1 -> n216
node n205 s6 word last -> n441
node n206 s4 -> n197
node n207 s5 -> n139
node n208 s6 word last -> n441
node n209 s2 -> n206
node n210 s3 last -> n211
node n211 s0 -> n211
node n212 s4 -> n197
node n213 s5 -> n139
node n214 s6 word last -> n441
node n215 s7 last -> n209
node n216 s1 last -> n215
node n217 s2 -> n201
node n218 s3 last -> n219
node n219 s0 -> n219
node n220 s4 -> n197
node n221 s5 -> n139
node n222 s8 -> n215
node n223 s1 -> n216
node n224 s6 word last -> n441
node n225 s9 last -> n139
node n226 s10 -> n225
node n227 s9 last -> n139
node n228 s11 last -> n226
node n229 s12 last -> n228
node n230 s13 -> n229
node n231 s11 -> n237
node n232 s17 -> n238
node n233 s14 -> n243
node n234 s21 -> n244
node n235 s22 last -> n245
node n236 s14 last -> n226
node n237 s15 last -> n236
node n238 s16 last -> n236
node n239 s18 last -> n225
node n240 s17 last -> n239
node n241 s15 last -> n240
node n242 s19 last -> n241
node n243 s14 last -> n242
node n244 s20 last -> n225
node n245 s11 last -> n225
node n246 s23 -> n230
node n247 s25 -> n269
node n248 s30 -> n271
node n249 s34 -> n311
node n250 s16 -> n312
node n251 s14 -> n313
node n252 s13 -> n229
node n253 s11 -> n237
node n254 s17 -> n314
node n255 s37 -> n326
node n256 s40 -> n333
node n257 s43 -> n348
node n258 s44 -> n357
node n259 s45 -> n359
node n260 s46 -> n371
node n261 s48 -> n377
node n262 s50 -> n379
node n263 s31 -> n390
node n264 s33 -> n399
node n265 s8 -> n402
node n266 s12 -> n310
node n267 s39 last -> n341
node n268 s24 last -> n225
node n269 s19 last -> n268
node n270 s21 last -> n225
node n271 s26 -> n270
node n272 s15 -> n279
node n273 s28 -> n281
node n274 s29 -> n292
node n275 s10 -> n299
node n276 s17 -> n309
node n277 s19 last -> n268
node n278 s14 last -> n269
node n279 s20 last -> n278
node n280 s17 last -> n225
node n281 s27 last -> n280
node n282 s26 last -> n225
node n283 s29 last -> n282
node n284 s29 last -> n283
node n285 s30 last -> n284
node n286 s24 last -> n285
node n287 s12 last -> n286
node n288 s26 last -> n287
node n289 s14 last -> n288
node n290 s16 last -> n289
node n291 s31 last -> n290
node n292 s17 last -> n291
node n293 s29 last -> n225
node n294 s17 last -> n293
node n295 s16 last -> n294
node n296 s32 last -> n295
node n297 s17 last -> n296
node n298 s27 last -> n297
node n299 s12 last -> n298
node n300 s32 -> n295
node n301 s33 last -> n303
node n302 s24 last -> n293
node n303 s15 last -> n302
node n304 s12 last -> n300
node n305 s33 last -> n304
node n306 s20 last -> n305
node n307 s14 last -> n306
node n308 s15 last -> n307
node n309 s27 last -> n308
node n310 s27 last -> n225
node n311 s16 last -> n310
node n312 s14 last -> n282
node n313 s12 last -> n280
node n314 s16 -> n236
node n315 s15 last -> n302
node n316 s14 last -> n225
node n317 s20 last -> n316
node n318 s15 last -> n317
node n319 s30 last -> n318
node n320 s18 last -> n319
node n321 s35 -> n320
node n322 s24 last -> n325
node n323 s19 last -> n312
node n324 s12 last -> n323
node n325 s35 last -> n324
node n326 s12 -> n321
node n327 s29 last -> n331
node n328 s14 last -> n244
node n329 s15 last -> n328
node n330 s36 last -> n329
node n331 s15 last -> n330
node n332 s12 last -> n282
node n333 s24 -> n332
node n334 s16 -> n338
node n335 s32 -> n339
node n336 s39 -> n341
node n337 s36 last -> n344
node n338 s22 last -> n245
node n339 s15 last -> n268
node n340 s38 last -> n225
node n341 s16 -> n340
node n342 s15 last -> n316
node n343 s12 last -> n310
node n344 s12 last -> n343
node n345 s9 -> n139
node n346 s13 last -> n225
node n347 s41 last -> n345
node n348 s27 -> n347
node n349 s38 last -> n353
node n350 s9 -> n139
node n351 s24 last -> n352
node n352 s42 last -> n225
node n353 s35 last -> n350
node n354 s16 last -> n225
node n355 s24 last -> n354
node n356 s24 last -> n355
node n357 s16 last -> n356
node n358 s35 last -> n313
node n359 s18 -> n358
node n360 s12 last -> n366
node n361 s19 last -> n294
node n362 s14 last -> n361
node n363 s15 last -> n362
node n364 s39 last -> n363
node n365 s27 last -> n364
node n366 s19 last -> n365
node n367 s15 last -> n294
node n368 s14 last -> n367
node n369 s15 last -> n368
node n370 s40 last -> n369
node n371 s11 last -> n370
node n372 s9 -> n139
node n373 s22 -> n225
node n374 s42 last -> n375
node n375 s47 last -> n225
node n376 s20 last -> n372
node n377 s12 -> n376
node n378 s44 last -> n357
node n379 s15 -> n225
node n380 s12 last -> n381
node n381 s49 last -> n225
node n382 s29 last -> n310
node n383 s26 last -> n382
node n384 s14 last -> n383
node n385 s15 last -> n384
node n386 s16 last -> n385
node n387 s24 -> n386
node n388 s16 last -> n389
node n389 s36 last -> n225
node n390 s29 -> n387
node n391 s12 -> n323
node n392 s16 last -> n395
node n393 s12 last -> n268
node n394 s26 last -> n393
node n395 s14 last -> n394
node n396 s13 last -> n225
node n397 s14 last -> n396
node n398 s19 last -> n397
node n399 s27 last -> n398
node n400 s34 last -> n225
node n401 s50 last -> n400
node n402 s25 -> n401
node n403 s50 -> n412
node n404 s30 -> n415
node n405 s32 -> n428
node n406 s45 -> n431
node n407 s39 -> n436
node n408 s17 -> n439
node n409 s14 -> n440
node n410 s40 last -> n400
node n411 s33 last -> n225
node n412 s39 -> n411
node n413 s34 last -> n225
node n414 s25 last -> n225
node n415 s48 -> n414
node n416 s51 last -> n417
node n417 s32 last -> n225
node n418 s26 last -> n282
node n419 s23 last -> n418
node n420 s29 last -> n419
node n421 s17 -> n420
node n422 s18 last -> n427
node n423 s41 last -> n225
node n424 s41 last -> n423
node n425 s15 last -> n424
node n426 s32 last -> n425
node n427 s11 last -> n426
node n428 s16 last -> n421
node n429 s27 last -> n427
node n430 s19 last -> n429
node n431 s12 last -> n430
node n432 s29 last -> n427
node n433 s17 last -> n432
node n434 s19 last -> n433
node n435 s14 last -> n434
node n436 s15 last -> n435
node n437 s16 last -> n239
node n438 s26 last -> n437
node n439 s12 last -> n438
node n440 s12 last -> n381
node n441 s4 last -> n246
node n442 s2 -> n57
node n443 s3 -> n446
node n444 s1 -> n61
node n445 s7 last -> n492
node n446 s0 -> n446
node n447 s4 -> n442
node n448 s5 -> n508
node n449 s1 -> n61
node n450 s6 word last -> n441
node n451 s4 -> n475
node n452 s5 -> n508
node n453 s8 -> n465
node n454 s1 -> n466
node n455 s6 word last -> n441
node n456 s4 -> n475
node n457 s5 -> n508
node n458 s6 word last -> n441
node n459 s2 -> n456
node n460 s3 last -> n461
node n461 s0 -> n461
node n462 s4 -> n475
node n463 s5 -> n508
node n464 s6 word last -> n441
node n465 s7 last -> n459
node n466 s1 last -> n465
node n467 s2 -> n451
node n468 s3 last -> n469
node n469 s0 -> n469
node n470 s4 -> n475
node n471 s5 -> n508
node n472 s8 -> n465
node n473 s1 -> n466
node n474 s6 word last -> n441
node n475 s7 -> n467
node n476 s2 -> n57
node n477 s3 -> n446
node n478 s1 last -> n61
node n479 s4 -> n475
node n480 s5 -> n508
node n481 s8 -> n490
node n482 s1 -> n491
node n483 s6 word last -> n441
node n484 s2 -> n456
node n485 s3 last -> n486
node n486 s0 -> n486
node n487 s4 -> n475
node n488 s5 -> n508
node n489 s6 word last -> n441
node n490 s7 last -> n484
node n491 s1 last -> n490
node n492 s2 -> n479
node n493 s3 last -> n494
node n494 s0 -> n494
node n495 s4 -> n475
node n496 s5 -> n508
node n497 s8 -> n490
node n498 s1 -> n491
node n499 s6 word last -> n441
node n500 s4 -> n442
node n501 s5 -> n508
node n502 s1 -> n504
node n503 s6 word last -> n441
node n504 s0 -> n504
node n505 s4 -> n442
node n506 s5 -> n508
node n507 s6 word last -> n441
node n508 s2 -> n500
node n509 s3 -> n512
node n510 s1 -> n504
node n511 s7 last -> n586
node n512 s0 -> n512
node n513 s4 -> n442
node n514 s5 -> n508
node n515 s1 -> n504
node n516 s6 word last -> n441
node n517 s4 -> n541
node n518 s5 -> n508
node n519 s8 -> n531
node n520 s1 -> n532
node n521 s6 word last -> n441
node n522 s4 -> n541
node n523 s5 -> n508
node n524 s6 word last -> n441
node n525 s2 -> n522
node n526 s3 last -> n527
node n527 s0 -> n527
node n528 s4 -> n541
node n529 s5 -> n508
node n530 s6 word last -> n441
node n531 s7 last -> n525
node n532 s1 last -> n531
node n533 s2 -> n517
node n534 s3 last -> n535
node n535 s0 -> n535
node n536 s4 -> n541
node n537 s5 -> n508
node n538 s8 -> n531
node n539 s1 -> n532
node n540 s6 word last -> n441
node n541 s7 -> n533
node n542 s2 -> n57
node n543 s3 -> n446
node n544 s1 last -> n61
node n545 s4 -> n541
node n546 s5 -> n508
node n547 s8 -> n556
node n548 s1 -> n557
node n549 s6 word last -> n441
node n550 s2 -> n522
node n551 s3 last -> n552
node n552 s0 -> n552
node n553 s4 -> n541
node n554 s5 -> n508
node n555 s6 word last -> n441
node n556 s7 last -> n550
node n557 s1 last -> n556
node n558 s2 -> n545
node n559 s3 last -> n560
node n560 s0 -> n560
node n561 s4 -> n541
node n562 s5 -> n508
node n563 s8 -> n556
node n564 s1 -> n557
node n565 s6 word last -> n441
node n566 s7 -> n558
node n567 s2 -> n57
node n568 s3 -> n446
node n569 s1 last -> n61
node n570 s4 -> n566
node n571 s5 -> n508
node n572 s8 -> n584
node n573 s1 -> n585
node n574 s6 word last -> n441
node n575 s4 -> n566
node n576 s5 -> n508
node n577 s6 word last -> n441
node n578 s2 -> n575
node n579 s3 last -> n580
node n580 s0 -> n580
node n581 s4 -> n566
node n582 s5 -> n508
node n583 s6 word last -> n441
node n584 s7 last -> n578
node n585 s1 last -> n584
node n586 s2 -> n570
node n587 s3 last -> n588
node n588 s0 -> n588
node n589 s4 -> n566
node n590 s5 -> n508
node n591 s8 -> n584
node n592 s1 -> n585
node n593 s6 word last -> n441
node n594 s9 last -> n508
node n595 s10 -> n594
node n596 s9 last -> n508
node n597 s11 last -> n595
node n598 s12 last -> n597
node n599 s13 -> n598
node n600 s11 -> n608
node n601 s17 -> n609
node n602 s14 -> n614
node n603 s2 word -> n632
node n604 s3 word -> n641
node n605 s21 -> n645
node n606 s22 last -> n646
node n607 s14 last -> n595
node n608 s15 last -> n607
node n609 s16 last -> n607
node n610 s18 last -> n594
node n611 s17 last -> n610
node n612 s15 last -> n611
node n613 s19 last -> n612
node n614 s14 last -> n613
node n615 s4 -> n631
node n616 s8 -> n623
node n617 s1 last -> n624
node n618 s4 last -> n631
node n619 s2 word -> n618
node n620 s3 word last -> n621
node n621 s0 word -> n621
node n622 s4 last -> n631
node n623 s7 last -> n619
node n624 s1 last -> n623
node n625 s2 word -> n615
node n626 s3 word last -> n627
node n627 s0 word -> n627
node n628 s4 -> n631
node n629 s8 -> n623
node n630 s1 last -> n624
node n631 s7 last -> n625
node n632 s4 -> n631
node n633 s8 -> n639
node n634 s1 last -> n640
node n635 s2 word -> n618
node n636 s3 word last -> n637
node n637 s0 word -> n637
node n638 s4 last -> n631
node n639 s7 last -> n635
node n640 s1 last -> n639
node n641 s0 word -> n641
node n642 s4 -> n631
node n643 s8 -> n639
node n644 s1 last -> n640
node n645 s20 last -> n594
node n646 s11 last -> n594
node n647 s2 word -> n632
node n648 s3 word -> n641
node n649 s19 last -> n650
node n650 s24 last -> n594
node n651 s21 last -> n594
node n652 s26 -> n651
node n653 s15 -> n663
node n654 s28 -> n665
node n655 s29 -> n676
node n656 s10 -> n683
node n657 s17 -> n693
node n658 s2 word -> n632
node n659 s3 word -> n641
node n660 s19 last -> n650
node n661 s19 last -> n650
node n662 s14 last -> n661
node n663 s20 last -> n662
node n664 s17 last -> n594
node n665 s27 last -> n664
node n666 s26 last -> n594
node n667 s29 last -> n666
node n668 s29 last -> n667
node n669 s30 last -> n668
node n670 s24 last -> n669
node n671 s12 last -> n670
node n672 s26 last -> n671
node n673 s14 last -> n672
node n674 s16 last -> n673
node n675 s31 last -> n674
node n676 s17 last -> n675
node n677 s29 last -> n594
node n678 s17 last -> n677
node n679 s16 last -> n678
node n680 s32 last -> n679
node n681 s17 last -> n680
node n682 s27 last -> n681
node n683 s12 last -> n682
node n684 s32 -> n679
node n685 s33 last -> n687
node n686 s24 last -> n677
node n687 s15 last -> n686
node n688 s12 last -> n684
node n689 s33 last -> n688
node n690 s20 last -> n689
node n691 s14 last -> n690
node n692 s15 last -> n691
node n693 s27 last -> n692
node n694 s2 word -> n632
node n695 s3 word -> n641
node n696 s16 last -> n697
node n697 s27 last -> n594
node n698 s2 word -> n632
node n699 s3 word -> n641
node n700 s14 last -> n666
node n701 s2 word -> n632
node n702 s3 word -> n641
node n703 s12 last -> n664
node n704 s27 -> n594
node n705 s2 word -> n632
node n706 s3 word last -> n641
node n707 s12 -> n597
node n708 s2 word -> n632
node n709 s3 word last -> n641
node n710 s2 word -> n632
node n711 s3 word -> n641
node n712 s15 last -> n607
node n713 s16 -> n607
node n714 s2 word -> n632
node n715 s3 word -> n641
node n716 s15 last -> n686
node n717 s14 last -> n594
node n718 s20 last -> n717
node n719 s15 last -> n718
node n720 s30 last -> n719
node n721 s18 last -> n720
node n722 s35 -> n721
node n723 s24 last -> n727
node n724 s14 last -> n666
node n725 s19 last -> n724
node n726 s12 last -> n725
node n727 s35 last -> n726
node n728 s12 -> n722
node n729 s29 -> n735
node n730 s2 word -> n632
node n731 s3 word last -> n641
node n732 s14 last -> n645
node n733 s15 last -> n732
node n734 s36 last -> n733
node n735 s15 last -> n734
node n736 s12 last -> n666
node n737 s24 -> n736
node n738 s16 -> n744
node n739 s32 -> n745
node n740 s39 -> n747
node n741 s36 -> n750
node n742 s2 word -> n632
node n743 s3 word last -> n641
node n744 s22 last -> n646
node n745 s15 last -> n650
node n746 s38 last -> n594
node n747 s16 -> n746
node n748 s15 last -> n717
node n749 s12 last -> n697
node n750 s12 last -> n749
node n751 s9 -> n508
node n752 s13 last -> n594
node n753 s41 last -> n751
node n754 s27 -> n753
node n755 s38 -> n761
node n756 s2 word -> n632
node n757 s3 word last -> n641
node n758 s9 -> n508
node n759 s24 last -> n760
node n760 s42 last -> n594
node n761 s35 last -> n758
node n762 s2 word -> n632
node n763 s3 word -> n641
node n764 s16 last -> n767
node n765 s16 last -> n594
node n766 s24 last -> n765
node n767 s24 last -> n766
node n768 s12 last -> n664
node n769 s35 last -> n768
node n770 s18 -> n769
node n771 s12 -> n779
node n772 s2 word -> n632
node n773 s3 word last -> n641
node n774 s19 last -> n678
node n775 s14 last -> n774
node n776 s15 last -> n775
node n777 s39 last -> n776
node n778 s27 last -> n777
node n779 s19 last -> n778
node n780 s15 last -> n678
node n781 s14 last -> n780
node n782 s15 last -> n781
node n783 s40 last -> n782
node n784 s11 -> n783
node n785 s2 word -> n632
node n786 s3 word last -> n641
node n787 s16 last -> n767
node n788 s44 -> n787
node n789 s12 -> n796
node n790 s2 word -> n632
node n791 s3 word last -> n641
node n792 s9 -> n508
node n793 s22 -> n594
node n794 s42 last -> n795
node n795 s47 last -> n594
node n796 s20 last -> n792
node n797 s15 -> n594
node n798 s2 word -> n632
node n799 s3 word -> n641
node n800 s12 last -> n801
node n801 s49 last -> n594
node n802 s29 last -> n697
node n803 s26 last -> n802
node n804 s14 last -> n803
node n805 s15 last -> n804
node n806 s16 last -> n805
node n807 s24 -> n806
node n808 s16 last -> n809
node n809 s36 last -> n594
node n810 s29 -> n807
node n811 s12 -> n725
node n812 s16 -> n817
node n813 s2 word -> n632
node n814 s3 word last -> n641
node n815 s12 last -> n650
node n816 s26 last -> n815
node n817 s14 last -> n816
node n818 s13 last -> n594
node n819 s14 last -> n818
node n820 s19 last -> n819
node n821 s27 -> n820
node n822 s2 word -> n632
node n823 s3 word last -> n641
node n824 s16 -> n746
node n825 s15 -> n717
node n826 s2 word -> n632
node n827 s3 word last -> n641
node n828 s34 last -> n594
node n829 s50 last -> n828
node n830 s25 -> n829
node n831 s50 -> n840
node n832 s30 -> n843
node n833 s32 -> n856
node n834 s45 -> n859
node n835 s39 -> n864
node n836 s17 -> n867
node n837 s14 -> n868
node n838 s40 last -> n828
node n839 s33 last -> n594
node n840 s39 -> n839
node n841 s34 last -> n594
node n842 s25 last -> n594
node n843 s48 -> n842
node n844 s51 last -> n845
node n845 s32 last -> n594
node n846 s26 last -> n666
node n847 s23 last -> n846
node n848 s29 last -> n847
node n849 s17 -> n848
node n850 s18 last -> n855
node n851 s41 last -> n594
node n852 s41 last -> n851
node n853 s15 last -> n852
node n854 s32 last -> n853
node n855 s11 last -> n854
node n856 s16 last -> n849
node n857 s27 last -> n855
node n858 s19 last -> n857
node n859 s12 last -> n858
node n860 s29 last -> n855
node n861 s17 last -> n860
node n862 s19 last -> n861
node n863 s14 last -> n862
node n864 s15 last -> n863
node n865 s16 last -> n610
node n866 s26 last -> n865
node n867 s12 last -> n866
node n868 s12 last -> n801
node n869 s2 word -> n632
node n870 s3 word last -> n641
