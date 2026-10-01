# gLex8FunnyPhone: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 "0123456789ABCDEFGHIJKLMNOPRSTUVWXY" "t0"
set s1 "-" "t1"
set s2 "23456789ABCDEFGHIJKLMNOPRSTUVWXY" "t2"
set s3 "-/" "t3"
set s4 "0123456789" "t4"
set s5 "23456789" "t5"
set s6 ")" "t6"
set s7 "(" "t7"
set s8 "ABCDEFGHIJKLMNOPRSTUVWXY" "t8"
set s9 "01ABCDEFGHIJKLMNOPRSTUVWXY" "t9"
set s10 "01" "t10"
set s11 "/" "t11"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s10 -> n22
node n1 s5 -> n43
node n2 s7 -> n21
node n3 s8 last -> n34
node n4 s0 last -> n30
node n5 s0 last -> n4
node n6 s0 last -> n5
node n7 s1 -> n6
node n8 s0 last -> n5
node n9 s0 last -> n7
node n10 s0 last -> n9
node n11 s2 last -> n10
node n12 s3 -> n11
node n13 s2 last -> n10
node n14 s4 last -> n12
node n15 s4 last -> n14
node n16 s5 -> n15
node n17 s7 last -> n21
node n18 s6 last -> n12
node n19 s4 last -> n18
node n20 s4 last -> n19
node n21 s5 last -> n20
node n22 s3 -> n16
node n23 s5 -> n28
node n24 s7 -> n21
node n25 s9 last -> n4
node n26 s4 word -> n12
node n27 s8 word last
node n28 s4 -> n26
node n29 s8 last -> n30
node n30 s0 word last
node n31 s1 -> n6
node n32 s0 word last -> n5
node n33 s0 last -> n31
node n34 s0 last -> n33
node n35 s2 -> n34
node n36 s10 last -> n5
node n37 s1 -> n35
node n38 s11 -> n11
node n39 s2 word -> n34
node n40 s10 word last -> n5
node n41 s4 -> n37
node n42 s8 last -> n31
node n43 s4 -> n41
node n44 s8 last -> n33
