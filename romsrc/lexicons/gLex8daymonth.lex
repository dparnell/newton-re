# gLex8daymonth: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 "," "t0"
set s1 "." "t1"
set s2 "y" "t2"
set s3 "r" "t3"
set s4 "a" "t4"
set s5 "u" "t5"
set s6 "n" "t6"
set s7 ".e" "t7"
set s8 ".y" "t8"
set s9 "l" "t9"
set s10 "J" "t10"
set s11 "b" "t11"
set s12 "e" "t12"
set s13 "d" "t13"
set s14 "i" "t14"
set s15 "F" "t15"
set s16 "h" "t16"
set s17 "c" "t17"
set s18 "o" "t18"
set s19 "M" "t19"
set s20 "p" "t20"
set s21 "t" "t21"
set s22 "s" "t22"
set s23 "g" "t23"
set s24 "A" "t24"
set s25 "m" "t25"
set s26 "S" "t26"
set s27 "O" "t27"
set s28 "v" "t28"
set s29 "N" "t29"
set s30 "D" "t30"
set s31 "T" "t31"
set s32 "W" "t32"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s10 -> n17
node n1 s15 -> n31
node n2 s19 -> n46
node n3 s24 -> n56
node n4 s26 -> n75
node n5 s27 -> n90
node n6 s29 -> n92
node n7 s30 -> n94
node n8 s31 -> n101
node n9 s32 last -> n120
node n10 s1 word -> n25
node n11 s5 -> n15
node n12 s0 word last
node n13 s2 word last -> n25
node n14 s3 last -> n13
node n15 s4 last -> n14
node n16 s6 word last -> n10
node n17 s4 -> n16
node n18 s5 last -> n21
node n19 s7 word -> n25
node n20 s0 word last
node n21 s6 word -> n19
node n22 s9 word last -> n23
node n23 s8 word -> n25
node n24 s0 word last
node n25 s0 word last
node n26 s1 word -> n25
node n27 s3 -> n29
node n28 s0 word last
node n29 s5 last -> n15
node n30 s11 word last -> n26
node n31 s12 -> n30
node n32 s3 word last -> n37
node n33 s1 word -> n25
node n34 s13 -> n36
node n35 s0 word last
node n36 s4 last -> n13
node n37 s14 word -> n33
node n38 s1 word -> n25
node n39 s0 word last
node n40 s1 word -> n25
node n41 s17 -> n43
node n42 s0 word last
node n43 s16 word last -> n25
node n44 s3 word -> n40
node n45 s2 word last -> n25
node n46 s4 -> n44
node n47 s18 word last -> n48
node n48 s6 word -> n33
node n49 s1 word -> n25
node n50 s0 word last
node n51 s1 word -> n25
node n52 s14 -> n54
node n53 s0 word last
node n54 s9 word last -> n25
node n55 s3 word last -> n51
node n56 s20 -> n55
node n57 s5 last -> n63
node n58 s1 word -> n25
node n59 s5 -> n62
node n60 s0 word last
node n61 s21 word last -> n25
node n62 s22 last -> n61
node n63 s23 word last -> n58
node n64 s1 word -> n25
node n65 s21 word -> n67
node n66 s0 word last
node n67 s1 word -> n25
node n68 s12 -> n73
node n69 s0 word last
node n70 s3 word last -> n25
node n71 s12 last -> n70
node n72 s11 last -> n71
node n73 s25 last -> n72
node n74 s20 word last -> n64
node n75 s12 -> n74
node n76 s4 word -> n83
node n77 s5 word last -> n48
node n78 s1 word -> n25
node n79 s5 -> n82
node n80 s0 word last
node n81 s13 last -> n36
node n82 s3 last -> n81
node n83 s21 word -> n78
node n84 s1 word -> n25
node n85 s0 word last
node n86 s1 word -> n25
node n87 s18 -> n72
node n88 s0 word last
node n89 s21 word last -> n86
node n90 s17 last -> n89
node n91 s28 word last -> n67
node n92 s18 last -> n91
node n93 s17 word last -> n67
node n94 s12 last -> n93
node n95 s1 word -> n25
node n96 s12 word -> n98
node n97 s0 word last
node n98 s1 word -> n25
node n99 s22 word -> n33
node n100 s0 word last
node n101 s5 word -> n95
node n102 s16 word last -> n106
node n103 s1 word -> n25
node n104 s3 word -> n98
node n105 s0 word last
node n106 s5 word -> n103
node n107 s1 word -> n25
node n108 s0 word last
node n109 s1 word -> n25
node n110 s22 word -> n113
node n111 s6 -> n116
node n112 s0 word last
node n113 s1 word -> n25
node n114 s0 word last
node n115 s22 last -> n81
node n116 s12 last -> n115
node n117 s13 word -> n109
node n118 s1 word -> n25
node n119 s0 word last
node n120 s12 word last -> n117
