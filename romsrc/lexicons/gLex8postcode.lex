# gLex8postcode: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 "0123456789" "t0"
set s1 "-" "t1"
set s2 "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "t2"
set s3 "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ" "t3"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s0 word -> n14
node n1 s2 word last -> n23
node n2 s0 last -> n17
node n3 s0 last -> n2
node n4 s0 last -> n3
node n5 s1 -> n4
node n6 s0 word last
node n7 s0 word -> n5
node n8 s2 last -> n9
node n9 s2 word last
node n10 s0 word -> n7
node n11 s1 last -> n3
node n12 s0 word -> n10
node n13 s2 word last -> n9
node n14 s0 word -> n12
node n15 s2 word last -> n16
node n16 s3 word last
node n17 s0 word last
node n18 s2 last -> n17
node n19 s0 last -> n18
node n20 s1 last -> n19
node n21 s2 word -> n20
node n22 s0 word last -> n3
node n23 s0 word -> n21
node n24 s2 word -> n30
node n25 s1 last -> n33
node n26 s0 word last -> n17
node n27 s0 last -> n26
node n28 s0 word -> n27
node n29 s2 word last
node n30 s0 word -> n28
node n31 s1 last -> n33
node n32 s0 last -> n27
node n33 s0 last -> n32
