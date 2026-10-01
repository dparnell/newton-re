# gLex8WorldPhone: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 "0123456789" "t0"
set s1 "#*" "t1"
set s2 ",-./0123456789" "t2"
set s3 ",-./" "t3"
set s4 "(" "t4"
set s5 ")" "t5"
set s6 "+" "t6"
set s7 "-" "t7"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s1 word -> n29
node n1 s0 word -> n7
node n2 s4 last -> n17
node n3 s0 word -> n3
node n4 s3 word -> n7
node n5 s1 word -> n29
node n6 s4 last -> n17
node n7 s1 word -> n29
node n8 s2 word -> n7
node n9 s4 last -> n17
node n10 s3 word -> n10
node n11 s4 -> n17
node n12 s0 word last -> n13
node n13 s2 word last -> n13
node n14 s5 word -> n10
node n15 s0 last -> n14
node n16 s0 last -> n14
node n17 s6 -> n16
node n18 s0 last -> n19
node n19 s5 word -> n10
node n20 s0 -> n22
node n21 s7 last -> n28
node n22 s5 word -> n10
node n23 s0 -> n14
node n24 s7 last -> n28
node n25 s5 word last -> n10
node n26 s0 -> n25
node n27 s5 word last -> n10
node n28 s0 last -> n26
node n29 s0 word -> n3
node n30 s3 word -> n33
node n31 s1 word -> n29
node n32 s4 last -> n17
node n33 s1 word -> n29
node n34 s0 word -> n7
node n35 s4 -> n17
node n36 s3 word last -> n33
