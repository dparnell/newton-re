# gLex8date: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 "01" "t0"
set s1 "3" "t1"
set s2 "123456789" "t2"
set s3 "0" "t3"
set s4 "0123456789" "t4"
set s5 "12" "t5"
set s6 "456789" "t6"
set s7 "/" "t7"
set s8 "012" "t8"
set s9 "1" "t9"
set s10 "23456789" "t10"
set s11 "789" "t11"
set s12 "0123456" "t12"
set s13 "2" "t13"
set s14 "03456789" "t14"
set s15 "-" "t15"
set s16 "h" "t16"
set s17 "c" "t17"
set s18 "r" "t18"
set s19 "y" "t19"
set s20 "a" "t20"
set s21 "H" "t21"
set s22 "C" "t22"
set s23 "R" "t23"
set s24 "Y" "t24"
set s25 "A" "t25"
set s26 "M" "t26"
set s27 "l" "t27"
set s28 "i" "t28"
set s29 "p" "t29"
set s30 "L" "t30"
set s31 "I" "t31"
set s32 "P" "t32"
set s33 "t" "t33"
set s34 "s" "t34"
set s35 "u" "t35"
set s36 "g" "t36"
set s37 "T" "t37"
set s38 "S" "t38"
set s39 "U" "t39"
set s40 "G" "t40"
set s41 "e" "t41"
set s42 "n" "t42"
set s43 "E" "t43"
set s44 "N" "t44"
set s45 "J" "t45"
set s46 "b" "t46"
set s47 "o" "t47"
set s48 "B" "t48"
set s49 "O" "t49"
set s50 "F" "t50"
set s51 "m" "t51"
set s52 "D" "t52"
set s53 "v" "t53"
set s54 "V" "t54"
set s55 "." "t55"
set s56 "3456789" "t56"
set s57 "," "t57"
set s58 "d" "t58"
set s59 "3456" "t59"
set s60 ",23456789" "t60"
set s61 ",0123456789" "t61"
set s62 "1234" "t62"
set s63 "Q" "t63"
set s64 "4" "t64"
set s65 "56789" "t65"
set s66 "'" "t66"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s9 word -> n1879
node n1 s13 word -> n2582
node n2 s3 -> n2593
node n3 s1 word -> n2599
node n4 s64 word -> n2619
node n5 s65 word -> n2629
node n6 s45 -> n3236
node n7 s50 -> n3274
node n8 s26 -> n3295
node n9 s25 -> n3310
node n10 s38 -> n3347
node n11 s49 -> n3367
node n12 s44 -> n3375
node n13 s52 -> n3379
node n14 s66 -> n3385
node n15 s63 last -> n3390
node n16 s1 word -> n2581
node n17 s3 -> n20
node n18 s5 word -> n21
node n19 s6 word last
node n20 s2 word last
node n21 s4 word last
node n22 s7 -> n16
node n23 s15 last -> n34
node n24 s8 word last
node n25 s9 word -> n24
node n26 s3 -> n20
node n27 s10 word last
node n28 s7 last -> n25
node n29 s4 last -> n28
node n30 s4 -> n29
node n31 s7 last -> n25
node n32 s11 -> n30
node n33 s12 last -> n28
node n34 s9 -> n32
node n35 s13 -> n37
node n36 s14 last -> n29
node n37 s3 -> n30
node n38 s2 last -> n28
node n39 s2 word last -> n22
node n40 s3 -> n39
node n41 s9 word -> n51
node n42 s10 word -> n22
node n43 s45 -> n164
node n44 s50 -> n190
node n45 s26 -> n201
node n46 s25 -> n212
node n47 s38 -> n239
node n48 s49 -> n253
node n49 s44 -> n259
node n50 s52 last -> n263
node n51 s7 -> n16
node n52 s8 word -> n22
node n53 s15 last -> n34
node n54 s16 word last
node n55 s17 last -> n54
node n56 s18 word -> n55
node n57 s19 word last
node n58 s20 -> n56
node n59 s25 last -> n62
node n60 s21 word last
node n61 s22 last -> n60
node n62 s23 word -> n61
node n63 s24 word last
node n64 s26 -> n58
node n65 s25 -> n75
node n66 s45 -> n94
node n67 s49 -> n115
node n68 s50 -> n124
node n69 s52 -> n131
node n70 s44 -> n137
node n71 s38 last -> n142
node n72 s27 word last
node n73 s28 last -> n72
node n74 s18 word last -> n73
node n75 s29 -> n74
node n76 s32 -> n81
node n77 s35 -> n85
node n78 s39 last -> n89
node n79 s30 word last
node n80 s31 last -> n79
node n81 s23 word last -> n80
node n82 s33 word last
node n83 s34 last -> n82
node n84 s35 last -> n83
node n85 s36 word last -> n84
node n86 s37 word last
node n87 s38 last -> n86
node n88 s39 last -> n87
node n89 s40 word last -> n88
node n90 s41 word last
node n91 s42 word -> n90
node n92 s27 word last -> n93
node n93 s19 word last
node n94 s35 -> n91
node n95 s39 -> n99
node n96 s20 -> n105
node n97 s25 last -> n109
node n98 s43 word last
node n99 s44 word -> n98
node n100 s30 word last -> n101
node n101 s24 word last
node n102 s18 last -> n93
node n103 s20 last -> n102
node n104 s35 last -> n103
node n105 s42 word last -> n104
node n106 s23 last -> n101
node n107 s25 last -> n106
node n108 s39 last -> n107
node n109 s44 word last -> n108
node n110 s18 word last
node n111 s41 last -> n110
node n112 s46 last -> n111
node n113 s47 last -> n112
node n114 s33 word last -> n113
node n115 s17 -> n114
node n116 s22 last -> n121
node n117 s23 word last
node n118 s43 last -> n117
node n119 s48 last -> n118
node n120 s49 last -> n119
node n121 s37 word last -> n120
node n122 s18 last -> n104
node n123 s46 word last -> n122
node n124 s41 -> n123
node n125 s43 last -> n127
node n126 s23 last -> n108
node n127 s48 word last -> n126
node n128 s51 last -> n112
node n129 s41 last -> n128
node n130 s17 word last -> n129
node n131 s41 -> n130
node n132 s43 last -> n135
node n133 s26 last -> n119
node n134 s43 last -> n133
node n135 s22 word last -> n134
node n136 s53 word last -> n129
node n137 s47 -> n136
node n138 s49 last -> n139
node n139 s54 word last -> n134
node n140 s33 word last -> n129
node n141 s29 word last -> n140
node n142 s41 -> n141
node n143 s43 last -> n145
node n144 s37 word last -> n134
node n145 s32 word last -> n144
node n146 s7 last -> n64
node n147 s4 last -> n146
node n148 s4 -> n147
node n149 s7 last -> n64
node n150 s11 -> n148
node n151 s12 last -> n146
node n152 s9 -> n150
node n153 s13 -> n155
node n154 s14 last -> n147
node n155 s3 -> n148
node n156 s2 last -> n146
node n157 s15 -> n152
node n158 s35 last -> n162
node n159 s15 last -> n152
node n160 s19 word last -> n159
node n161 s18 last -> n160
node n162 s20 last -> n161
node n163 s42 word last -> n157
node n164 s20 -> n163
node n165 s25 -> n173
node n166 s35 -> n176
node n167 s39 last -> n182
node n168 s15 -> n152
node n169 s39 last -> n172
node n170 s24 word last -> n159
node n171 s23 last -> n170
node n172 s25 last -> n171
node n173 s44 word last -> n168
node n174 s41 word -> n159
node n175 s15 last -> n152
node n176 s42 word -> n174
node n177 s27 word last -> n178
node n178 s19 word -> n159
node n179 s15 last -> n152
node n180 s43 word -> n159
node n181 s15 last -> n152
node n182 s44 word -> n180
node n183 s30 word last -> n184
node n184 s24 word -> n159
node n185 s15 last -> n152
node n186 s35 last -> n162
node n187 s18 -> n186
node n188 s15 last -> n152
node n189 s46 word last -> n187
node n190 s41 -> n189
node n191 s43 last -> n195
node n192 s39 last -> n172
node n193 s23 -> n192
node n194 s15 last -> n152
node n195 s48 word last -> n193
node n196 s16 word last -> n159
node n197 s17 -> n196
node n198 s15 last -> n152
node n199 s18 word -> n197
node n200 s19 word last -> n159
node n201 s20 -> n199
node n202 s25 last -> n206
node n203 s21 word last -> n159
node n204 s22 -> n203
node n205 s15 last -> n152
node n206 s23 word -> n204
node n207 s24 word last -> n159
node n208 s27 word last -> n159
node n209 s28 -> n208
node n210 s15 last -> n152
node n211 s18 word last -> n209
node n212 s29 -> n211
node n213 s32 -> n219
node n214 s35 -> n224
node n215 s39 last -> n229
node n216 s30 word last -> n159
node n217 s31 -> n216
node n218 s15 last -> n152
node n219 s23 word last -> n217
node n220 s33 word last -> n159
node n221 s34 last -> n220
node n222 s35 -> n221
node n223 s15 last -> n152
node n224 s36 word last -> n222
node n225 s37 word last -> n159
node n226 s38 last -> n225
node n227 s39 -> n226
node n228 s15 last -> n152
node n229 s40 word last -> n227
node n230 s15 -> n152
node n231 s33 word last -> n232
node n232 s15 -> n152
node n233 s41 last -> n237
node n234 s18 word last -> n159
node n235 s41 last -> n234
node n236 s46 last -> n235
node n237 s51 last -> n236
node n238 s29 word last -> n230
node n239 s41 -> n238
node n240 s43 last -> n249
node n241 s15 -> n152
node n242 s37 word last -> n243
node n243 s15 -> n152
node n244 s43 last -> n248
node n245 s23 word last -> n159
node n246 s43 last -> n245
node n247 s48 last -> n246
node n248 s26 last -> n247
node n249 s32 word last -> n241
node n250 s15 -> n152
node n251 s47 last -> n236
node n252 s33 word last -> n250
node n253 s17 -> n252
node n254 s22 last -> n257
node n255 s15 -> n152
node n256 s49 last -> n247
node n257 s37 word last -> n255
node n258 s53 word last -> n232
node n259 s47 -> n258
node n260 s49 last -> n261
node n261 s54 word last -> n243
node n262 s17 word last -> n232
node n263 s41 -> n262
node n264 s43 last -> n265
node n265 s22 word last -> n243
node n266 s7 -> n40
node n267 s55 -> n311
node n268 s15 last -> n603
node n269 s55 last -> n16
node n270 s2 last -> n269
node n271 s3 -> n270
node n272 s9 -> n274
node n273 s10 last -> n269
node n274 s55 -> n16
node n275 s8 last -> n269
node n276 s55 last -> n271
node n277 s4 last -> n276
node n278 s4 -> n277
node n279 s55 last -> n271
node n280 s11 -> n278
node n281 s12 last -> n276
node n282 s9 -> n280
node n283 s13 -> n285
node n284 s14 last -> n277
node n285 s3 -> n278
node n286 s2 last -> n276
node n287 s15 last -> n282
node n288 s2 word last -> n287
node n289 s3 -> n288
node n290 s5 word -> n293
node n291 s1 word -> n295
node n292 s6 word last -> n287
node n293 s4 word -> n287
node n294 s15 last -> n282
node n295 s0 word -> n287
node n296 s15 last -> n282
node n297 s55 -> n289
node n298 s15 last -> n305
node n299 s55 last -> n25
node n300 s4 last -> n299
node n301 s4 -> n300
node n302 s55 last -> n25
node n303 s11 -> n301
node n304 s12 last -> n299
node n305 s9 -> n303
node n306 s13 -> n308
node n307 s14 last -> n300
node n308 s3 -> n301
node n309 s2 last -> n299
node n310 s2 word last -> n297
node n311 s3 -> n310
node n312 s9 word -> n322
node n313 s10 word -> n297
node n314 s45 -> n482
node n315 s50 -> n514
node n316 s26 -> n527
node n317 s25 -> n540
node n318 s38 -> n572
node n319 s49 -> n589
node n320 s44 -> n596
node n321 s52 last -> n600
node n322 s55 -> n289
node n323 s8 word -> n297
node n324 s15 last -> n305
node n325 s55 -> n16
node n326 s35 last -> n329
node n327 s19 last -> n269
node n328 s18 last -> n327
node n329 s20 last -> n328
node n330 s42 last -> n325
node n331 s20 -> n330
node n332 s25 -> n340
node n333 s35 -> n343
node n334 s39 last -> n349
node n335 s55 -> n16
node n336 s39 last -> n339
node n337 s24 last -> n269
node n338 s23 last -> n337
node n339 s25 last -> n338
node n340 s44 last -> n335
node n341 s41 -> n269
node n342 s55 last -> n16
node n343 s42 -> n341
node n344 s27 last -> n345
node n345 s19 -> n269
node n346 s55 last -> n16
node n347 s43 -> n269
node n348 s55 last -> n16
node n349 s44 -> n347
node n350 s30 last -> n351
node n351 s24 -> n269
node n352 s55 last -> n16
node n353 s45 -> n331
node n354 s50 -> n365
node n355 s26 -> n376
node n356 s25 -> n387
node n357 s38 -> n414
node n358 s49 -> n428
node n359 s44 -> n434
node n360 s52 last -> n438
node n361 s35 last -> n329
node n362 s18 -> n361
node n363 s55 last -> n16
node n364 s46 last -> n362
node n365 s41 -> n364
node n366 s43 last -> n370
node n367 s39 last -> n339
node n368 s23 -> n367
node n369 s55 last -> n16
node n370 s48 last -> n368
node n371 s16 last -> n269
node n372 s17 -> n371
node n373 s55 last -> n16
node n374 s18 -> n372
node n375 s19 last -> n269
node n376 s20 -> n374
node n377 s25 last -> n381
node n378 s21 last -> n269
node n379 s22 -> n378
node n380 s55 last -> n16
node n381 s23 -> n379
node n382 s24 last -> n269
node n383 s27 last -> n269
node n384 s28 -> n383
node n385 s55 last -> n16
node n386 s18 last -> n384
node n387 s29 -> n386
node n388 s32 -> n394
node n389 s35 -> n399
node n390 s39 last -> n404
node n391 s30 last -> n269
node n392 s31 -> n391
node n393 s55 last -> n16
node n394 s23 last -> n392
node n395 s33 last -> n269
node n396 s34 last -> n395
node n397 s35 -> n396
node n398 s55 last -> n16
node n399 s36 last -> n397
node n400 s37 last -> n269
node n401 s38 last -> n400
node n402 s39 -> n401
node n403 s55 last -> n16
node n404 s40 last -> n402
node n405 s55 -> n16
node n406 s33 last -> n407
node n407 s55 -> n16
node n408 s41 last -> n412
node n409 s18 last -> n269
node n410 s41 last -> n409
node n411 s46 last -> n410
node n412 s51 last -> n411
node n413 s29 last -> n405
node n414 s41 -> n413
node n415 s43 last -> n424
node n416 s55 -> n16
node n417 s37 last -> n418
node n418 s55 -> n16
node n419 s43 last -> n423
node n420 s23 last -> n269
node n421 s43 last -> n420
node n422 s48 last -> n421
node n423 s26 last -> n422
node n424 s32 last -> n416
node n425 s55 -> n16
node n426 s47 last -> n411
node n427 s33 last -> n425
node n428 s17 -> n427
node n429 s22 last -> n432
node n430 s55 -> n16
node n431 s49 last -> n422
node n432 s37 last -> n430
node n433 s53 last -> n407
node n434 s47 -> n433
node n435 s49 last -> n436
node n436 s54 last -> n418
node n437 s17 last -> n407
node n438 s41 -> n437
node n439 s43 last -> n440
node n440 s22 last -> n418
node n441 s55 last -> n353
node n442 s4 last -> n441
node n443 s4 -> n442
node n444 s55 last -> n353
node n445 s11 -> n443
node n446 s12 last -> n441
node n447 s9 -> n445
node n448 s13 -> n450
node n449 s14 last -> n442
node n450 s3 -> n443
node n451 s2 last -> n441
node n452 s15 last -> n447
node n453 s2 word last -> n452
node n454 s3 -> n453
node n455 s5 word -> n458
node n456 s1 word -> n460
node n457 s6 word last -> n452
node n458 s4 word -> n452
node n459 s15 last -> n447
node n460 s0 word -> n452
node n461 s15 last -> n447
node n462 s55 -> n454
node n463 s15 -> n471
node n464 s35 last -> n480
node n465 s55 last -> n64
node n466 s4 last -> n465
node n467 s4 -> n466
node n468 s55 last -> n64
node n469 s11 -> n467
node n470 s12 last -> n465
node n471 s9 -> n469
node n472 s13 -> n474
node n473 s14 last -> n466
node n474 s3 -> n467
node n475 s2 last -> n465
node n476 s55 -> n454
node n477 s15 last -> n471
node n478 s19 word last -> n476
node n479 s18 last -> n478
node n480 s20 last -> n479
node n481 s42 word last -> n462
node n482 s20 -> n481
node n483 s25 -> n492
node n484 s35 -> n496
node n485 s39 last -> n504
node n486 s55 -> n454
node n487 s15 -> n471
node n488 s39 last -> n491
node n489 s24 word last -> n476
node n490 s23 last -> n489
node n491 s25 last -> n490
node n492 s44 word last -> n486
node n493 s41 word -> n476
node n494 s55 -> n454
node n495 s15 last -> n471
node n496 s42 word -> n493
node n497 s27 word last -> n498
node n498 s19 word -> n476
node n499 s55 -> n454
node n500 s15 last -> n471
node n501 s43 word -> n476
node n502 s55 -> n454
node n503 s15 last -> n471
node n504 s44 word -> n501
node n505 s30 word last -> n506
node n506 s24 word -> n476
node n507 s55 -> n454
node n508 s15 last -> n471
node n509 s35 last -> n480
node n510 s18 -> n509
node n511 s55 -> n454
node n512 s15 last -> n471
node n513 s46 word last -> n510
node n514 s41 -> n513
node n515 s43 last -> n520
node n516 s39 last -> n491
node n517 s23 -> n516
node n518 s55 -> n454
node n519 s15 last -> n471
node n520 s48 word last -> n517
node n521 s16 word last -> n476
node n522 s17 -> n521
node n523 s55 -> n454
node n524 s15 last -> n471
node n525 s18 word -> n522
node n526 s19 word last -> n476
node n527 s20 -> n525
node n528 s25 last -> n533
node n529 s21 word last -> n476
node n530 s22 -> n529
node n531 s55 -> n454
node n532 s15 last -> n471
node n533 s23 word -> n530
node n534 s24 word last -> n476
node n535 s27 word last -> n476
node n536 s28 -> n535
node n537 s55 -> n454
node n538 s15 last -> n471
node n539 s18 word last -> n536
node n540 s29 -> n539
node n541 s32 -> n548
node n542 s35 -> n554
node n543 s39 last -> n560
node n544 s30 word last -> n476
node n545 s31 -> n544
node n546 s55 -> n454
node n547 s15 last -> n471
node n548 s23 word last -> n545
node n549 s33 word last -> n476
node n550 s34 last -> n549
node n551 s35 -> n550
node n552 s55 -> n454
node n553 s15 last -> n471
node n554 s36 word last -> n551
node n555 s37 word last -> n476
node n556 s38 last -> n555
node n557 s39 -> n556
node n558 s55 -> n454
node n559 s15 last -> n471
node n560 s40 word last -> n557
node n561 s55 -> n454
node n562 s15 -> n471
node n563 s33 word last -> n564
node n564 s55 -> n454
node n565 s15 -> n471
node n566 s41 last -> n570
node n567 s18 word last -> n476
node n568 s41 last -> n567
node n569 s46 last -> n568
node n570 s51 last -> n569
node n571 s29 word last -> n561
node n572 s41 -> n571
node n573 s43 last -> n584
node n574 s55 -> n454
node n575 s15 -> n471
node n576 s37 word last -> n577
node n577 s55 -> n454
node n578 s15 -> n471
node n579 s43 last -> n583
node n580 s23 word last -> n476
node n581 s43 last -> n580
node n582 s48 last -> n581
node n583 s26 last -> n582
node n584 s32 word last -> n574
node n585 s55 -> n454
node n586 s15 -> n471
node n587 s47 last -> n569
node n588 s33 word last -> n585
node n589 s17 -> n588
node n590 s22 last -> n594
node n591 s55 -> n454
node n592 s15 -> n471
node n593 s49 last -> n582
node n594 s37 word last -> n591
node n595 s53 word last -> n564
node n596 s47 -> n595
node n597 s49 last -> n598
node n598 s54 word last -> n577
node n599 s17 word last -> n564
node n600 s41 -> n599
node n601 s43 last -> n602
node n602 s22 word last -> n577
node n603 s56 word -> n21
node n604 s3 -> n21
node n605 s9 word -> n616
node n606 s13 word -> n618
node n607 s26 -> n58
node n608 s25 -> n75
node n609 s45 -> n94
node n610 s49 -> n115
node n611 s50 -> n124
node n612 s52 -> n131
node n613 s44 -> n137
node n614 s38 last -> n142
node n615 s4 last -> n21
node n616 s11 word -> n615
node n617 s12 word last
node n618 s3 word -> n615
node n619 s2 word last
node n620 s4 word -> n266
node n621 s57 -> n681
node n622 s48 -> n666
node n623 s46 -> n670
node n624 s25 -> n673
node n625 s20 last -> n676
node n626 s57 -> n681
node n627 s48 -> n666
node n628 s46 -> n670
node n629 s25 -> n673
node n630 s20 last -> n676
node n631 s57 -> n651
node n632 s48 -> n638
node n633 s46 -> n641
node n634 s25 -> n644
node n635 s20 last -> n647
node n636 s55 word last
node n637 s22 last -> n636
node n638 s55 -> n637
node n639 s22 word last
node n640 s17 last -> n636
node n641 s55 -> n640
node n642 s17 word last
node n643 s52 last -> n636
node n644 s55 -> n643
node n645 s52 word last
node n646 s58 last -> n636
node n647 s55 -> n646
node n648 s58 word last
node n649 s4 last -> n631
node n650 s4 last -> n649
node n651 s4 last -> n650
node n652 s57 -> n651
node n653 s48 -> n638
node n654 s46 -> n641
node n655 s25 -> n644
node n656 s20 -> n647
node n657 s4 last -> n658
node n658 s57 -> n651
node n659 s48 -> n638
node n660 s46 -> n641
node n661 s25 -> n644
node n662 s20 -> n647
node n663 s4 last -> n631
node n664 s2 last -> n652
node n665 s15 last -> n664
node n666 s22 word -> n665
node n667 s55 last -> n669
node n668 s55 word last -> n665
node n669 s22 last -> n668
node n670 s17 word -> n665
node n671 s55 last -> n672
node n672 s17 last -> n668
node n673 s52 word -> n665
node n674 s55 last -> n675
node n675 s52 last -> n668
node n676 s58 word -> n665
node n677 s55 last -> n678
node n678 s58 last -> n668
node n679 s4 last -> n626
node n680 s4 last -> n679
node n681 s4 last -> n680
node n682 s4 -> n620
node n683 s7 -> n746
node n684 s57 word -> n681
node n685 s15 -> n1051
node n686 s55 -> n1302
node n687 s48 -> n666
node n688 s46 -> n670
node n689 s25 -> n673
node n690 s20 last -> n676
node n691 s14 -> n21
node n692 s9 -> n616
node n693 s13 last -> n618
node n694 s7 last -> n691
node n695 s2 word last -> n694
node n696 s3 -> n695
node n697 s9 word -> n699
node n698 s10 word last -> n694
node n699 s7 -> n691
node n700 s8 word last -> n694
node n701 s7 last -> n696
node n702 s2 last -> n701
node n703 s3 -> n702
node n704 s5 -> n707
node n705 s1 -> n709
node n706 s6 last -> n701
node n707 s4 -> n701
node n708 s7 last -> n696
node n709 s0 -> n701
node n710 s7 last -> n696
node n711 s15 last -> n703
node n712 s12 word -> n711
node n713 s11 word last -> n714
node n714 s15 -> n703
node n715 s4 last -> n716
node n716 s4 word last -> n711
node n717 s9 word -> n712
node n718 s13 word -> n721
node n719 s56 word -> n716
node n720 s3 last -> n716
node n721 s2 word -> n711
node n722 s3 word last -> n714
node n723 s7 -> n717
node n724 s15 last -> n727
node n725 s3 -> n28
node n726 s2 last -> n701
node n727 s3 -> n725
node n728 s9 -> n734
node n729 s13 -> n737
node n730 s1 -> n740
node n731 s6 last -> n743
node n732 s7 -> n696
node n733 s4 last -> n29
node n734 s11 -> n732
node n735 s7 -> n696
node n736 s12 last -> n701
node n737 s3 -> n732
node n738 s7 -> n696
node n739 s2 last -> n701
node n740 s10 -> n28
node n741 s7 -> n696
node n742 s0 last -> n701
node n743 s4 -> n28
node n744 s7 last -> n696
node n745 s2 word last -> n723
node n746 s3 -> n745
node n747 s9 word -> n757
node n748 s10 word -> n723
node n749 s45 -> n926
node n750 s50 -> n958
node n751 s26 -> n971
node n752 s25 -> n984
node n753 s38 -> n1016
node n754 s49 -> n1033
node n755 s44 -> n1040
node n756 s52 last -> n1044
node n757 s7 -> n717
node n758 s8 word -> n723
node n759 s15 last -> n727
node n760 s7 -> n691
node n761 s41 word last -> n694
node n762 s42 word -> n760
node n763 s27 word last -> n764
node n764 s7 -> n691
node n765 s19 word last -> n694
node n766 s35 -> n762
node n767 s39 -> n772
node n768 s20 -> n781
node n769 s25 last -> n787
node n770 s7 -> n691
node n771 s43 word last -> n694
node n772 s44 word -> n770
node n773 s30 word last -> n774
node n774 s7 -> n691
node n775 s24 word last -> n694
node n776 s19 word last -> n694
node n777 s18 last -> n776
node n778 s20 last -> n777
node n779 s35 -> n778
node n780 s7 last -> n691
node n781 s42 word last -> n779
node n782 s24 word last -> n694
node n783 s23 last -> n782
node n784 s25 last -> n783
node n785 s39 -> n784
node n786 s7 last -> n691
node n787 s44 word last -> n785
node n788 s45 -> n766
node n789 s26 -> n801
node n790 s49 -> n814
node n791 s25 -> n826
node n792 s50 -> n848
node n793 s52 -> n858
node n794 s44 -> n865
node n795 s38 last -> n871
node n796 s16 word last -> n694
node n797 s17 -> n796
node n798 s7 last -> n691
node n799 s18 word -> n797
node n800 s19 word last -> n694
node n801 s20 -> n799
node n802 s25 last -> n806
node n803 s21 word last -> n694
node n804 s22 -> n803
node n805 s7 last -> n691
node n806 s23 word -> n804
node n807 s24 word last -> n694
node n808 s18 word last -> n694
node n809 s41 last -> n808
node n810 s46 last -> n809
node n811 s47 -> n810
node n812 s7 last -> n691
node n813 s33 word last -> n811
node n814 s17 -> n813
node n815 s22 last -> n821
node n816 s23 word last -> n694
node n817 s43 last -> n816
node n818 s48 last -> n817
node n819 s49 -> n818
node n820 s7 last -> n691
node n821 s37 word last -> n819
node n822 s27 word last -> n694
node n823 s28 -> n822
node n824 s7 last -> n691
node n825 s18 word last -> n823
node n826 s29 -> n825
node n827 s32 -> n833
node n828 s35 -> n838
node n829 s39 last -> n843
node n830 s30 word last -> n694
node n831 s31 -> n830
node n832 s7 last -> n691
node n833 s23 word last -> n831
node n834 s33 word last -> n694
node n835 s34 last -> n834
node n836 s35 -> n835
node n837 s7 last -> n691
node n838 s36 word last -> n836
node n839 s37 word last -> n694
node n840 s38 last -> n839
node n841 s39 -> n840
node n842 s7 last -> n691
node n843 s40 word last -> n841
node n844 s35 last -> n778
node n845 s18 -> n844
node n846 s7 last -> n691
node n847 s46 word last -> n845
node n848 s41 -> n847
node n849 s43 last -> n853
node n850 s39 last -> n784
node n851 s23 -> n850
node n852 s7 last -> n691
node n853 s48 word last -> n851
node n854 s51 last -> n810
node n855 s41 -> n854
node n856 s7 last -> n691
node n857 s17 word last -> n855
node n858 s41 -> n857
node n859 s43 last -> n863
node n860 s26 last -> n818
node n861 s43 -> n860
node n862 s7 last -> n691
node n863 s22 word last -> n861
node n864 s53 word last -> n855
node n865 s47 -> n864
node n866 s49 last -> n867
node n867 s54 word last -> n861
node n868 s7 -> n691
node n869 s33 word last -> n855
node n870 s29 word last -> n868
node n871 s41 -> n870
node n872 s43 last -> n875
node n873 s7 -> n691
node n874 s37 word last -> n861
node n875 s32 word last -> n873
node n876 s7 last -> n788
node n877 s2 last -> n876
node n878 s3 -> n877
node n879 s5 -> n882
node n880 s1 -> n884
node n881 s6 last -> n876
node n882 s7 -> n788
node n883 s4 last -> n876
node n884 s7 -> n788
node n885 s0 last -> n876
node n886 s15 last -> n878
node n887 s4 word last -> n886
node n888 s4 -> n887
node n889 s15 last -> n878
node n890 s11 word -> n888
node n891 s12 word last -> n886
node n892 s9 -> n890
node n893 s13 -> n895
node n894 s14 last -> n887
node n895 s3 word -> n888
node n896 s2 word last -> n886
node n897 s7 -> n892
node n898 s15 -> n902
node n899 s35 last -> n924
node n900 s2 -> n876
node n901 s3 last -> n146
node n902 s3 -> n900
node n903 s9 -> n907
node n904 s13 -> n912
node n905 s1 -> n915
node n906 s6 last -> n918
node n907 s12 -> n876
node n908 s11 -> n910
node n909 s7 last -> n788
node n910 s4 -> n147
node n911 s7 last -> n788
node n912 s2 -> n876
node n913 s7 -> n788
node n914 s3 last -> n910
node n915 s7 -> n788
node n916 s0 -> n876
node n917 s10 last -> n146
node n918 s7 -> n788
node n919 s4 last -> n146
node n920 s7 -> n892
node n921 s15 last -> n902
node n922 s19 word last -> n920
node n923 s18 last -> n922
node n924 s20 last -> n923
node n925 s42 word last -> n897
node n926 s20 -> n925
node n927 s25 -> n936
node n928 s35 -> n940
node n929 s39 last -> n948
node n930 s7 -> n892
node n931 s15 -> n902
node n932 s39 last -> n935
node n933 s24 word last -> n920
node n934 s23 last -> n933
node n935 s25 last -> n934
node n936 s44 word last -> n930
node n937 s41 word -> n920
node n938 s7 -> n892
node n939 s15 last -> n902
node n940 s42 word -> n937
node n941 s27 word last -> n942
node n942 s19 word -> n920
node n943 s7 -> n892
node n944 s15 last -> n902
node n945 s43 word -> n920
node n946 s7 -> n892
node n947 s15 last -> n902
node n948 s44 word -> n945
node n949 s30 word last -> n950
node n950 s24 word -> n920
node n951 s7 -> n892
node n952 s15 last -> n902
node n953 s35 last -> n924
node n954 s18 -> n953
node n955 s7 -> n892
node n956 s15 last -> n902
node n957 s46 word last -> n954
node n958 s41 -> n957
node n959 s43 last -> n964
node n960 s39 last -> n935
node n961 s23 -> n960
node n962 s7 -> n892
node n963 s15 last -> n902
node n964 s48 word last -> n961
node n965 s16 word last -> n920
node n966 s17 -> n965
node n967 s7 -> n892
node n968 s15 last -> n902
node n969 s18 word -> n966
node n970 s19 word last -> n920
node n971 s20 -> n969
node n972 s25 last -> n977
node n973 s21 word last -> n920
node n974 s22 -> n973
node n975 s7 -> n892
node n976 s15 last -> n902
node n977 s23 word -> n974
node n978 s24 word last -> n920
node n979 s27 word last -> n920
node n980 s28 -> n979
node n981 s7 -> n892
node n982 s15 last -> n902
node n983 s18 word last -> n980
node n984 s29 -> n983
node n985 s32 -> n992
node n986 s35 -> n998
node n987 s39 last -> n1004
node n988 s30 word last -> n920
node n989 s31 -> n988
node n990 s7 -> n892
node n991 s15 last -> n902
node n992 s23 word last -> n989
node n993 s33 word last -> n920
node n994 s34 last -> n993
node n995 s35 -> n994
node n996 s7 -> n892
node n997 s15 last -> n902
node n998 s36 word last -> n995
node n999 s37 word last -> n920
node n1000 s38 last -> n999
node n1001 s39 -> n1000
node n1002 s7 -> n892
node n1003 s15 last -> n902
node n1004 s40 word last -> n1001
node n1005 s7 -> n892
node n1006 s15 -> n902
node n1007 s33 word last -> n1008
node n1008 s7 -> n892
node n1009 s15 -> n902
node n1010 s41 last -> n1014
node n1011 s18 word last -> n920
node n1012 s41 last -> n1011
node n1013 s46 last -> n1012
node n1014 s51 last -> n1013
node n1015 s29 word last -> n1005
node n1016 s41 -> n1015
node n1017 s43 last -> n1028
node n1018 s7 -> n892
node n1019 s15 -> n902
node n1020 s37 word last -> n1021
node n1021 s7 -> n892
node n1022 s15 -> n902
node n1023 s43 last -> n1027
node n1024 s23 word last -> n920
node n1025 s43 last -> n1024
node n1026 s48 last -> n1025
node n1027 s26 last -> n1026
node n1028 s32 word last -> n1018
node n1029 s7 -> n892
node n1030 s15 -> n902
node n1031 s47 last -> n1013
node n1032 s33 word last -> n1029
node n1033 s17 -> n1032
node n1034 s22 last -> n1038
node n1035 s7 -> n892
node n1036 s15 -> n902
node n1037 s49 last -> n1026
node n1038 s37 word last -> n1035
node n1039 s53 word last -> n1008
node n1040 s47 -> n1039
node n1041 s49 last -> n1042
node n1042 s54 word last -> n1021
node n1043 s17 word last -> n1008
node n1044 s41 -> n1043
node n1045 s43 last -> n1046
node n1046 s22 word last -> n1021
node n1047 s15 -> n691
node n1048 s57 word last
node n1049 s2 word -> n1047
node n1050 s3 word last
node n1051 s3 -> n1049
node n1052 s9 word -> n1065
node n1053 s13 word -> n1072
node n1054 s1 word -> n1076
node n1055 s45 -> n1086
node n1056 s26 -> n1113
node n1057 s49 -> n1126
node n1058 s25 -> n1138
node n1059 s6 word -> n1156
node n1060 s50 -> n1162
node n1061 s52 -> n1172
node n1062 s44 -> n1179
node n1063 s38 last -> n1185
node n1064 s57 word last
node n1065 s59 word -> n1064
node n1066 s11 word -> n1070
node n1067 s15 -> n691
node n1068 s8 word -> n1047
node n1069 s57 word last
node n1070 s4 -> n21
node n1071 s57 word last
node n1072 s2 word -> n1064
node n1073 s3 word -> n1070
node n1074 s15 -> n691
node n1075 s57 word last
node n1076 s0 word -> n1064
node n1077 s15 -> n691
node n1078 s60 word last
node n1079 s15 -> n691
node n1080 s41 word last -> n1081
node n1081 s15 last -> n691
node n1082 s42 word -> n1079
node n1083 s27 word last -> n1084
node n1084 s15 -> n691
node n1085 s19 word last -> n1081
node n1086 s35 -> n1082
node n1087 s39 -> n1092
node n1088 s20 -> n1101
node n1089 s25 last -> n1107
node n1090 s15 -> n691
node n1091 s43 word last -> n1081
node n1092 s44 word -> n1090
node n1093 s30 word last -> n1094
node n1094 s15 -> n691
node n1095 s24 word last -> n1081
node n1096 s19 word last -> n1081
node n1097 s18 last -> n1096
node n1098 s20 last -> n1097
node n1099 s35 -> n1098
node n1100 s15 last -> n691
node n1101 s42 word last -> n1099
node n1102 s24 word last -> n1081
node n1103 s23 last -> n1102
node n1104 s25 last -> n1103
node n1105 s39 -> n1104
node n1106 s15 last -> n691
node n1107 s44 word last -> n1105
node n1108 s16 word last -> n1081
node n1109 s17 -> n1108
node n1110 s15 last -> n691
node n1111 s18 word -> n1109
node n1112 s19 word last -> n1081
node n1113 s20 -> n1111
node n1114 s25 last -> n1118
node n1115 s21 word last -> n1081
node n1116 s22 -> n1115
node n1117 s15 last -> n691
node n1118 s23 word -> n1116
node n1119 s24 word last -> n1081
node n1120 s18 word last -> n1081
node n1121 s41 last -> n1120
node n1122 s46 last -> n1121
node n1123 s47 -> n1122
node n1124 s15 last -> n691
node n1125 s33 word last -> n1123
node n1126 s17 -> n1125
node n1127 s22 last -> n1133
node n1128 s23 word last -> n1081
node n1129 s43 last -> n1128
node n1130 s48 last -> n1129
node n1131 s49 -> n1130
node n1132 s15 last -> n691
node n1133 s37 word last -> n1131
node n1134 s27 word last -> n1081
node n1135 s28 -> n1134
node n1136 s15 last -> n691
node n1137 s18 word last -> n1135
node n1138 s29 -> n1137
node n1139 s32 -> n1145
node n1140 s35 -> n1150
node n1141 s39 last -> n1155
node n1142 s30 word last -> n1081
node n1143 s31 -> n1142
node n1144 s15 last -> n691
node n1145 s23 word last -> n1143
node n1146 s33 word last -> n1081
node n1147 s34 last -> n1146
node n1148 s35 -> n1147
node n1149 s15 last -> n691
node n1150 s36 word last -> n1148
node n1151 s37 word last -> n1081
node n1152 s38 last -> n1151
node n1153 s39 -> n1152
node n1154 s15 last -> n691
node n1155 s40 word last -> n1153
node n1156 s15 -> n691
node n1157 s61 word last
node n1158 s35 last -> n1098
node n1159 s18 -> n1158
node n1160 s15 last -> n691
node n1161 s46 word last -> n1159
node n1162 s41 -> n1161
node n1163 s43 last -> n1167
node n1164 s39 last -> n1104
node n1165 s23 -> n1164
node n1166 s15 last -> n691
node n1167 s48 word last -> n1165
node n1168 s51 last -> n1122
node n1169 s41 -> n1168
node n1170 s15 last -> n691
node n1171 s17 word last -> n1169
node n1172 s41 -> n1171
node n1173 s43 last -> n1177
node n1174 s26 last -> n1130
node n1175 s43 -> n1174
node n1176 s15 last -> n691
node n1177 s22 word last -> n1175
node n1178 s53 word last -> n1169
node n1179 s47 -> n1178
node n1180 s49 last -> n1181
node n1181 s54 word last -> n1175
node n1182 s15 -> n691
node n1183 s33 word last -> n1169
node n1184 s29 word last -> n1182
node n1185 s41 -> n1184
node n1186 s43 last -> n1189
node n1187 s15 -> n691
node n1188 s37 word last -> n1175
node n1189 s32 word last -> n1187
node n1190 s55 last -> n691
node n1191 s2 word last -> n1190
node n1192 s3 -> n1191
node n1193 s9 word -> n1195
node n1194 s10 word last -> n1190
node n1195 s55 -> n691
node n1196 s8 word last -> n1190
node n1197 s55 last -> n1192
node n1198 s2 last -> n1197
node n1199 s3 -> n1198
node n1200 s5 -> n1203
node n1201 s1 -> n1205
node n1202 s6 last -> n1197
node n1203 s4 -> n1197
node n1204 s55 last -> n1192
node n1205 s0 -> n1197
node n1206 s55 last -> n1192
node n1207 s15 last -> n1199
node n1208 s4 word last -> n1207
node n1209 s4 -> n1208
node n1210 s15 last -> n1225
node n1211 s56 word -> n21
node n1212 s3 -> n21
node n1213 s9 word -> n616
node n1214 s13 word last -> n618
node n1215 s55 last -> n1211
node n1216 s2 word last -> n1215
node n1217 s3 -> n1216
node n1218 s9 word -> n1220
node n1219 s10 word last -> n1215
node n1220 s55 -> n1211
node n1221 s8 word last -> n1215
node n1222 s55 last -> n1217
node n1223 s2 -> n1222
node n1224 s3 last -> n276
node n1225 s3 -> n1223
node n1226 s9 -> n1230
node n1227 s13 -> n1235
node n1228 s1 -> n1238
node n1229 s6 last -> n1241
node n1230 s12 -> n1222
node n1231 s11 -> n1233
node n1232 s55 last -> n1192
node n1233 s55 -> n1217
node n1234 s4 last -> n277
node n1235 s2 -> n1222
node n1236 s3 -> n1233
node n1237 s55 last -> n1192
node n1238 s0 -> n1222
node n1239 s10 -> n276
node n1240 s55 last -> n1192
node n1241 s4 -> n276
node n1242 s55 last -> n1192
node n1243 s11 word -> n1209
node n1244 s12 word -> n1246
node n1245 s15 last -> n282
node n1246 s15 last -> n1225
node n1247 s9 word -> n1243
node n1248 s13 word -> n1253
node n1249 s3 -> n1256
node n1250 s1 word -> n1258
node n1251 s6 word -> n1261
node n1252 s15 last -> n1271
node n1253 s3 word -> n1209
node n1254 s2 word -> n1246
node n1255 s15 last -> n282
node n1256 s3 word -> n1207
node n1257 s2 word last -> n1246
node n1258 s0 word -> n1246
node n1259 s10 word -> n1207
node n1260 s15 last -> n282
node n1261 s4 word -> n1207
node n1262 s15 last -> n282
node n1263 s2 last -> n636
node n1264 s3 -> n1263
node n1265 s9 -> n1267
node n1266 s10 last -> n636
node n1267 s8 -> n636
node n1268 s55 word last
node n1269 s55 last -> n1264
node n1270 s2 last -> n1269
node n1271 s3 -> n1270
node n1272 s5 -> n1275
node n1273 s1 -> n1277
node n1274 s6 last -> n1269
node n1275 s4 -> n1269
node n1276 s55 last -> n1264
node n1277 s55 -> n1264
node n1278 s0 last -> n1269
node n1279 s55 word -> n1247
node n1280 s15 last -> n1283
node n1281 s3 -> n299
node n1282 s2 last -> n1197
node n1283 s3 -> n1281
node n1284 s9 -> n1290
node n1285 s13 -> n1293
node n1286 s1 -> n1296
node n1287 s6 last -> n1299
node n1288 s55 -> n1192
node n1289 s4 last -> n300
node n1290 s11 -> n1288
node n1291 s55 -> n1192
node n1292 s12 last -> n1197
node n1293 s3 -> n1288
node n1294 s55 -> n1192
node n1295 s2 last -> n1197
node n1296 s10 -> n299
node n1297 s55 -> n1192
node n1298 s0 last -> n1197
node n1299 s4 -> n299
node n1300 s55 last -> n1192
node n1301 s2 word last -> n1279
node n1302 s3 -> n1301
node n1303 s9 word -> n1313
node n1304 s10 word -> n1279
node n1305 s45 -> n1758
node n1306 s50 -> n1790
node n1307 s26 -> n1803
node n1308 s25 -> n1816
node n1309 s38 -> n1848
node n1310 s49 -> n1865
node n1311 s44 -> n1872
node n1312 s52 last -> n1876
node n1313 s55 word -> n1247
node n1314 s8 word -> n1279
node n1315 s15 last -> n1283
node n1316 s55 -> n691
node n1317 s41 word last -> n1190
node n1318 s42 word -> n1316
node n1319 s27 word last -> n1320
node n1320 s55 -> n691
node n1321 s19 word last -> n1190
node n1322 s35 -> n1318
node n1323 s39 -> n1328
node n1324 s20 -> n1337
node n1325 s25 last -> n1343
node n1326 s55 -> n691
node n1327 s43 word last -> n1190
node n1328 s44 word -> n1326
node n1329 s30 word last -> n1330
node n1330 s55 -> n691
node n1331 s24 word last -> n1190
node n1332 s19 word last -> n1190
node n1333 s18 last -> n1332
node n1334 s20 last -> n1333
node n1335 s35 -> n1334
node n1336 s55 last -> n691
node n1337 s42 word last -> n1335
node n1338 s24 word last -> n1190
node n1339 s23 last -> n1338
node n1340 s25 last -> n1339
node n1341 s39 -> n1340
node n1342 s55 last -> n691
node n1343 s44 word last -> n1341
node n1344 s45 -> n1322
node n1345 s26 -> n1357
node n1346 s49 -> n1370
node n1347 s25 -> n1382
node n1348 s50 -> n1404
node n1349 s52 -> n1414
node n1350 s44 -> n1421
node n1351 s38 last -> n1427
node n1352 s16 word last -> n1190
node n1353 s17 -> n1352
node n1354 s55 last -> n691
node n1355 s18 word -> n1353
node n1356 s19 word last -> n1190
node n1357 s20 -> n1355
node n1358 s25 last -> n1362
node n1359 s21 word last -> n1190
node n1360 s22 -> n1359
node n1361 s55 last -> n691
node n1362 s23 word -> n1360
node n1363 s24 word last -> n1190
node n1364 s18 word last -> n1190
node n1365 s41 last -> n1364
node n1366 s46 last -> n1365
node n1367 s47 -> n1366
node n1368 s55 last -> n691
node n1369 s33 word last -> n1367
node n1370 s17 -> n1369
node n1371 s22 last -> n1377
node n1372 s23 word last -> n1190
node n1373 s43 last -> n1372
node n1374 s48 last -> n1373
node n1375 s49 -> n1374
node n1376 s55 last -> n691
node n1377 s37 word last -> n1375
node n1378 s27 word last -> n1190
node n1379 s28 -> n1378
node n1380 s55 last -> n691
node n1381 s18 word last -> n1379
node n1382 s29 -> n1381
node n1383 s32 -> n1389
node n1384 s35 -> n1394
node n1385 s39 last -> n1399
node n1386 s30 word last -> n1190
node n1387 s31 -> n1386
node n1388 s55 last -> n691
node n1389 s23 word last -> n1387
node n1390 s33 word last -> n1190
node n1391 s34 last -> n1390
node n1392 s35 -> n1391
node n1393 s55 last -> n691
node n1394 s36 word last -> n1392
node n1395 s37 word last -> n1190
node n1396 s38 last -> n1395
node n1397 s39 -> n1396
node n1398 s55 last -> n691
node n1399 s40 word last -> n1397
node n1400 s35 last -> n1334
node n1401 s18 -> n1400
node n1402 s55 last -> n691
node n1403 s46 word last -> n1401
node n1404 s41 -> n1403
node n1405 s43 last -> n1409
node n1406 s39 last -> n1340
node n1407 s23 -> n1406
node n1408 s55 last -> n691
node n1409 s48 word last -> n1407
node n1410 s51 last -> n1366
node n1411 s41 -> n1410
node n1412 s55 last -> n691
node n1413 s17 word last -> n1411
node n1414 s41 -> n1413
node n1415 s43 last -> n1419
node n1416 s26 last -> n1374
node n1417 s43 -> n1416
node n1418 s55 last -> n691
node n1419 s22 word last -> n1417
node n1420 s53 word last -> n1411
node n1421 s47 -> n1420
node n1422 s49 last -> n1423
node n1423 s54 word last -> n1417
node n1424 s55 -> n691
node n1425 s33 word last -> n1411
node n1426 s29 word last -> n1424
node n1427 s41 -> n1426
node n1428 s43 last -> n1431
node n1429 s55 -> n691
node n1430 s37 word last -> n1417
node n1431 s32 word last -> n1429
node n1432 s55 last -> n1344
node n1433 s2 last -> n1432
node n1434 s3 -> n1433
node n1435 s5 -> n1438
node n1436 s1 -> n1440
node n1437 s6 last -> n1432
node n1438 s55 -> n1344
node n1439 s4 last -> n1432
node n1440 s55 -> n1344
node n1441 s0 last -> n1432
node n1442 s15 last -> n1434
node n1443 s4 word last -> n1442
node n1444 s4 -> n1443
node n1445 s15 last -> n1565
node n1446 s19 word last -> n1215
node n1447 s18 last -> n1446
node n1448 s20 last -> n1447
node n1449 s35 -> n1448
node n1450 s55 last -> n1211
node n1451 s42 word last -> n1449
node n1452 s20 -> n1451
node n1453 s25 -> n1461
node n1454 s35 -> n1464
node n1455 s39 last -> n1470
node n1456 s24 word last -> n1215
node n1457 s23 last -> n1456
node n1458 s25 last -> n1457
node n1459 s39 -> n1458
node n1460 s55 last -> n1211
node n1461 s44 word last -> n1459
node n1462 s41 word -> n1215
node n1463 s55 last -> n1211
node n1464 s42 word -> n1462
node n1465 s27 word last -> n1466
node n1466 s19 word -> n1215
node n1467 s55 last -> n1211
node n1468 s43 word -> n1215
node n1469 s55 last -> n1211
node n1470 s44 word -> n1468
node n1471 s30 word last -> n1472
node n1472 s24 word -> n1215
node n1473 s55 last -> n1211
node n1474 s45 -> n1452
node n1475 s50 -> n1486
node n1476 s26 -> n1497
node n1477 s25 -> n1508
node n1478 s38 -> n1535
node n1479 s49 -> n1549
node n1480 s44 -> n1555
node n1481 s52 last -> n1559
node n1482 s35 last -> n1448
node n1483 s18 -> n1482
node n1484 s55 last -> n1211
node n1485 s46 word last -> n1483
node n1486 s41 -> n1485
node n1487 s43 last -> n1491
node n1488 s39 last -> n1458
node n1489 s23 -> n1488
node n1490 s55 last -> n1211
node n1491 s48 word last -> n1489
node n1492 s16 word last -> n1215
node n1493 s17 -> n1492
node n1494 s55 last -> n1211
node n1495 s18 word -> n1493
node n1496 s19 word last -> n1215
node n1497 s20 -> n1495
node n1498 s25 last -> n1502
node n1499 s21 word last -> n1215
node n1500 s22 -> n1499
node n1501 s55 last -> n1211
node n1502 s23 word -> n1500
node n1503 s24 word last -> n1215
node n1504 s27 word last -> n1215
node n1505 s28 -> n1504
node n1506 s55 last -> n1211
node n1507 s18 word last -> n1505
node n1508 s29 -> n1507
node n1509 s32 -> n1515
node n1510 s35 -> n1520
node n1511 s39 last -> n1525
node n1512 s30 word last -> n1215
node n1513 s31 -> n1512
node n1514 s55 last -> n1211
node n1515 s23 word last -> n1513
node n1516 s33 word last -> n1215
node n1517 s34 last -> n1516
node n1518 s35 -> n1517
node n1519 s55 last -> n1211
node n1520 s36 word last -> n1518
node n1521 s37 word last -> n1215
node n1522 s38 last -> n1521
node n1523 s39 -> n1522
node n1524 s55 last -> n1211
node n1525 s40 word last -> n1523
node n1526 s55 -> n1211
node n1527 s33 word last -> n1532
node n1528 s18 word last -> n1215
node n1529 s41 last -> n1528
node n1530 s46 last -> n1529
node n1531 s51 last -> n1530
node n1532 s41 -> n1531
node n1533 s55 last -> n1211
node n1534 s29 word last -> n1526
node n1535 s41 -> n1534
node n1536 s43 last -> n1545
node n1537 s55 -> n1211
node n1538 s37 word last -> n1543
node n1539 s23 word last -> n1215
node n1540 s43 last -> n1539
node n1541 s48 last -> n1540
node n1542 s26 last -> n1541
node n1543 s43 -> n1542
node n1544 s55 last -> n1211
node n1545 s32 word last -> n1537
node n1546 s47 -> n1530
node n1547 s55 last -> n1211
node n1548 s33 word last -> n1546
node n1549 s17 -> n1548
node n1550 s22 last -> n1553
node n1551 s49 -> n1541
node n1552 s55 last -> n1211
node n1553 s37 word last -> n1551
node n1554 s53 word last -> n1532
node n1555 s47 -> n1554
node n1556 s49 last -> n1557
node n1557 s54 word last -> n1543
node n1558 s17 word last -> n1532
node n1559 s41 -> n1558
node n1560 s43 last -> n1561
node n1561 s22 word last -> n1543
node n1562 s55 last -> n1474
node n1563 s2 -> n1562
node n1564 s3 last -> n441
node n1565 s3 -> n1563
node n1566 s9 -> n1570
node n1567 s13 -> n1575
node n1568 s1 -> n1578
node n1569 s6 last -> n1581
node n1570 s12 -> n1562
node n1571 s11 -> n1573
node n1572 s55 last -> n1344
node n1573 s55 -> n1474
node n1574 s4 last -> n442
node n1575 s2 -> n1562
node n1576 s55 -> n1344
node n1577 s3 last -> n1573
node n1578 s55 -> n1344
node n1579 s0 -> n1562
node n1580 s10 last -> n441
node n1581 s55 -> n1344
node n1582 s4 last -> n441
node n1583 s11 word -> n1444
node n1584 s12 word -> n1586
node n1585 s15 last -> n447
node n1586 s15 last -> n1565
node n1587 s9 word -> n1583
node n1588 s13 word -> n1593
node n1589 s3 -> n1596
node n1590 s1 word -> n1598
node n1591 s6 word -> n1601
node n1592 s15 last -> n1721
node n1593 s3 word -> n1444
node n1594 s2 word -> n1586
node n1595 s15 last -> n447
node n1596 s3 word -> n1442
node n1597 s2 word last -> n1586
node n1598 s0 word -> n1586
node n1599 s10 word -> n1442
node n1600 s15 last -> n447
node n1601 s4 word -> n1442
node n1602 s15 last -> n447
node n1603 s19 last -> n636
node n1604 s18 last -> n1603
node n1605 s20 last -> n1604
node n1606 s35 -> n1605
node n1607 s55 word last
node n1608 s42 last -> n1606
node n1609 s20 -> n1608
node n1610 s25 -> n1618
node n1611 s35 -> n1621
node n1612 s39 last -> n1627
node n1613 s24 last -> n636
node n1614 s23 last -> n1613
node n1615 s25 last -> n1614
node n1616 s39 -> n1615
node n1617 s55 word last
node n1618 s44 last -> n1616
node n1619 s41 -> n636
node n1620 s55 word last
node n1621 s42 -> n1619
node n1622 s27 last -> n1623
node n1623 s19 -> n636
node n1624 s55 word last
node n1625 s43 -> n636
node n1626 s55 word last
node n1627 s44 -> n1625
node n1628 s30 last -> n1629
node n1629 s24 -> n636
node n1630 s55 word last
node n1631 s45 -> n1609
node n1632 s50 -> n1643
node n1633 s26 -> n1654
node n1634 s25 -> n1665
node n1635 s38 -> n1692
node n1636 s49 -> n1706
node n1637 s44 -> n1712
node n1638 s52 last -> n1716
node n1639 s35 last -> n1605
node n1640 s18 -> n1639
node n1641 s55 word last
node n1642 s46 last -> n1640
node n1643 s41 -> n1642
node n1644 s43 last -> n1648
node n1645 s39 last -> n1615
node n1646 s23 -> n1645
node n1647 s55 word last
node n1648 s48 last -> n1646
node n1649 s16 last -> n636
node n1650 s17 -> n1649
node n1651 s55 word last
node n1652 s18 -> n1650
node n1653 s19 last -> n636
node n1654 s20 -> n1652
node n1655 s25 last -> n1659
node n1656 s21 last -> n636
node n1657 s22 -> n1656
node n1658 s55 word last
node n1659 s23 -> n1657
node n1660 s24 last -> n636
node n1661 s27 last -> n636
node n1662 s28 -> n1661
node n1663 s55 word last
node n1664 s18 last -> n1662
node n1665 s29 -> n1664
node n1666 s32 -> n1672
node n1667 s35 -> n1677
node n1668 s39 last -> n1682
node n1669 s30 last -> n636
node n1670 s31 -> n1669
node n1671 s55 word last
node n1672 s23 last -> n1670
node n1673 s33 last -> n636
node n1674 s34 last -> n1673
node n1675 s35 -> n1674
node n1676 s55 word last
node n1677 s36 last -> n1675
node n1678 s37 last -> n636
node n1679 s38 last -> n1678
node n1680 s39 -> n1679
node n1681 s55 word last
node n1682 s40 last -> n1680
node n1683 s18 last -> n636
node n1684 s41 last -> n1683
node n1685 s46 last -> n1684
node n1686 s51 last -> n1685
node n1687 s41 -> n1686
node n1688 s55 word last
node n1689 s33 -> n1687
node n1690 s55 word last
node n1691 s29 last -> n1689
node n1692 s41 -> n1691
node n1693 s43 last -> n1702
node n1694 s23 last -> n636
node n1695 s43 last -> n1694
node n1696 s48 last -> n1695
node n1697 s26 last -> n1696
node n1698 s43 -> n1697
node n1699 s55 word last
node n1700 s37 -> n1698
node n1701 s55 word last
node n1702 s32 last -> n1700
node n1703 s47 -> n1685
node n1704 s55 word last
node n1705 s33 last -> n1703
node n1706 s17 -> n1705
node n1707 s22 last -> n1710
node n1708 s49 -> n1696
node n1709 s55 word last
node n1710 s37 last -> n1708
node n1711 s53 last -> n1687
node n1712 s47 -> n1711
node n1713 s49 last -> n1714
node n1714 s54 last -> n1698
node n1715 s17 last -> n1687
node n1716 s41 -> n1715
node n1717 s43 last -> n1718
node n1718 s22 last -> n1698
node n1719 s55 last -> n1631
node n1720 s2 last -> n1719
node n1721 s3 -> n1720
node n1722 s5 -> n1725
node n1723 s1 -> n1727
node n1724 s6 last -> n1719
node n1725 s4 -> n1719
node n1726 s55 last -> n1631
node n1727 s55 -> n1631
node n1728 s0 last -> n1719
node n1729 s55 word -> n1587
node n1730 s15 -> n1734
node n1731 s35 last -> n1756
node n1732 s2 -> n1432
node n1733 s3 last -> n465
node n1734 s3 -> n1732
node n1735 s9 -> n1739
node n1736 s13 -> n1744
node n1737 s1 -> n1747
node n1738 s6 last -> n1750
node n1739 s12 -> n1432
node n1740 s11 -> n1742
node n1741 s55 last -> n1344
node n1742 s4 -> n466
node n1743 s55 last -> n1344
node n1744 s2 -> n1432
node n1745 s55 -> n1344
node n1746 s3 last -> n1742
node n1747 s55 -> n1344
node n1748 s0 -> n1432
node n1749 s10 last -> n465
node n1750 s55 -> n1344
node n1751 s4 last -> n465
node n1752 s55 word -> n1587
node n1753 s15 last -> n1734
node n1754 s19 word last -> n1752
node n1755 s18 last -> n1754
node n1756 s20 last -> n1755
node n1757 s42 word last -> n1729
node n1758 s20 -> n1757
node n1759 s25 -> n1768
node n1760 s35 -> n1772
node n1761 s39 last -> n1780
node n1762 s55 word -> n1587
node n1763 s15 -> n1734
node n1764 s39 last -> n1767
node n1765 s24 word last -> n1752
node n1766 s23 last -> n1765
node n1767 s25 last -> n1766
node n1768 s44 word last -> n1762
node n1769 s41 word -> n1752
node n1770 s55 word -> n1587
node n1771 s15 last -> n1734
node n1772 s42 word -> n1769
node n1773 s27 word last -> n1774
node n1774 s19 word -> n1752
node n1775 s55 word -> n1587
node n1776 s15 last -> n1734
node n1777 s43 word -> n1752
node n1778 s55 word -> n1587
node n1779 s15 last -> n1734
node n1780 s44 word -> n1777
node n1781 s30 word last -> n1782
node n1782 s24 word -> n1752
node n1783 s55 word -> n1587
node n1784 s15 last -> n1734
node n1785 s35 last -> n1756
node n1786 s18 -> n1785
node n1787 s55 word -> n1587
node n1788 s15 last -> n1734
node n1789 s46 word last -> n1786
node n1790 s41 -> n1789
node n1791 s43 last -> n1796
node n1792 s39 last -> n1767
node n1793 s23 -> n1792
node n1794 s55 word -> n1587
node n1795 s15 last -> n1734
node n1796 s48 word last -> n1793
node n1797 s16 word last -> n1752
node n1798 s17 -> n1797
node n1799 s55 word -> n1587
node n1800 s15 last -> n1734
node n1801 s18 word -> n1798
node n1802 s19 word last -> n1752
node n1803 s20 -> n1801
node n1804 s25 last -> n1809
node n1805 s21 word last -> n1752
node n1806 s22 -> n1805
node n1807 s55 word -> n1587
node n1808 s15 last -> n1734
node n1809 s23 word -> n1806
node n1810 s24 word last -> n1752
node n1811 s27 word last -> n1752
node n1812 s28 -> n1811
node n1813 s55 word -> n1587
node n1814 s15 last -> n1734
node n1815 s18 word last -> n1812
node n1816 s29 -> n1815
node n1817 s32 -> n1824
node n1818 s35 -> n1830
node n1819 s39 last -> n1836
node n1820 s30 word last -> n1752
node n1821 s31 -> n1820
node n1822 s55 word -> n1587
node n1823 s15 last -> n1734
node n1824 s23 word last -> n1821
node n1825 s33 word last -> n1752
node n1826 s34 last -> n1825
node n1827 s35 -> n1826
node n1828 s55 word -> n1587
node n1829 s15 last -> n1734
node n1830 s36 word last -> n1827
node n1831 s37 word last -> n1752
node n1832 s38 last -> n1831
node n1833 s39 -> n1832
node n1834 s55 word -> n1587
node n1835 s15 last -> n1734
node n1836 s40 word last -> n1833
node n1837 s55 word -> n1587
node n1838 s15 -> n1734
node n1839 s33 word last -> n1840
node n1840 s55 word -> n1587
node n1841 s15 -> n1734
node n1842 s41 last -> n1846
node n1843 s18 word last -> n1752
node n1844 s41 last -> n1843
node n1845 s46 last -> n1844
node n1846 s51 last -> n1845
node n1847 s29 word last -> n1837
node n1848 s41 -> n1847
node n1849 s43 last -> n1860
node n1850 s55 word -> n1587
node n1851 s15 -> n1734
node n1852 s37 word last -> n1853
node n1853 s55 word -> n1587
node n1854 s15 -> n1734
node n1855 s43 last -> n1859
node n1856 s23 word last -> n1752
node n1857 s43 last -> n1856
node n1858 s48 last -> n1857
node n1859 s26 last -> n1858
node n1860 s32 word last -> n1850
node n1861 s55 word -> n1587
node n1862 s15 -> n1734
node n1863 s47 last -> n1845
node n1864 s33 word last -> n1861
node n1865 s17 -> n1864
node n1866 s22 last -> n1870
node n1867 s55 word -> n1587
node n1868 s15 -> n1734
node n1869 s49 last -> n1858
node n1870 s37 word last -> n1867
node n1871 s53 word last -> n1840
node n1872 s47 -> n1871
node n1873 s49 last -> n1874
node n1874 s54 word last -> n1853
node n1875 s17 word last -> n1840
node n1876 s41 -> n1875
node n1877 s43 last -> n1878
node n1878 s22 word last -> n1853
node n1879 s11 word -> n682
node n1880 s8 word -> n2054
node n1881 s59 word -> n2178
node n1882 s57 word -> n681
node n1883 s7 -> n2206
node n1884 s55 -> n2388
node n1885 s48 -> n666
node n1886 s46 -> n670
node n1887 s25 -> n673
node n1888 s20 -> n676
node n1889 s63 word -> n2575
node n1890 s15 last -> n2063
node n1891 s7 -> n691
node n1892 s4 word last -> n694
node n1893 s5 word -> n1891
node n1894 s1 word -> n1897
node n1895 s6 word -> n694
node n1896 s3 last -> n695
node n1897 s7 -> n691
node n1898 s0 word last -> n694
node n1899 s7 last -> n1893
node n1900 s2 last -> n1899
node n1901 s3 -> n1900
node n1902 s9 -> n1906
node n1903 s13 -> n1909
node n1904 s1 -> n1911
node n1905 s6 last -> n1899
node n1906 s7 -> n1893
node n1907 s56 -> n701
node n1908 s8 last -> n1899
node n1909 s7 -> n1893
node n1910 s4 last -> n701
node n1911 s7 -> n1893
node n1912 s0 last -> n701
node n1913 s15 last -> n1901
node n1914 s4 word last -> n1913
node n1915 s56 word -> n1914
node n1916 s3 -> n1914
node n1917 s9 word -> n1921
node n1918 s13 word last -> n1923
node n1919 s15 -> n1901
node n1920 s4 last -> n1914
node n1921 s11 word -> n1919
node n1922 s12 word last -> n1913
node n1923 s3 word -> n1919
node n1924 s2 word last -> n1913
node n1925 s7 -> n1915
node n1926 s15 last -> n1964
node n1927 s7 -> n691
node n1928 s11 word -> n615
node n1929 s8 word -> n694
node n1930 s59 word last
node n1931 s9 word -> n1927
node n1932 s13 word -> n1935
node n1933 s3 -> n1938
node n1934 s56 word last -> n1940
node n1935 s7 -> n691
node n1936 s3 word -> n615
node n1937 s2 word last
node n1938 s2 word -> n694
node n1939 s3 word last
node n1940 s7 -> n691
node n1941 s4 word last
node n1942 s7 last -> n1931
node n1943 s2 -> n1942
node n1944 s3 -> n1946
node n1945 s7 last -> n1948
node n1946 s4 -> n29
node n1947 s7 last -> n1931
node n1948 s6 word -> n1940
node n1949 s9 word -> n1953
node n1950 s13 word -> n1958
node n1951 s1 word -> n1961
node n1952 s3 last -> n1938
node n1953 s7 -> n691
node n1954 s11 word -> n1956
node n1955 s12 word last -> n694
node n1956 s4 -> n21
node n1957 s7 last -> n691
node n1958 s7 -> n691
node n1959 s3 word -> n1956
node n1960 s2 word last -> n694
node n1961 s7 -> n691
node n1962 s0 word -> n694
node n1963 s10 word last
node n1964 s13 -> n1943
node n1965 s1 -> n1969
node n1966 s6 -> n1972
node n1967 s3 -> n1974
node n1968 s9 last -> n1977
node n1969 s0 -> n1942
node n1970 s10 -> n28
node n1971 s7 last -> n1948
node n1972 s4 -> n28
node n1973 s7 last -> n1948
node n1974 s3 -> n28
node n1975 s2 last -> n1976
node n1976 s7 last -> n1948
node n1977 s59 -> n1942
node n1978 s11 -> n1946
node n1979 s7 -> n1948
node n1980 s8 last -> n1976
node n1981 s2 word -> n1925
node n1982 s3 word last -> n1989
node n1983 s2 last -> n694
node n1984 s3 -> n1983
node n1985 s9 -> n1987
node n1986 s10 last -> n694
node n1987 s7 -> n691
node n1988 s8 last -> n694
node n1989 s15 last -> n1984
node n1990 s3 -> n1981
node n1991 s9 word -> n2003
node n1992 s13 word -> n2043
node n1993 s1 word -> n2047
node n1994 s6 word -> n2051
node n1995 s45 -> n926
node n1996 s50 -> n958
node n1997 s26 -> n971
node n1998 s25 -> n984
node n1999 s38 -> n1016
node n2000 s49 -> n1033
node n2001 s44 -> n1040
node n2002 s52 last -> n1044
node n2003 s7 -> n1915
node n2004 s8 word -> n1925
node n2005 s59 word -> n2024
node n2006 s11 word -> n2039
node n2007 s15 last -> n1964
node n2008 s7 -> n1893
node n2009 s8 last -> n1899
node n2010 s9 -> n2008
node n2011 s10 -> n1899
node n2012 s3 last -> n1900
node n2013 s15 last -> n2010
node n2014 s4 word last -> n2013
node n2015 s4 -> n2014
node n2016 s15 last -> n2010
node n2017 s11 word -> n2015
node n2018 s12 word last -> n2013
node n2019 s9 -> n2017
node n2020 s13 -> n2022
node n2021 s14 last -> n2014
node n2022 s3 word -> n2015
node n2023 s2 word last -> n2013
node n2024 s7 -> n2019
node n2025 s15 last -> n2029
node n2026 s8 -> n1976
node n2027 s56 -> n694
node n2028 s7 last -> n1948
node n2029 s9 -> n2026
node n2030 s13 -> n2034
node n2031 s1 -> n2036
node n2032 s6 -> n1976
node n2033 s3 last -> n2038
node n2034 s4 -> n694
node n2035 s7 last -> n1948
node n2036 s0 -> n694
node n2037 s7 last -> n1948
node n2038 s2 last -> n1976
node n2039 s7 -> n2019
node n2040 s4 -> n2042
node n2041 s15 last -> n2029
node n2042 s4 word last -> n1989
node n2043 s7 -> n1915
node n2044 s2 word -> n2024
node n2045 s3 word -> n2039
node n2046 s15 last -> n1964
node n2047 s7 -> n1915
node n2048 s0 word -> n2024
node n2049 s10 word -> n1989
node n2050 s15 last -> n1964
node n2051 s7 -> n1915
node n2052 s4 word -> n1989
node n2053 s15 last -> n1964
node n2054 s7 -> n1990
node n2055 s57 word -> n681
node n2056 s15 -> n2063
node n2057 s55 -> n2137
node n2058 s48 -> n666
node n2059 s46 -> n670
node n2060 s25 -> n673
node n2061 s20 -> n676
node n2062 s4 last -> n626
node n2063 s45 -> n1086
node n2064 s26 -> n1113
node n2065 s49 -> n1126
node n2066 s1 word -> n2076
node n2067 s6 word -> n1156
node n2068 s9 word -> n2079
node n2069 s13 word -> n2086
node n2070 s25 -> n1138
node n2071 s3 -> n1049
node n2072 s50 -> n1162
node n2073 s52 -> n1172
node n2074 s44 -> n1179
node n2075 s38 last -> n1185
node n2076 s0 word -> n1047
node n2077 s15 -> n691
node n2078 s60 word last
node n2079 s12 word -> n1047
node n2080 s11 word -> n2083
node n2081 s15 -> n691
node n2082 s57 word last
node n2083 s4 -> n21
node n2084 s15 -> n691
node n2085 s57 word last
node n2086 s2 word -> n1047
node n2087 s3 word -> n2083
node n2088 s15 -> n691
node n2089 s57 word last
node n2090 s55 word -> n1247
node n2091 s15 last -> n2110
node n2092 s3 -> n299
node n2093 s2 last -> n2109
node n2094 s2 word -> n1190
node n2095 s3 word last
node n2096 s3 -> n2094
node n2097 s56 word -> n2100
node n2098 s13 word -> n2102
node n2099 s9 word last -> n2105
node n2100 s55 -> n691
node n2101 s4 word last
node n2102 s3 word -> n615
node n2103 s55 -> n691
node n2104 s2 word last
node n2105 s11 word -> n615
node n2106 s55 -> n691
node n2107 s8 word -> n1190
node n2108 s59 word last
node n2109 s55 last -> n2096
node n2110 s3 -> n2092
node n2111 s13 -> n2115
node n2112 s1 -> n2120
node n2113 s6 -> n2123
node n2114 s9 last -> n2125
node n2115 s2 -> n2109
node n2116 s3 -> n2118
node n2117 s55 last -> n2096
node n2118 s4 -> n300
node n2119 s55 last -> n2096
node n2120 s0 -> n2109
node n2121 s10 -> n299
node n2122 s55 last -> n2096
node n2123 s4 -> n299
node n2124 s55 last -> n2096
node n2125 s12 -> n2109
node n2126 s11 -> n2118
node n2127 s55 last -> n2096
node n2128 s2 word -> n2090
node n2129 s3 word last -> n2136
node n2130 s55 -> n691
node n2131 s8 last -> n1190
node n2132 s9 -> n2130
node n2133 s10 -> n1190
node n2134 s3 last -> n2135
node n2135 s2 last -> n1190
node n2136 s15 last -> n2132
node n2137 s3 -> n2128
node n2138 s9 word -> n2150
node n2139 s13 word -> n2167
node n2140 s1 word -> n2171
node n2141 s6 word -> n2175
node n2142 s45 -> n1758
node n2143 s50 -> n1790
node n2144 s26 -> n1803
node n2145 s25 -> n1816
node n2146 s38 -> n1848
node n2147 s49 -> n1865
node n2148 s44 -> n1872
node n2149 s52 last -> n1876
node n2150 s55 word -> n1247
node n2151 s8 word -> n2090
node n2152 s11 word -> n2156
node n2153 s59 word -> n2166
node n2154 s15 last -> n2110
node n2155 s4 word last -> n2136
node n2156 s4 -> n2155
node n2157 s15 last -> n2158
node n2158 s6 -> n1190
node n2159 s5 -> n2162
node n2160 s1 -> n2164
node n2161 s3 last -> n2135
node n2162 s55 -> n691
node n2163 s4 last -> n1190
node n2164 s55 -> n691
node n2165 s0 last -> n1190
node n2166 s15 last -> n2158
node n2167 s55 word -> n1247
node n2168 s3 word -> n2156
node n2169 s2 word -> n2166
node n2170 s15 last -> n2110
node n2171 s55 word -> n1247
node n2172 s0 word -> n2166
node n2173 s10 word -> n2136
node n2174 s15 last -> n2110
node n2175 s55 word -> n1247
node n2176 s4 word -> n2136
node n2177 s15 last -> n2110
node n2178 s7 -> n746
node n2179 s57 word -> n681
node n2180 s15 -> n1051
node n2181 s55 -> n1302
node n2182 s48 -> n666
node n2183 s46 -> n670
node n2184 s25 -> n673
node n2185 s20 -> n676
node n2186 s4 last -> n626
node n2187 s9 -> n1921
node n2188 s13 -> n1923
node n2189 s14 last -> n1914
node n2190 s7 -> n2187
node n2191 s15 last -> n2192
node n2192 s3 -> n2038
node n2193 s9 -> n2197
node n2194 s13 -> n2200
node n2195 s1 -> n2202
node n2196 s6 last -> n1976
node n2197 s56 -> n1942
node n2198 s7 -> n1948
node n2199 s8 last -> n1976
node n2200 s4 -> n1942
node n2201 s7 last -> n1948
node n2202 s0 -> n1942
node n2203 s7 last -> n1948
node n2204 s2 word -> n2190
node n2205 s3 word last -> n1989
node n2206 s3 -> n2204
node n2207 s9 word -> n2219
node n2208 s13 word -> n2224
node n2209 s1 word -> n2228
node n2210 s6 word -> n2232
node n2211 s45 -> n2244
node n2212 s50 -> n2276
node n2213 s26 -> n2289
node n2214 s25 -> n2302
node n2215 s38 -> n2334
node n2216 s49 -> n2351
node n2217 s44 -> n2358
node n2218 s52 last -> n2362
node n2219 s8 word -> n2190
node n2220 s59 word -> n2024
node n2221 s11 word -> n2039
node n2222 s7 -> n2187
node n2223 s15 last -> n2192
node n2224 s2 word -> n2024
node n2225 s7 -> n2187
node n2226 s3 word -> n2039
node n2227 s15 last -> n2192
node n2228 s7 -> n2187
node n2229 s0 word -> n2024
node n2230 s10 word -> n1989
node n2231 s15 last -> n2192
node n2232 s7 -> n2187
node n2233 s4 word -> n1989
node n2234 s15 last -> n2192
node n2235 s7 -> n892
node n2236 s15 -> n878
node n2237 s35 last -> n2242
node n2238 s7 -> n892
node n2239 s15 last -> n878
node n2240 s19 word last -> n2238
node n2241 s18 last -> n2240
node n2242 s20 last -> n2241
node n2243 s42 word last -> n2235
node n2244 s20 -> n2243
node n2245 s25 -> n2254
node n2246 s35 -> n2258
node n2247 s39 last -> n2266
node n2248 s7 -> n892
node n2249 s15 -> n878
node n2250 s39 last -> n2253
node n2251 s24 word last -> n2238
node n2252 s23 last -> n2251
node n2253 s25 last -> n2252
node n2254 s44 word last -> n2248
node n2255 s41 word -> n2238
node n2256 s7 -> n892
node n2257 s15 last -> n878
node n2258 s42 word -> n2255
node n2259 s27 word last -> n2260
node n2260 s19 word -> n2238
node n2261 s7 -> n892
node n2262 s15 last -> n878
node n2263 s43 word -> n2238
node n2264 s7 -> n892
node n2265 s15 last -> n878
node n2266 s44 word -> n2263
node n2267 s30 word last -> n2268
node n2268 s24 word -> n2238
node n2269 s7 -> n892
node n2270 s15 last -> n878
node n2271 s35 last -> n2242
node n2272 s18 -> n2271
node n2273 s7 -> n892
node n2274 s15 last -> n878
node n2275 s46 word last -> n2272
node n2276 s41 -> n2275
node n2277 s43 last -> n2282
node n2278 s39 last -> n2253
node n2279 s23 -> n2278
node n2280 s7 -> n892
node n2281 s15 last -> n878
node n2282 s48 word last -> n2279
node n2283 s16 word last -> n2238
node n2284 s17 -> n2283
node n2285 s7 -> n892
node n2286 s15 last -> n878
node n2287 s18 word -> n2284
node n2288 s19 word last -> n2238
node n2289 s20 -> n2287
node n2290 s25 last -> n2295
node n2291 s21 word last -> n2238
node n2292 s22 -> n2291
node n2293 s7 -> n892
node n2294 s15 last -> n878
node n2295 s23 word -> n2292
node n2296 s24 word last -> n2238
node n2297 s27 word last -> n2238
node n2298 s28 -> n2297
node n2299 s7 -> n892
node n2300 s15 last -> n878
node n2301 s18 word last -> n2298
node n2302 s29 -> n2301
node n2303 s32 -> n2310
node n2304 s35 -> n2316
node n2305 s39 last -> n2322
node n2306 s30 word last -> n2238
node n2307 s31 -> n2306
node n2308 s7 -> n892
node n2309 s15 last -> n878
node n2310 s23 word last -> n2307
node n2311 s33 word last -> n2238
node n2312 s34 last -> n2311
node n2313 s35 -> n2312
node n2314 s7 -> n892
node n2315 s15 last -> n878
node n2316 s36 word last -> n2313
node n2317 s37 word last -> n2238
node n2318 s38 last -> n2317
node n2319 s39 -> n2318
node n2320 s7 -> n892
node n2321 s15 last -> n878
node n2322 s40 word last -> n2319
node n2323 s7 -> n892
node n2324 s15 -> n878
node n2325 s33 word last -> n2326
node n2326 s7 -> n892
node n2327 s15 -> n878
node n2328 s41 last -> n2332
node n2329 s18 word last -> n2238
node n2330 s41 last -> n2329
node n2331 s46 last -> n2330
node n2332 s51 last -> n2331
node n2333 s29 word last -> n2323
node n2334 s41 -> n2333
node n2335 s43 last -> n2346
node n2336 s7 -> n892
node n2337 s15 -> n878
node n2338 s37 word last -> n2339
node n2339 s7 -> n892
node n2340 s15 -> n878
node n2341 s43 last -> n2345
node n2342 s23 word last -> n2238
node n2343 s43 last -> n2342
node n2344 s48 last -> n2343
node n2345 s26 last -> n2344
node n2346 s32 word last -> n2336
node n2347 s7 -> n892
node n2348 s15 -> n878
node n2349 s47 last -> n2331
node n2350 s33 word last -> n2347
node n2351 s17 -> n2350
node n2352 s22 last -> n2356
node n2353 s7 -> n892
node n2354 s15 -> n878
node n2355 s49 last -> n2344
node n2356 s37 word last -> n2353
node n2357 s53 word last -> n2326
node n2358 s47 -> n2357
node n2359 s49 last -> n2360
node n2360 s54 word last -> n2339
node n2361 s17 word last -> n2326
node n2362 s41 -> n2361
node n2363 s43 last -> n2364
node n2364 s22 word last -> n2339
node n2365 s4 -> n1208
node n2366 s15 last -> n1199
node n2367 s11 word -> n2365
node n2368 s12 word last -> n1207
node n2369 s9 -> n2367
node n2370 s13 -> n2373
node n2371 s14 -> n1208
node n2372 s15 last -> n1271
node n2373 s3 word -> n2365
node n2374 s2 word last -> n1207
node n2375 s55 word -> n2369
node n2376 s15 last -> n2378
node n2377 s2 last -> n2109
node n2378 s3 -> n2377
node n2379 s5 -> n2382
node n2380 s1 -> n2384
node n2381 s6 last -> n2109
node n2382 s4 -> n2109
node n2383 s55 last -> n2096
node n2384 s0 -> n2109
node n2385 s55 last -> n2096
node n2386 s2 word -> n2375
node n2387 s3 word last -> n2136
node n2388 s3 -> n2386
node n2389 s9 word -> n2401
node n2390 s13 word -> n2406
node n2391 s1 word -> n2410
node n2392 s6 word -> n2414
node n2393 s45 -> n2436
node n2394 s50 -> n2468
node n2395 s26 -> n2481
node n2396 s25 -> n2494
node n2397 s38 -> n2526
node n2398 s49 -> n2543
node n2399 s44 -> n2550
node n2400 s52 last -> n2554
node n2401 s55 word -> n2369
node n2402 s8 word -> n2375
node n2403 s11 word -> n2156
node n2404 s59 word -> n2166
node n2405 s15 last -> n2378
node n2406 s55 word -> n2369
node n2407 s3 word -> n2156
node n2408 s2 word -> n2166
node n2409 s15 last -> n2378
node n2410 s55 word -> n2369
node n2411 s0 word -> n2166
node n2412 s10 word -> n2136
node n2413 s15 last -> n2378
node n2414 s55 word -> n2369
node n2415 s4 word -> n2136
node n2416 s15 last -> n2378
node n2417 s4 -> n1443
node n2418 s15 last -> n1434
node n2419 s11 word -> n2417
node n2420 s12 word last -> n1442
node n2421 s9 -> n2419
node n2422 s13 -> n2425
node n2423 s14 -> n1443
node n2424 s15 last -> n1721
node n2425 s3 word -> n2417
node n2426 s2 word last -> n1442
node n2427 s55 word -> n2421
node n2428 s15 -> n1434
node n2429 s35 last -> n2434
node n2430 s55 word -> n2421
node n2431 s15 last -> n1434
node n2432 s19 word last -> n2430
node n2433 s18 last -> n2432
node n2434 s20 last -> n2433
node n2435 s42 word last -> n2427
node n2436 s20 -> n2435
node n2437 s25 -> n2446
node n2438 s35 -> n2450
node n2439 s39 last -> n2458
node n2440 s55 word -> n2421
node n2441 s15 -> n1434
node n2442 s39 last -> n2445
node n2443 s24 word last -> n2430
node n2444 s23 last -> n2443
node n2445 s25 last -> n2444
node n2446 s44 word last -> n2440
node n2447 s41 word -> n2430
node n2448 s55 word -> n2421
node n2449 s15 last -> n1434
node n2450 s42 word -> n2447
node n2451 s27 word last -> n2452
node n2452 s19 word -> n2430
node n2453 s55 word -> n2421
node n2454 s15 last -> n1434
node n2455 s43 word -> n2430
node n2456 s55 word -> n2421
node n2457 s15 last -> n1434
node n2458 s44 word -> n2455
node n2459 s30 word last -> n2460
node n2460 s24 word -> n2430
node n2461 s55 word -> n2421
node n2462 s15 last -> n1434
node n2463 s35 last -> n2434
node n2464 s18 -> n2463
node n2465 s55 word -> n2421
node n2466 s15 last -> n1434
node n2467 s46 word last -> n2464
node n2468 s41 -> n2467
node n2469 s43 last -> n2474
node n2470 s39 last -> n2445
node n2471 s23 -> n2470
node n2472 s55 word -> n2421
node n2473 s15 last -> n1434
node n2474 s48 word last -> n2471
node n2475 s16 word last -> n2430
node n2476 s17 -> n2475
node n2477 s55 word -> n2421
node n2478 s15 last -> n1434
node n2479 s18 word -> n2476
node n2480 s19 word last -> n2430
node n2481 s20 -> n2479
node n2482 s25 last -> n2487
node n2483 s21 word last -> n2430
node n2484 s22 -> n2483
node n2485 s55 word -> n2421
node n2486 s15 last -> n1434
node n2487 s23 word -> n2484
node n2488 s24 word last -> n2430
node n2489 s27 word last -> n2430
node n2490 s28 -> n2489
node n2491 s55 word -> n2421
node n2492 s15 last -> n1434
node n2493 s18 word last -> n2490
node n2494 s29 -> n2493
node n2495 s32 -> n2502
node n2496 s35 -> n2508
node n2497 s39 last -> n2514
node n2498 s30 word last -> n2430
node n2499 s31 -> n2498
node n2500 s55 word -> n2421
node n2501 s15 last -> n1434
node n2502 s23 word last -> n2499
node n2503 s33 word last -> n2430
node n2504 s34 last -> n2503
node n2505 s35 -> n2504
node n2506 s55 word -> n2421
node n2507 s15 last -> n1434
node n2508 s36 word last -> n2505
node n2509 s37 word last -> n2430
node n2510 s38 last -> n2509
node n2511 s39 -> n2510
node n2512 s55 word -> n2421
node n2513 s15 last -> n1434
node n2514 s40 word last -> n2511
node n2515 s55 word -> n2421
node n2516 s15 -> n1434
node n2517 s33 word last -> n2518
node n2518 s55 word -> n2421
node n2519 s15 -> n1434
node n2520 s41 last -> n2524
node n2521 s18 word last -> n2430
node n2522 s41 last -> n2521
node n2523 s46 last -> n2522
node n2524 s51 last -> n2523
node n2525 s29 word last -> n2515
node n2526 s41 -> n2525
node n2527 s43 last -> n2538
node n2528 s55 word -> n2421
node n2529 s15 -> n1434
node n2530 s37 word last -> n2531
node n2531 s55 word -> n2421
node n2532 s15 -> n1434
node n2533 s43 last -> n2537
node n2534 s23 word last -> n2430
node n2535 s43 last -> n2534
node n2536 s48 last -> n2535
node n2537 s26 last -> n2536
node n2538 s32 word last -> n2528
node n2539 s55 word -> n2421
node n2540 s15 -> n1434
node n2541 s47 last -> n2523
node n2542 s33 word last -> n2539
node n2543 s17 -> n2542
node n2544 s22 last -> n2548
node n2545 s55 word -> n2421
node n2546 s15 -> n1434
node n2547 s49 last -> n2536
node n2548 s37 word last -> n2545
node n2549 s53 word last -> n2518
node n2550 s47 -> n2549
node n2551 s49 last -> n2552
node n2552 s54 word last -> n2531
node n2553 s17 word last -> n2518
node n2554 s41 -> n2553
node n2555 s43 last -> n2556
node n2556 s22 word last -> n2531
node n2557 s62 word last
node n2558 s63 last -> n2557
node n2559 s4 word last -> n2558
node n2560 s4 last -> n2559
node n2561 s24 last -> n2560
node n2562 s50 -> n2561
node n2563 s63 -> n2567
node n2564 s62 last -> n2568
node n2565 s24 last -> n615
node n2566 s50 last -> n2565
node n2567 s62 word last -> n2566
node n2568 s63 word last -> n691
node n2569 s15 -> n2562
node n2570 s4 last -> n2572
node n2571 s15 last -> n2562
node n2572 s4 word last -> n2571
node n2573 s11 word -> n2569
node n2574 s12 word last -> n2571
node n2575 s9 -> n2573
node n2576 s13 -> n2579
node n2577 s15 -> n2562
node n2578 s14 last -> n2572
node n2579 s3 word -> n2569
node n2580 s2 word last -> n2571
node n2581 s0 word last
node n2582 s3 word -> n682
node n2583 s2 word -> n2178
node n2584 s57 word -> n681
node n2585 s7 -> n2206
node n2586 s55 -> n2388
node n2587 s48 -> n666
node n2588 s46 -> n670
node n2589 s25 -> n673
node n2590 s20 -> n676
node n2591 s63 word -> n2575
node n2592 s15 last -> n2063
node n2593 s3 word -> n266
node n2594 s2 word last -> n2595
node n2595 s7 -> n1990
node n2596 s15 -> n2063
node n2597 s55 -> n2137
node n2598 s57 word last
node n2599 s0 word -> n2178
node n2600 s10 word -> n2610
node n2601 s57 word -> n681
node n2602 s7 -> n2206
node n2603 s55 -> n2388
node n2604 s48 -> n666
node n2605 s46 -> n670
node n2606 s25 -> n673
node n2607 s20 -> n676
node n2608 s63 word -> n2575
node n2609 s15 last -> n2063
node n2610 s7 -> n40
node n2611 s55 -> n311
node n2612 s15 -> n603
node n2613 s57 -> n681
node n2614 s48 -> n666
node n2615 s46 -> n670
node n2616 s25 -> n673
node n2617 s20 -> n676
node n2618 s4 last -> n626
node n2619 s4 word -> n2610
node n2620 s57 word -> n681
node n2621 s7 -> n2206
node n2622 s55 -> n2388
node n2623 s48 -> n666
node n2624 s46 -> n670
node n2625 s25 -> n673
node n2626 s20 -> n676
node n2627 s63 word -> n2575
node n2628 s15 last -> n2063
node n2629 s4 word -> n2610
node n2630 s57 word -> n681
node n2631 s7 -> n2206
node n2632 s55 -> n2388
node n2633 s48 -> n666
node n2634 s46 -> n670
node n2635 s25 -> n673
node n2636 s20 -> n676
node n2637 s15 last -> n2063
node n2638 s19 last -> n1190
node n2639 s18 last -> n2638
node n2640 s20 last -> n2639
node n2641 s35 -> n2640
node n2642 s55 last -> n691
node n2643 s42 last -> n2641
node n2644 s20 -> n2643
node n2645 s25 -> n2653
node n2646 s35 -> n2656
node n2647 s39 last -> n2662
node n2648 s24 last -> n1190
node n2649 s23 last -> n2648
node n2650 s25 last -> n2649
node n2651 s39 -> n2650
node n2652 s55 last -> n691
node n2653 s44 last -> n2651
node n2654 s55 -> n691
node n2655 s41 last -> n1190
node n2656 s42 -> n2654
node n2657 s27 last -> n2658
node n2658 s55 -> n691
node n2659 s19 last -> n1190
node n2660 s55 -> n691
node n2661 s43 last -> n1190
node n2662 s44 -> n2660
node n2663 s30 last -> n2664
node n2664 s55 -> n691
node n2665 s24 last -> n1190
node n2666 s45 -> n2644
node n2667 s50 -> n2678
node n2668 s26 -> n2689
node n2669 s25 -> n2700
node n2670 s38 -> n2727
node n2671 s49 -> n2741
node n2672 s44 -> n2747
node n2673 s52 last -> n2751
node n2674 s35 last -> n2640
node n2675 s18 -> n2674
node n2676 s55 last -> n691
node n2677 s46 last -> n2675
node n2678 s41 -> n2677
node n2679 s43 last -> n2683
node n2680 s39 last -> n2650
node n2681 s23 -> n2680
node n2682 s55 last -> n691
node n2683 s48 last -> n2681
node n2684 s16 last -> n1190
node n2685 s17 -> n2684
node n2686 s55 last -> n691
node n2687 s18 -> n2685
node n2688 s19 last -> n1190
node n2689 s20 -> n2687
node n2690 s25 last -> n2694
node n2691 s21 last -> n1190
node n2692 s22 -> n2691
node n2693 s55 last -> n691
node n2694 s23 -> n2692
node n2695 s24 last -> n1190
node n2696 s27 last -> n1190
node n2697 s28 -> n2696
node n2698 s55 last -> n691
node n2699 s18 last -> n2697
node n2700 s29 -> n2699
node n2701 s32 -> n2707
node n2702 s35 -> n2712
node n2703 s39 last -> n2717
node n2704 s30 last -> n1190
node n2705 s31 -> n2704
node n2706 s55 last -> n691
node n2707 s23 last -> n2705
node n2708 s33 last -> n1190
node n2709 s34 last -> n2708
node n2710 s35 -> n2709
node n2711 s55 last -> n691
node n2712 s36 last -> n2710
node n2713 s37 last -> n1190
node n2714 s38 last -> n2713
node n2715 s39 -> n2714
node n2716 s55 last -> n691
node n2717 s40 last -> n2715
node n2718 s55 -> n691
node n2719 s33 last -> n2724
node n2720 s18 last -> n1190
node n2721 s41 last -> n2720
node n2722 s46 last -> n2721
node n2723 s51 last -> n2722
node n2724 s41 -> n2723
node n2725 s55 last -> n691
node n2726 s29 last -> n2718
node n2727 s41 -> n2726
node n2728 s43 last -> n2737
node n2729 s55 -> n691
node n2730 s37 last -> n2735
node n2731 s23 last -> n1190
node n2732 s43 last -> n2731
node n2733 s48 last -> n2732
node n2734 s26 last -> n2733
node n2735 s43 -> n2734
node n2736 s55 last -> n691
node n2737 s32 last -> n2729
node n2738 s47 -> n2722
node n2739 s55 last -> n691
node n2740 s33 last -> n2738
node n2741 s17 -> n2740
node n2742 s22 last -> n2745
node n2743 s49 -> n2733
node n2744 s55 last -> n691
node n2745 s37 last -> n2743
node n2746 s53 last -> n2724
node n2747 s47 -> n2746
node n2748 s49 last -> n2749
node n2749 s54 last -> n2735
node n2750 s17 last -> n2724
node n2751 s41 -> n2750
node n2752 s43 last -> n2753
node n2753 s22 last -> n2735
node n2754 s15 last -> n2666
node n2755 s4 word last -> n2754
node n2756 s4 -> n2755
node n2757 s15 last -> n2758
node n2758 s6 -> n1190
node n2759 s45 -> n2644
node n2760 s50 -> n2678
node n2761 s26 -> n2689
node n2762 s25 -> n2700
node n2763 s38 -> n2727
node n2764 s49 -> n2741
node n2765 s44 -> n2747
node n2766 s52 -> n2751
node n2767 s5 -> n2162
node n2768 s1 -> n2164
node n2769 s3 last -> n2135
node n2770 s11 word -> n2756
node n2771 s12 word -> n2773
node n2772 s15 last -> n2158
node n2773 s15 last -> n2758
node n2774 s9 -> n2770
node n2775 s13 -> n2779
node n2776 s3 -> n2782
node n2777 s1 -> n2784
node n2778 s6 last -> n2787
node n2779 s3 word -> n2756
node n2780 s2 word -> n2773
node n2781 s15 last -> n2158
node n2782 s3 word -> n2754
node n2783 s2 word last -> n2773
node n2784 s0 word -> n2773
node n2785 s10 word -> n2754
node n2786 s15 last -> n2158
node n2787 s4 word -> n2754
node n2788 s15 last -> n2158
node n2789 s55 word -> n2774
node n2790 s7 -> n3165
node n2791 s15 -> n3203
node n2792 s35 last -> n3234
node n2793 s19 last -> n1899
node n2794 s18 last -> n2793
node n2795 s20 last -> n2794
node n2796 s35 -> n2795
node n2797 s7 last -> n1893
node n2798 s42 last -> n2796
node n2799 s20 -> n2798
node n2800 s25 -> n2808
node n2801 s35 -> n2811
node n2802 s39 last -> n2817
node n2803 s24 last -> n1899
node n2804 s23 last -> n2803
node n2805 s25 last -> n2804
node n2806 s39 -> n2805
node n2807 s7 last -> n1893
node n2808 s44 last -> n2806
node n2809 s41 -> n1899
node n2810 s7 last -> n1893
node n2811 s42 -> n2809
node n2812 s27 last -> n2813
node n2813 s19 -> n1899
node n2814 s7 last -> n1893
node n2815 s43 -> n1899
node n2816 s7 last -> n1893
node n2817 s44 -> n2815
node n2818 s30 last -> n2819
node n2819 s24 -> n1899
node n2820 s7 last -> n1893
node n2821 s45 -> n2799
node n2822 s50 -> n2833
node n2823 s26 -> n2844
node n2824 s25 -> n2855
node n2825 s38 -> n2882
node n2826 s49 -> n2896
node n2827 s44 -> n2902
node n2828 s52 last -> n2906
node n2829 s35 last -> n2795
node n2830 s18 -> n2829
node n2831 s7 last -> n1893
node n2832 s46 last -> n2830
node n2833 s41 -> n2832
node n2834 s43 last -> n2838
node n2835 s39 last -> n2805
node n2836 s23 -> n2835
node n2837 s7 last -> n1893
node n2838 s48 last -> n2836
node n2839 s16 last -> n1899
node n2840 s17 -> n2839
node n2841 s7 last -> n1893
node n2842 s18 -> n2840
node n2843 s19 last -> n1899
node n2844 s20 -> n2842
node n2845 s25 last -> n2849
node n2846 s21 last -> n1899
node n2847 s22 -> n2846
node n2848 s7 last -> n1893
node n2849 s23 -> n2847
node n2850 s24 last -> n1899
node n2851 s27 last -> n1899
node n2852 s28 -> n2851
node n2853 s7 last -> n1893
node n2854 s18 last -> n2852
node n2855 s29 -> n2854
node n2856 s32 -> n2862
node n2857 s35 -> n2867
node n2858 s39 last -> n2872
node n2859 s30 last -> n1899
node n2860 s31 -> n2859
node n2861 s7 last -> n1893
node n2862 s23 last -> n2860
node n2863 s33 last -> n1899
node n2864 s34 last -> n2863
node n2865 s35 -> n2864
node n2866 s7 last -> n1893
node n2867 s36 last -> n2865
node n2868 s37 last -> n1899
node n2869 s38 last -> n2868
node n2870 s39 -> n2869
node n2871 s7 last -> n1893
node n2872 s40 last -> n2870
node n2873 s18 last -> n1899
node n2874 s41 last -> n2873
node n2875 s46 last -> n2874
node n2876 s51 last -> n2875
node n2877 s41 -> n2876
node n2878 s7 last -> n1893
node n2879 s33 -> n2877
node n2880 s7 last -> n1893
node n2881 s29 last -> n2879
node n2882 s41 -> n2881
node n2883 s43 last -> n2892
node n2884 s23 last -> n1899
node n2885 s43 last -> n2884
node n2886 s48 last -> n2885
node n2887 s26 last -> n2886
node n2888 s43 -> n2887
node n2889 s7 last -> n1893
node n2890 s37 -> n2888
node n2891 s7 last -> n1893
node n2892 s32 last -> n2890
node n2893 s47 -> n2875
node n2894 s7 last -> n1893
node n2895 s33 last -> n2893
node n2896 s17 -> n2895
node n2897 s22 last -> n2900
node n2898 s49 -> n2886
node n2899 s7 last -> n1893
node n2900 s37 last -> n2898
node n2901 s53 last -> n2877
node n2902 s47 -> n2901
node n2903 s49 last -> n2904
node n2904 s54 last -> n2888
node n2905 s17 last -> n2877
node n2906 s41 -> n2905
node n2907 s43 last -> n2908
node n2908 s22 last -> n2888
node n2909 s15 last -> n2821
node n2910 s4 word last -> n2909
node n2911 s4 -> n2910
node n2912 s15 last -> n2821
node n2913 s11 word -> n2911
node n2914 s12 word last -> n2909
node n2915 s9 -> n2913
node n2916 s13 -> n2918
node n2917 s14 last -> n2910
node n2918 s3 word -> n2911
node n2919 s2 word last -> n2909
node n2920 s7 -> n2915
node n2921 s15 last -> n2924
node n2922 s7 -> n691
node n2923 s4 last -> n694
node n2924 s5 -> n2922
node n2925 s1 -> n2936
node n2926 s45 -> n2944
node n2927 s50 -> n2970
node n2928 s26 -> n2981
node n2929 s25 -> n2992
node n2930 s38 -> n3019
node n2931 s49 -> n3033
node n2932 s44 -> n3039
node n2933 s52 -> n3043
node n2934 s6 -> n694
node n2935 s3 last -> n1983
node n2936 s7 -> n691
node n2937 s0 last -> n694
node n2938 s7 -> n1948
node n2939 s35 last -> n2942
node n2940 s19 last -> n1976
node n2941 s18 last -> n2940
node n2942 s20 last -> n2941
node n2943 s42 last -> n2938
node n2944 s20 -> n2943
node n2945 s25 -> n2953
node n2946 s35 -> n2956
node n2947 s39 last -> n2962
node n2948 s7 -> n1948
node n2949 s39 last -> n2952
node n2950 s24 last -> n1976
node n2951 s23 last -> n2950
node n2952 s25 last -> n2951
node n2953 s44 last -> n2948
node n2954 s7 -> n1948
node n2955 s41 last -> n1976
node n2956 s42 -> n2954
node n2957 s27 last -> n2958
node n2958 s7 -> n1948
node n2959 s19 last -> n1976
node n2960 s7 -> n1948
node n2961 s43 last -> n1976
node n2962 s44 -> n2960
node n2963 s30 last -> n2964
node n2964 s7 -> n1948
node n2965 s24 last -> n1976
node n2966 s35 last -> n2942
node n2967 s18 -> n2966
node n2968 s7 last -> n1948
node n2969 s46 last -> n2967
node n2970 s41 -> n2969
node n2971 s43 last -> n2975
node n2972 s39 last -> n2952
node n2973 s23 -> n2972
node n2974 s7 last -> n1948
node n2975 s48 last -> n2973
node n2976 s16 last -> n1976
node n2977 s17 -> n2976
node n2978 s7 last -> n1948
node n2979 s18 -> n2977
node n2980 s19 last -> n1976
node n2981 s20 -> n2979
node n2982 s25 last -> n2986
node n2983 s21 last -> n1976
node n2984 s22 -> n2983
node n2985 s7 last -> n1948
node n2986 s23 -> n2984
node n2987 s24 last -> n1976
node n2988 s27 last -> n1976
node n2989 s28 -> n2988
node n2990 s7 last -> n1948
node n2991 s18 last -> n2989
node n2992 s29 -> n2991
node n2993 s32 -> n2999
node n2994 s35 -> n3004
node n2995 s39 last -> n3009
node n2996 s30 last -> n1976
node n2997 s31 -> n2996
node n2998 s7 last -> n1948
node n2999 s23 last -> n2997
node n3000 s33 last -> n1976
node n3001 s34 last -> n3000
node n3002 s35 -> n3001
node n3003 s7 last -> n1948
node n3004 s36 last -> n3002
node n3005 s37 last -> n1976
node n3006 s38 last -> n3005
node n3007 s39 -> n3006
node n3008 s7 last -> n1948
node n3009 s40 last -> n3007
node n3010 s7 -> n1948
node n3011 s33 last -> n3012
node n3012 s7 -> n1948
node n3013 s41 last -> n3017
node n3014 s18 last -> n1976
node n3015 s41 last -> n3014
node n3016 s46 last -> n3015
node n3017 s51 last -> n3016
node n3018 s29 last -> n3010
node n3019 s41 -> n3018
node n3020 s43 last -> n3029
node n3021 s7 -> n1948
node n3022 s37 last -> n3023
node n3023 s7 -> n1948
node n3024 s43 last -> n3028
node n3025 s23 last -> n1976
node n3026 s43 last -> n3025
node n3027 s48 last -> n3026
node n3028 s26 last -> n3027
node n3029 s32 last -> n3021
node n3030 s7 -> n1948
node n3031 s47 last -> n3016
node n3032 s33 last -> n3030
node n3033 s17 -> n3032
node n3034 s22 last -> n3037
node n3035 s7 -> n1948
node n3036 s49 last -> n3027
node n3037 s37 last -> n3035
node n3038 s53 last -> n3012
node n3039 s47 -> n3038
node n3040 s49 last -> n3041
node n3041 s54 last -> n3023
node n3042 s17 last -> n3012
node n3043 s41 -> n3042
node n3044 s43 last -> n3045
node n3045 s22 last -> n3023
node n3046 s2 word -> n2920
node n3047 s3 word last -> n3164
node n3048 s19 last -> n694
node n3049 s18 last -> n3048
node n3050 s20 last -> n3049
node n3051 s35 -> n3050
node n3052 s7 last -> n691
node n3053 s42 last -> n3051
node n3054 s20 -> n3053
node n3055 s25 -> n3063
node n3056 s35 -> n3066
node n3057 s39 last -> n3072
node n3058 s24 last -> n694
node n3059 s23 last -> n3058
node n3060 s25 last -> n3059
node n3061 s39 -> n3060
node n3062 s7 last -> n691
node n3063 s44 last -> n3061
node n3064 s41 -> n694
node n3065 s7 last -> n691
node n3066 s42 -> n3064
node n3067 s27 last -> n3068
node n3068 s19 -> n694
node n3069 s7 last -> n691
node n3070 s43 -> n694
node n3071 s7 last -> n691
node n3072 s44 -> n3070
node n3073 s30 last -> n3074
node n3074 s24 -> n694
node n3075 s7 last -> n691
node n3076 s45 -> n3054
node n3077 s50 -> n3088
node n3078 s26 -> n3099
node n3079 s25 -> n3110
node n3080 s38 -> n3137
node n3081 s49 -> n3151
node n3082 s44 -> n3157
node n3083 s52 last -> n3161
node n3084 s35 last -> n3050
node n3085 s18 -> n3084
node n3086 s7 last -> n691
node n3087 s46 last -> n3085
node n3088 s41 -> n3087
node n3089 s43 last -> n3093
node n3090 s39 last -> n3060
node n3091 s23 -> n3090
node n3092 s7 last -> n691
node n3093 s48 last -> n3091
node n3094 s16 last -> n694
node n3095 s17 -> n3094
node n3096 s7 last -> n691
node n3097 s18 -> n3095
node n3098 s19 last -> n694
node n3099 s20 -> n3097
node n3100 s25 last -> n3104
node n3101 s21 last -> n694
node n3102 s22 -> n3101
node n3103 s7 last -> n691
node n3104 s23 -> n3102
node n3105 s24 last -> n694
node n3106 s27 last -> n694
node n3107 s28 -> n3106
node n3108 s7 last -> n691
node n3109 s18 last -> n3107
node n3110 s29 -> n3109
node n3111 s32 -> n3117
node n3112 s35 -> n3122
node n3113 s39 last -> n3127
node n3114 s30 last -> n694
node n3115 s31 -> n3114
node n3116 s7 last -> n691
node n3117 s23 last -> n3115
node n3118 s33 last -> n694
node n3119 s34 last -> n3118
node n3120 s35 -> n3119
node n3121 s7 last -> n691
node n3122 s36 last -> n3120
node n3123 s37 last -> n694
node n3124 s38 last -> n3123
node n3125 s39 -> n3124
node n3126 s7 last -> n691
node n3127 s40 last -> n3125
node n3128 s7 -> n691
node n3129 s33 last -> n3134
node n3130 s18 last -> n694
node n3131 s41 last -> n3130
node n3132 s46 last -> n3131
node n3133 s51 last -> n3132
node n3134 s41 -> n3133
node n3135 s7 last -> n691
node n3136 s29 last -> n3128
node n3137 s41 -> n3136
node n3138 s43 last -> n3147
node n3139 s7 -> n691
node n3140 s37 last -> n3145
node n3141 s23 last -> n694
node n3142 s43 last -> n3141
node n3143 s48 last -> n3142
node n3144 s26 last -> n3143
node n3145 s43 -> n3144
node n3146 s7 last -> n691
node n3147 s32 last -> n3139
node n3148 s47 -> n3132
node n3149 s7 last -> n691
node n3150 s33 last -> n3148
node n3151 s17 -> n3150
node n3152 s22 last -> n3155
node n3153 s49 -> n3143
node n3154 s7 last -> n691
node n3155 s37 last -> n3153
node n3156 s53 last -> n3134
node n3157 s47 -> n3156
node n3158 s49 last -> n3159
node n3159 s54 last -> n3145
node n3160 s17 last -> n3134
node n3161 s41 -> n3160
node n3162 s43 last -> n3163
node n3163 s22 last -> n3145
node n3164 s15 last -> n3076
node n3165 s3 -> n3046
node n3166 s9 word -> n3170
node n3167 s13 word -> n3190
node n3168 s1 word -> n3194
node n3169 s6 word last -> n3198
node n3170 s12 word -> n2920
node n3171 s11 word -> n3174
node n3172 s7 -> n2915
node n3173 s15 last -> n3178
node n3174 s7 -> n2915
node n3175 s4 -> n3177
node n3176 s15 last -> n2924
node n3177 s4 word last -> n3164
node n3178 s5 -> n2922
node n3179 s1 -> n2936
node n3180 s45 -> n2799
node n3181 s50 -> n2833
node n3182 s26 -> n2844
node n3183 s25 -> n2855
node n3184 s38 -> n2882
node n3185 s49 -> n2896
node n3186 s44 -> n2902
node n3187 s52 -> n2906
node n3188 s6 -> n694
node n3189 s3 last -> n1983
node n3190 s2 word -> n2920
node n3191 s7 -> n2915
node n3192 s3 word -> n3174
node n3193 s15 last -> n3178
node n3194 s7 -> n2915
node n3195 s0 word -> n2920
node n3196 s10 word -> n3164
node n3197 s15 last -> n3178
node n3198 s7 -> n2915
node n3199 s4 word -> n3164
node n3200 s15 last -> n3178
node n3201 s2 word -> n1081
node n3202 s3 word last
node n3203 s3 -> n3201
node n3204 s9 word -> n3216
node n3205 s13 word -> n3221
node n3206 s1 word -> n3224
node n3207 s6 word -> n3227
node n3208 s50 -> n124
node n3209 s25 -> n75
node n3210 s44 -> n137
node n3211 s52 -> n131
node n3212 s26 -> n58
node n3213 s45 -> n94
node n3214 s49 -> n115
node n3215 s38 last -> n142
node n3216 s12 word -> n1081
node n3217 s11 word -> n3219
node n3218 s15 last -> n691
node n3219 s4 -> n21
node n3220 s15 last -> n691
node n3221 s2 word -> n1081
node n3222 s3 word -> n3219
node n3223 s15 last -> n691
node n3224 s0 word -> n1081
node n3225 s15 -> n691
node n3226 s10 word last
node n3227 s15 -> n691
node n3228 s4 word last
node n3229 s7 -> n3165
node n3230 s15 -> n3203
node n3231 s55 last -> n2774
node n3232 s19 word last -> n3229
node n3233 s18 last -> n3232
node n3234 s20 last -> n3233
node n3235 s42 word last -> n2789
node n3236 s20 -> n3235
node n3237 s25 -> n3247
node n3238 s35 -> n3252
node n3239 s39 last -> n3262
node n3240 s55 word -> n2774
node n3241 s7 -> n3165
node n3242 s15 -> n3203
node n3243 s39 last -> n3246
node n3244 s24 word last -> n3229
node n3245 s23 last -> n3244
node n3246 s25 last -> n3245
node n3247 s44 word last -> n3240
node n3248 s41 word -> n3229
node n3249 s55 word -> n2774
node n3250 s7 -> n3165
node n3251 s15 last -> n3203
node n3252 s42 word -> n3248
node n3253 s27 word last -> n3254
node n3254 s19 word -> n3229
node n3255 s55 word -> n2774
node n3256 s7 -> n3165
node n3257 s15 last -> n3203
node n3258 s43 word -> n3229
node n3259 s55 word -> n2774
node n3260 s7 -> n3165
node n3261 s15 last -> n3203
node n3262 s44 word -> n3258
node n3263 s30 word last -> n3264
node n3264 s24 word -> n3229
node n3265 s55 word -> n2774
node n3266 s7 -> n3165
node n3267 s15 last -> n3203
node n3268 s35 last -> n3234
node n3269 s18 -> n3268
node n3270 s55 word -> n2774
node n3271 s7 -> n3165
node n3272 s15 last -> n3203
node n3273 s46 word last -> n3269
node n3274 s41 -> n3273
node n3275 s43 -> n3282
node n3276 s24 last -> n3287
node n3277 s39 last -> n3246
node n3278 s23 -> n3277
node n3279 s55 word -> n2774
node n3280 s7 -> n3165
node n3281 s15 last -> n3203
node n3282 s48 word last -> n3278
node n3283 s62 word last -> n2571
node n3284 s63 -> n3283
node n3285 s15 last -> n2562
node n3286 s4 word last -> n3284
node n3287 s4 last -> n3286
node n3288 s16 word last -> n3229
node n3289 s17 -> n3288
node n3290 s55 word -> n2774
node n3291 s7 -> n3165
node n3292 s15 last -> n3203
node n3293 s18 word -> n3289
node n3294 s19 word last -> n3229
node n3295 s20 -> n3293
node n3296 s25 last -> n3302
node n3297 s21 word last -> n3229
node n3298 s22 -> n3297
node n3299 s55 word -> n2774
node n3300 s7 -> n3165
node n3301 s15 last -> n3203
node n3302 s23 word -> n3298
node n3303 s24 word last -> n3229
node n3304 s27 word last -> n3229
node n3305 s28 -> n3304
node n3306 s55 word -> n2774
node n3307 s7 -> n3165
node n3308 s15 last -> n3203
node n3309 s18 word last -> n3305
node n3310 s29 -> n3309
node n3311 s32 -> n3319
node n3312 s35 -> n3326
node n3313 s39 last -> n3333
node n3314 s30 word last -> n3229
node n3315 s31 -> n3314
node n3316 s55 word -> n2774
node n3317 s7 -> n3165
node n3318 s15 last -> n3203
node n3319 s23 word last -> n3315
node n3320 s33 word last -> n3229
node n3321 s34 last -> n3320
node n3322 s35 -> n3321
node n3323 s55 word -> n2774
node n3324 s7 -> n3165
node n3325 s15 last -> n3203
node n3326 s36 word last -> n3322
node n3327 s37 word last -> n3229
node n3328 s38 last -> n3327
node n3329 s39 -> n3328
node n3330 s55 word -> n2774
node n3331 s7 -> n3165
node n3332 s15 last -> n3203
node n3333 s40 word last -> n3329
node n3334 s55 word -> n2774
node n3335 s7 -> n3165
node n3336 s15 -> n3203
node n3337 s33 word last -> n3338
node n3338 s55 word -> n2774
node n3339 s7 -> n3165
node n3340 s15 -> n3203
node n3341 s41 last -> n3345
node n3342 s18 word last -> n3229
node n3343 s41 last -> n3342
node n3344 s46 last -> n3343
node n3345 s51 last -> n3344
node n3346 s29 word last -> n3334
node n3347 s41 -> n3346
node n3348 s43 last -> n3361
node n3349 s55 word -> n2774
node n3350 s7 -> n3165
node n3351 s15 -> n3203
node n3352 s37 word last -> n3353
node n3353 s55 word -> n2774
node n3354 s7 -> n3165
node n3355 s15 -> n3203
node n3356 s43 last -> n3360
node n3357 s23 word last -> n3229
node n3358 s43 last -> n3357
node n3359 s48 last -> n3358
node n3360 s26 last -> n3359
node n3361 s32 word last -> n3349
node n3362 s55 word -> n2774
node n3363 s7 -> n3165
node n3364 s15 -> n3203
node n3365 s47 last -> n3344
node n3366 s33 word last -> n3362
node n3367 s17 -> n3366
node n3368 s22 last -> n3373
node n3369 s55 word -> n2774
node n3370 s7 -> n3165
node n3371 s15 -> n3203
node n3372 s49 last -> n3359
node n3373 s37 word last -> n3369
node n3374 s53 word last -> n3338
node n3375 s47 -> n3374
node n3376 s49 last -> n3377
node n3377 s54 word last -> n3353
node n3378 s17 word last -> n3338
node n3379 s41 -> n3378
node n3380 s43 last -> n3381
node n3381 s22 word last -> n3353
node n3382 s66 last -> n615
node n3383 s15 last -> n3382
node n3384 s4 word last -> n3383
node n3385 s4 last -> n3384
node n3386 s4 last -> n2572
node n3387 s24 last -> n3386
node n3388 s50 -> n3387
node n3389 s15 last -> n2562
node n3390 s62 word last -> n3388
